#ifdef CH_LANG_CC
/*
 *      _______              __
 *     / ___/ /  ___  __ _  / /  ___
 *    / /__/ _ \/ _ \/  V \/ _ \/ _ \
 *    \___/_//_/\___/_/_/_/_.__/\___/
 *    Please refer to Copyright.txt, in Chombo's root directory.
 */
#endif

#include <iostream>
#include <fstream>
using namespace std;

#include "FABView.H"
// this lets us use dumpIVS, other dump functions
#include "DebugDump.H"

#include "PyParse.H"
#include "CH_HDF5.H"
#include "parstream.H"
#include "CH_Timer.H"
#include "memusage.H"

#include "AMR.H"
#include "Scheduler.H"
#include "AMRLevel.H"
#include "AMRLevelMappedConsFactory.H"
#include "AMRLevelMappedCons.H"
#include "LevelGridMetrics.H"
#include "MayDay.H"

// Physics objects for shallow-water equations.
#include "MOLShallowWaterPhysics.H"

// Source term for shallow-water equations.
#include "LevelSWSourceTerm.H"

// We use analytic functions for initial
// conditions and boundary conditions.
#include "ShallowWaterCubedSphereIBC.H"

// Strategies for stability, tagging.
#include "SWMappedStabilityStrategy.H"
#include "SWMappedTaggingStrategy.H"

// Coordinate systems.
#include "CartesianCS.H"
#include "AnalyticCS.H"
#include "SingleBlockCSAdaptor.H"
#include "DoubleCartesianCS.H"
#include "CubedSphere2DCS.H"

#include "generalFuncs.H"

// #define HALEM_PROC_SPEED
#ifdef HALEM_PROC_SPEED
#include <cstdio>
#include <sys/sysinfo.h>
#include <machine/hal_sysinfo.h>
#endif

#ifdef CH_MPI
#include "CH_Attach.H"
#endif

#ifdef CH_Linux
// Should be undefined by default
#define TRAP_FPE
#undef  TRAP_FPE
#endif

#ifdef TRAP_FPE
static void enableFpExceptions();
#endif

OldTimer Everything    ("gov Everything", 0);
OldTimer TimeReadInput ("gov Read Input",   Everything);
OldTimer TimeSetupAMR  ("gov Setup AMR",    Everything);
OldTimer TimeRun       ("gov Run",          Everything);
OldTimer TimeConclude  ("gov Conclude",     Everything);

// amrRun is a function (as opposed to inline in main()) to get
// around MPI scoping problems
void amrRun();

// setupFixedGrids allows fixed grids to be read in and used in this AMR
// computation example
void setupFixedGrids(Vector<Vector<Box> >& a_amrGrids,
                     const ProblemDomain&  a_domain,
                     int                   a_maxLevel,
                     int                   a_maxGridSize,
                     int                   a_blockFactor,
                     int                   a_verbosity,
                     string           a_gridFile);

// setupVortices allows vortex parameters to be read in and used in this AMR
// computation example
void setupVortices(Vector<RealVect>& a_center,
                   Vector<Real>&     a_radius,
                   Vector<Real>&     a_strength,
                   int               a_verbosity,
                   string       a_vortexFile);

// One more function for MPI
void dumpmemoryatexit();

int main(int a_argc, char* a_argv[])
{
#ifdef CH_MPI
  // Start MPI
  MPI_Init(&a_argc,&a_argv);
#ifdef CH_AIX
  H5dont_atexit();
#endif
  setChomboMPIErrorHandler();
#endif

  int rank, number_procs;

#ifdef CH_MPI
  MPI_Comm_rank(Chombo_MPI::comm, &rank);
  MPI_Comm_size(Chombo_MPI::comm, &number_procs);
#else
  rank = 0;
  number_procs = 1;
#endif

  if (rank == 0)
  {
    pout() << " number_procs = " << number_procs << endl;
  }

  OldTimer::TimerInit(rank);

  Everything.start();

  // Check for an input file
  char* inFile = NULL;

  if (a_argc > 1)
  {
    inFile = a_argv[1];
  }
  else
  {
    pout() << "Usage:  swsphere...ex <inputfile>" << endl;
    pout() << "No input file specified" << endl;
    return -1;
  }

  // Parse the input file.
  PyParse parms(inFile);

#ifdef TRAP_FPE
  enableFpExceptions();
#endif

  // Run amrRun, i.e., do the computation
  amrRun();

  Everything.stop();

#ifndef CH_NTIMER
  Real end_memory = get_memory_usage_from_OS();

  pout() << endl
         << "Everything completed --- "
         << "mem: "
         << setw(8) << setprecision(3)
         << setiosflags(ios::fixed)
         << end_memory
         << " MB, time: "
         << setw(8) << setprecision(3)
         << setiosflags(ios::fixed)
         << Everything.wc_time()
         << " sec (wall-clock)" << endl << endl;
#endif

#if !defined(CH_NTIMER) && defined(CH_MPI)
  Real avg_memory, min_memory, max_memory;
  gather_memory_from_procs(end_memory, avg_memory, min_memory, max_memory);
#endif

  // OldTimer::TimerSummary();

#ifdef CH_MPI
  // Exit MPI
  dumpmemoryatexit();
  MPI_Finalize();
#endif
}
//-----------------------------------------------------------------------

//-----------------------------------------------------------------------
void
amrRun()
{
  // Start timing the reading of the input file
  TimeReadInput.start();

  // Here's our parser.
  PyParse parser;

  // This determines the amount of diagnositic output generated
  int verbosity = 0;
  parser.query("verbosity",verbosity);
  CH_assert(verbosity >= 0);

  // Stop after this number of steps
  int nstop = 0;
  parser.get("max_step",nstop);

  // Stop when the simulation time get here
  Real stopTime = 0.0;
  parser.get("max_time",stopTime);

  // Maximum AMR level limit
  int maxLevel = 0;
  parser.get("max_level", maxLevel);

  // Refinement ratios between levels
  vector<int> refRatios(maxLevel+1, 2);
  // Note: this requires a refRatio to be defined for the finest level
  // (even though it will never be used)
  parser.get("ref_ratio",refRatios);
  if (refRatios.size() <= maxLevel)
    refRatios.resize(maxLevel + 1, refRatios.back());

  // Number of coarse time steps from one regridding to the next
  vector<int> regridIntervals(maxLevel+1, 2);
  parser.query("regrid_interval", regridIntervals);
  if (regridIntervals.size() < maxLevel)
    regridIntervals.resize(maxLevel + 1, regridIntervals.back());

  // How far to extend refinement from cells newly tagged for refinement
  int tagBufferSize = 3;
  parser.query("tag_buffer_size",tagBufferSize);

  // Threshold that triggers refinement
  Real refineThresh = 0.3;
  parser.query("refine_thresh",refineThresh);

  // Whether refinement threshold is scaled with dx
  int refinementIsScaledInt = 0;
  parser.query("refinement_is_scaled", refinementIsScaledInt);
  bool refinementIsScaled = (refinementIsScaledInt == 1);

  // Minimum dimension of a grid
  int blockFactor = 1;
  parser.query("block_factor",blockFactor);

  // Maximum dimension of a grid
  int maxGridSize = 32;
  parser.query("max_grid_size",maxGridSize);

  Real fillRatio = 0.75;
  parser.query("fill_ratio",fillRatio);

  // Grid buffer size
  int gridBufferSize = 1;
  const int codeReqBufferSize = LevelGridMetrics::bufferSize4thO(
    refRatios,
    maxLevel,
    5);  // Num ghost 5 is hard coded in AMRLevelMappedCons
  if (parser.contains("grid_buffer_size"))
  {
    parser.get("grid_buffer_size", gridBufferSize);
    if (gridBufferSize < codeReqBufferSize)
    {
      pout() << "\nWARNING: Program requested grid buffer size: "
        << codeReqBufferSize
        << "\n         User requested grid buffer size   : "
        << gridBufferSize
        << "\nUsing a buffer size that is too small may corrupt "
        "the solution.\n";
      MayDay::Warning("Do not specify the grid buffer size to avoid this "
          "warning");
      pout() << endl;
    }
  }
  else
    {
      gridBufferSize = codeReqBufferSize;
    }

  // Order of the normal predictor (CTU -> 0, PLM -> 1, PPM -> 2)
  string normalPred = "PPM";
  int normalPredOrder = 0;
  parser.query("normal_predictor",normalPred);
  if (normalPred == "CTU" || normalPred == "ctu")
  {
    normalPredOrder = 0;
  }
  else if (normalPred == "PLM" || normalPred == "plm")
  {
    normalPredOrder = 1;
  }
  else if (normalPred == "PPM" || normalPred == "ppm")
  {
    normalPredOrder = 2;
  }
  else
  {
    MayDay::Error("Normal predictor must by PLM or PPM");
  }

  // Use fourth order slopes:  default true
  int inFourthOrderSlopes = 1;
  bool useFourthOrderSlopes;
  parser.query("use_fourth_order_slopes",inFourthOrderSlopes);
  useFourthOrderSlopes = (inFourthOrderSlopes == 1);

  // Arbitrary fiat by petermc, 17 June 2008
  useFourthOrderSlopes = true;

  // Do slope limiting:  default true
  bool usePrimLimiting = true;
  parser.query("use_prim_limiting", usePrimLimiting);

  // This should actually be 1 even if slope limiting is off
  bool highOrderLimiter = true;
  parser.query("high_order_limiter", highOrderLimiter);

  // NEW Kreiss-Oliger artificial viscosity
  bool useArtVisc = false;
  parser.query("use_art_visc", useArtVisc);

  Real ratioArtVisc = 0.;
  if (useArtVisc)
    {
      parser.query("ratio_art_visc", ratioArtVisc);
    }

  bool forwardEuler = false;
  parser.query("forward_euler", forwardEuler);

  // Do slope limiting using characteristics
  bool useCharLimiting = false;
  parser.query("use_char_limiting",useCharLimiting);

  // Initial values are averaged, since the solution is
  // given pointwise.
  bool initialAverage = true;

  // Do slope flattening:  default false
  bool useFlattening = false;
  parser.query("use_flattening", useFlattening);

  // Avoid PPM:  default false
  bool noPPM = false;
  parser.query("no_ppm", noPPM);

  // Do deconvolution:  default true
  bool doDeconvolution = true;
  parser.query("do_deconvolution", doDeconvolution);

  // Do deconvolution:  default true
  bool doFaceDeconvolution = true;
  parser.query("do_face_deconvolution", doFaceDeconvolution);

  // Apply artificial viscosity based on divergence.
  // Artificial viscosity coefficient/multiplier
  Real artificialViscosity = 0.0;
  parser.query("artificial_viscosity",artificialViscosity);
  bool useArtificialViscosity = (artificialViscosity > 0.0);

  // Set up checkpointing
  int checkpointInterval = 0;
  parser.query("checkpoint_interval",checkpointInterval);

  // Set up plot file writing
  int plotInterval = 0;
  parser.query("plot_interval",plotInterval);

  // CFL multiplier
  Real cfl = 0.8;
  parser.get("cfl",cfl);

  // Initial CFL multiplier
  Real initialCFL = 0.1;
  parser.query("initial_cfl",initialCFL);

  // Set up whether to use subcycling in time.
  bool useSubcycling = true;
  parser.query("use_subcycling", useSubcycling);

  // Determine if a fixed or variable time step will be used
  Real fixedDt = -1;
  parser.query("fixed_dt",fixedDt);

  // Limit the time step growth
  Real maxDtGrowth = 1.1;
  parser.query("max_dt_growth",maxDtGrowth);

  // Let the time step grow by this factor above the "maximum" before
  // reducing it
  Real dtToleranceFactor = 1.1;
  parser.query("dt_tolerance_factor",dtToleranceFactor);

  // End timing the reading of the input file
  TimeReadInput.stop();

#ifndef CH_NTIMER
  pout() << "Input Read completed --- "
         << "mem: "
         << setw(8) << setprecision(3)
         << setiosflags(ios::fixed)
         << get_memory_usage_from_OS()
         << " MB, time: "
         << setw(8) << setprecision(3)
         << setiosflags(ios::fixed)
         << TimeReadInput.wc_time()
         << " sec (wall-clock)" << endl;
#endif

  // Start timing AMR solver setup
  TimeSetupAMR.start();

  // Don't use source term by default
  bool useSourceTerm = false;

  // Source term multiplier
  Real sourceTermScaling = 0.0;

  // Create and define IBC (initial and boundary condition) object
  RefCountedPtr<ScalarFunction> height;
  parser.get("height", height);

  // Advection velocity field or stream function.
  // RefCountedPtr<ScalarFunction> streamFunction;
  // parser.query("stream_function", streamFunction);
  RefCountedPtr<VectorFunction> velocity;
  parser.get("velocity", velocity);
  //  if (velocity.isNull() and streamFunction.isNull())
  //    MayDay::Error("Either velocity or stream_function must be given.");

  // If we have both a stream function and a velocity function, prefer the
  // former.
  //  if (!streamFunction.isNull() and !velocity.isNull())
  //    velocity = RefCountedPtr<VectorFunction>();

  // Coordinate system for mapped grids
  ProblemDomain probDomain;
  MultiBlockCoordSysFactory* coordSysFact = NULL;
  vector<int> numCells(SpaceDim, 0);
  //  string coordSys;
  //  parser.query("coord_sys", coordSys);
  bool isPeriodic[SpaceDim];
  MOLPhysics* molPhysics = NULL;
  Real domainLength = 1.0;
  SWMappedStabilityStrategy* stabilityStrategy = NULL;
  Real stabilityFactor = 1.3925;

  // gravitational acceleration
  Real g = 9.80616;
  parser.query("g", g);

  // rotational velocity
  Real omega = 7.292e-5;
  parser.query("Omega", omega);

  // angle between axis of solid-body rotation and polar axis
  Real alpha = 0.0;
  parser.query("alpha", alpha);

  bool mapLonLat = false;
  parser.query("lonlat", mapLonLat);

  // sphere radius should be set to 1.
  //  Real a = 6.37122e6;
  //  parser.query("a", a);

  LevelSourceTerm* sourceTerm = NULL;

  // Cubed-sphere coordinate system.
  {
    CH_assert(SpaceDim == 2);
    parser.get("num_cells", numCells[0]);
    numCells[1] = numCells[0];
    numCells[0] *= 11;

    // The "domain length," which determines the grid spacing, is
    // pi/2 * each panel, including the space in between.
    domainLength = 11.0 * 0.5 * M_PI;

    // On a cubed sphere, the coordinate system isn't periodic
    // in the usual sense.
    for (int dim = 0; dim < SpaceDim; dim++)
      isPeriodic[dim] = 0;

    probDomain.define(IntVect::Zero,
                      IntVect(D_DECL(numCells[0]-1,
                      numCells[1]-1,
                      numCells[2]-1)),
                      isPeriodic);

    coordSysFact = new CubedSphere2DCSFactory();
    // set real space to longitude-latitude?
    ((CubedSphere2DCSFactory*) coordSysFact)->setFlatMap(mapLonLat);
    PhysIBC* ibc = new ShallowWaterCubedSphereIBC(height, velocity,
                                                  g, omega, alpha);

    // Set up the physics for shallow-water equations.
    MOLShallowWaterPhysics* swPhysics =
      new MOLShallowWaterPhysics(height, velocity);
    swPhysics->setPhysIBC(ibc);
    // Cast to physics base class pointer for technical reasons
    molPhysics = dynamic_cast<MOLPhysics*>(swPhysics);

    useSourceTerm = true;
    LevelSWSourceTerm* swSourceTerm = new LevelSWSourceTerm();
    // Cast to source term base class pointer for technical reasons
    sourceTerm = dynamic_cast<LevelSourceTerm*>(swSourceTerm);

    // This computes a stable time step.
    // if (!velocity.isNull())
    stabilityStrategy = new SWMappedStabilityStrategy(stabilityFactor, velocity);
  }

  // Note: the AMRLevelFactory takes responsibility for destruction
  // of coordSysFact.

  // Set up output files
  string plotPrefix;
  parser.query("plot_prefix", plotPrefix);

  string chkPrefix;
  parser.query("checkpoint_prefix", chkPrefix);

  if (verbosity >= 2)
  {
    pout() << "verbosity = " << verbosity << endl;

    pout() << "maximum_step = " << nstop << endl;
    pout() << "maximum_time = " << stopTime << endl;
    if (fixedDt > 0)
    {
      pout() << "fixed_dt = " << fixedDt << endl;
    }

    pout() << "number_of_cells = " << D_TERM(numCells[0] << "  " <<,
                                             numCells[1] << "  " <<,
                                             numCells[2] << ) endl;
    pout() << "is_period = " << D_TERM(isPeriodic[0] << "  " <<,
                                       isPeriodic[1] << "  " <<,
                                       isPeriodic[2] << ) endl;

    pout() << "maximum_level = " << maxLevel << endl;
    pout() << "refinement_ratio = ";
    for (int i = 0; i < refRatios.size(); ++i)
    {
      pout() << refRatios[i] << " ";
    }
    pout() << endl;

    pout() << "regrid_interval = ";
    for (int i = 0; i < regridIntervals.size(); ++i)
    {
      pout() << regridIntervals[i] << " ";
    }
    pout() << endl;
    pout() << "tag_buffer_size = " << tagBufferSize << endl;

    pout() << "refinement_threshold = " << refineThresh << endl;

    pout() << "blocking_factor = " << blockFactor << endl;
    pout() << "max_grid_size = " << maxGridSize << endl;
    pout() << "fill_ratio = " << fillRatio << endl;
    pout() << "grid buffer size = " << gridBufferSize << endl;

    pout() << "normal_predictor = ";
    if (normalPredOrder == 1)
    {
      pout() << "PLM" << endl;
    }
    else if (normalPredOrder == 2)
    {
      pout() << "PPM" << endl;
    }
    else
    {
      pout() << "Unknown (" << normalPredOrder << ")" << endl;
    }

    pout() << "slope_order = "
           << (useFourthOrderSlopes ? "4th" : "2nd") << endl;
    pout() << "use_primitive_slope_limiting = "
           << (usePrimLimiting ? "yes" : "no") << endl;
    pout() << "use_characteristic_slope_limiting = "
           << (useCharLimiting ? "yes" : "no") << endl;
    pout() << "initial_average = "
           << (initialAverage ? "yes" : "no") << endl;
    pout() << "use_slope_flattening = "
           << (useFlattening ? "yes" : "no") << endl;

    pout() << "use_artificial_viscosity = "
           << (useArtificialViscosity ? "yes" : "no") << endl;
    if (useArtificialViscosity)
    {
      pout() << "artificial_viscosity = " << artificialViscosity << endl;
    }

    pout() << "use_source_term = "
           << (useSourceTerm ? "yes" : "no") << endl;
    if (useSourceTerm)
    {
      pout() << "source_term_scaling = " << sourceTermScaling << endl;
    }

    pout() << "checkpoint_interval = " << checkpointInterval << endl;
    pout() << "plot_interval = " << plotInterval << endl;

    pout() << "CFL = " << cfl << endl;
    pout() << "initial_CFL = " << initialCFL << endl;

    pout() << "maximum_dt_growth = " << maxDtGrowth << endl;
    pout() << "dt_tolerance_factor = " << dtToleranceFactor << endl;
  }

  int spaceOrder = (useFourthOrderSlopes) ? 4 : 2;

  AMRLevelMappedTaggingStrategy* taggingStrategy =
    new SWMappedTaggingStrategy(refineThresh, refinementIsScaled);

  // Set up the AMRLevelFactory.
  AMRLevelMappedConsFactory amrFact(coordSysFact, stabilityStrategy, taggingStrategy, plotPrefix);
  amrFact.spaceOrder(spaceOrder);
  amrFact.limitFaceValues(usePrimLimiting); // WAS limitFaceValues
  amrFact.highOrderLimiter(highOrderLimiter); // WAS limitFaceValues
  amrFact.initialAverage(initialAverage);
  amrFact.useFlattening(useFlattening);
  amrFact.noPPM(noPPM);
  amrFact.doDeconvolution(doDeconvolution);
  amrFact.doFaceDeconvolution(doFaceDeconvolution);
  amrFact.useArtificialViscosity(useArtificialViscosity);
  amrFact.artificialViscosity(artificialViscosity);
  amrFact.useArtVisc(useArtVisc);
  amrFact.ratioArtVisc(ratioArtVisc);
  amrFact.forwardEuler(forwardEuler);
  amrFact.CFL(cfl);
  amrFact.domainLength(domainLength);
  amrFact.refinementThreshold(refineThresh);
  amrFact.refinementIsScaled(refinementIsScaled);
  amrFact.tagBufferSize(tagBufferSize);
  amrFact.verbosity(verbosity);
  amrFact.initialDtMultiplier(initialCFL);
  amrFact.molPhysics(molPhysics);
  amrFact.useSourceTerm(useSourceTerm);
  amrFact.sourceTerm(sourceTerm);

  { // scope of AMR amr
    AMR amr;

    // Set up the AMR object
    amr.define(maxLevel, refRatios, probDomain, &amrFact);

    if (fixedDt > 0)
    {
      amr.fixedDt(fixedDt);
    }

    // Set grid generation parameters
    amr.maxGridSize(maxGridSize);
    amr.blockFactor(blockFactor);
    amr.fillRatio(fillRatio);

    amr.gridBufferSize(gridBufferSize);

    // Set up periodic output.
    RefCountedPtr<Scheduler> scheduler(new Scheduler());
    if (plotInterval > 0)
    {
      RefCountedPtr<PlotterPeriodicFunction> plotter(new PlotterPeriodicFunction(plotPrefix));
      scheduler->schedule(plotter, plotInterval);
    }

    if (checkpointInterval > 0)
    {
      string chkPrefix;
      parser.get("chk_prefix", chkPrefix);
      RefCountedPtr<CheckpointPeriodicFunction> dumper(new CheckpointPeriodicFunction(chkPrefix));
      scheduler->schedule(dumper, checkpointInterval);
    }
    amr.schedule(scheduler);

    amr.regridIntervals(regridIntervals);
    amr.maxDtGrow(maxDtGrowth);
    amr.dtToleranceFactor(dtToleranceFactor);
    amr.useSubcyclingInTime(useSubcycling);

    // Set up output files
    if (!plotPrefix.empty())
    {
      amr.plotPrefix(plotPrefix);
    }

    if (!chkPrefix.empty())
    {
      amr.checkpointPrefix(chkPrefix);
    }

    amr.verbosity(verbosity);

    // Set up input files
    if (!parser.contains("restart_file"))
    {
      if (!parser.contains("fixed_hierarchy"))
      {
        // initialize from scratch for AMR run
        // initialize hierarchy of levels
        amr.setupForNewAMRRun();
      }
      else
      {
        //      string gridFile;
        //      parser.query("fixed_hierarchy",gridFile);

        // initialize from a list of grids in "gridFile"
        int numLevels = maxLevel+1;
        Vector<Vector<Box> > amrGrids(numLevels);
        //       setupFixedGrids(amrGrids,
        //                       probDomain,
        //                       maxLevel,
        //                       maxGridSize,
        //                       blockFactor,
        //                       verbosity,
        //                       gridFile);
        GenFuncs::readBoxes(amrGrids, parser, probDomain,
            maxGridSize, blockFactor,
            numLevels, refRatios, verbosity);
        amr.setupForFixedHierarchyRun(amrGrids,1);
      }
    }
    else // read from restart file
    {
      string restartFile;
      parser.query("restart_file",restartFile);

#ifdef CH_USE_HDF5
      HDF5Handle handle(restartFile,HDF5Handle::OPEN_RDONLY);
      // read from checkpoint file
      amr.setupForRestart(handle);
      handle.close();
#else
      MayDay::Error("amrRun restart only defined with hdf5");
#endif
      // This section added by petermc, 20 Jan 2010
      if (parser.contains("fixed_hierarchy"))
      {
        int numLevels = maxLevel+1;
        Vector<int> regridInterval(numLevels);
        for (int ilev = 0; ilev < numLevels; ilev++) regridInterval[ilev] = 0;
        amr.regridIntervals(regridInterval);
      }
    }

    // End timing AMR solver setup
    TimeSetupAMR.stop();

#ifndef CH_NTIMER
    pout() << "AMR Setup completed ---- "
      << "mem: "
      << setw(8) << setprecision(3)
      << setiosflags(ios::fixed)
      << get_memory_usage_from_OS()
      << " MB, time: "
      << setw(8) << setprecision(3)
      << setiosflags(ios::fixed)
      << TimeSetupAMR.wc_time()
      << " sec (wall-clock)" << endl;

    if (verbosity >= 1)
    {
      pout() << endl;
    }
#endif

    // Run and time the computation
    TimeRun.start();
    amr.run(stopTime,nstop);
    TimeRun.stop();

#ifndef CH_NTIMER
    if (verbosity >= 1)
    {
      pout() << endl;
    }

    pout() << "AMR Run completed ------ "
      << "mem: "
      << setw(8) << setprecision(3)
      << setiosflags(ios::fixed)
      << get_memory_usage_from_OS()
      << " MB, time: "
      << setw(8) << setprecision(3)
      << setiosflags(ios::fixed)
      << TimeRun.wc_time()
      << " sec (wall-clock)" << endl;
#endif

    // Output the last plot file and statistics - time the process
    TimeConclude.start();
    amr.conclude();
    TimeConclude.stop();

#ifndef CH_NTIMER
    pout() << "AMR Conclude completed - "
      << "mem: "
      << setw(8) << setprecision(3)
      << setiosflags(ios::fixed)
      << get_memory_usage_from_OS()
      << " MB, time: "
      << setw(8) << setprecision(3)
      << setiosflags(ios::fixed)
      << TimeConclude.wc_time()
      << " sec (wall-clock)" << endl;
#endif
  } // scope of amr
}
//-----------------------------------------------------------------------

// setupFixedGrids allows fixed grids to be read in and used in this AMR
// computation example
//-----------------------------------------------------------------------
void
setupFixedGrids(Vector<Vector<Box> >& a_amrGrids,
                const ProblemDomain&  a_domain,
                int                   a_maxLevel,
                int                   a_maxGridSize,
                int                   a_blockFactor,
                int                   a_verbosity,
                string           a_gridFile)
{
  // Run this task on one processor
  if (procID() == uniqueProc(SerialTask::compute))
  {
    a_amrGrids.push_back(Vector<Box>(1,a_domain.domainBox()));

    // Read in predefined grids
    ifstream is(a_gridFile.c_str(), ios::in);

    if (is.fail())
    {
      MayDay::Error("Cannot open grids file");
    }

    // Format of file:
    //   number of levels, then for each level (starting with level 1):
    //   number of grids on level, list of boxes

    int inNumLevels;
    is >> inNumLevels;

    CH_assert (inNumLevels <= a_maxLevel+1);

    if (a_verbosity >= 3)
    {
      pout() << "numLevels = " << inNumLevels << endl;
    }

    while (is.get() != '\n');

    a_amrGrids.resize(inNumLevels);

    // Check to see if coarsest level needs to be broken up
    domainSplit(a_domain,a_amrGrids[0],a_maxGridSize,a_blockFactor);

    if (a_verbosity >= 3)
    {
      pout() << "level 0: ";
      for (int n = 0; n < a_amrGrids[0].size(); n++)
      {
        pout() << a_amrGrids[0][0] << endl;
      }
    }

    // Now loop over levels, starting with level 1
    int ngrid;
    for (int lev = 1; lev < inNumLevels; lev++)
    {
      is >> ngrid;

      if (a_verbosity >= 3)
      {
        pout() << "level " << lev << " numGrids = " << ngrid << endl;
        pout() << "Grids: ";
      }

      while (is.get() != '\n');

      a_amrGrids[lev].resize(ngrid);

      for (int i = 0; i < ngrid; i++)
      {
        Box bx;
        is >> bx;

        while (is.get() != '\n');

        // Quick check on box size
        Box bxRef(bx);

        if (bxRef.longside() > a_maxGridSize)
        {
          pout() << "Grid " << bx << " too large" << endl;
          MayDay::Error();
        }

        if (a_verbosity >= 3)
        {
          pout() << bx << endl;
        }

        a_amrGrids[lev][i] = bx;
      } // End loop over boxes on this level
    } // End loop over levels
  }

  // Broadcast results to all the processors
  broadcast(a_amrGrids,uniqueProc(SerialTask::compute));
}
//-----------------------------------------------------------------------

#ifdef TRAP_FPE
#include <fenv.h>

// FE_INEXACT    inexact result
// FE_DIVBYZERO  division by zero
// FE_UNDERFLOW  result not representable due to underflow
// FE_OVERFLOW   result not representable due to overflow
// FE_INVALID    invalid operation

//-----------------------------------------------------------------------
static void enableFpExceptions ()
{
  if (feclearexcept(FE_ALL_EXCEPT) != 0)
  {
    MayDay::Abort("feclearexcept failed");
  }

  int flags = FE_DIVBYZERO |
              FE_INVALID   |
//              FE_UNDERFLOW |
              FE_OVERFLOW  ;

  if (feenableexcept(flags) == -1)
  {
    MayDay::Abort("feenableexcept failed");
  }
}
//-----------------------------------------------------------------------
#endif