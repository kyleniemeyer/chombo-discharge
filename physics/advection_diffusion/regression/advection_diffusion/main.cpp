#include "driver.H"
#include "cdr_gdnv.H"
#include "rod_sphere.H"
#include "advection_diffusion_stepper.H"
#include "ParmParse.H"

using namespace physics::advection_diffusion;

int main(int argc, char* argv[]){

#ifdef CH_MPI
  MPI_Init(&argc, &argv);
#endif

  // Build class options from input script and command line options
  char* input_file = argv[1];
  ParmParse pp(argc-2, argv+2, NULL, input_file);

  // Set geometry and AMR 
  RefCountedPtr<computational_geometry> compgeom = RefCountedPtr<computational_geometry> (new rod_sphere());
  RefCountedPtr<amr_mesh> amr                    = RefCountedPtr<amr_mesh> (new amr_mesh());
  RefCountedPtr<geo_coarsener> geocoarsen        = RefCountedPtr<geo_coarsener> (new geo_coarsener());
  RefCountedPtr<cell_tagger> tagger              = RefCountedPtr<cell_tagger> (NULL);

  // Set up basic advection_diffusion 
  RefCountedPtr<cdr_solver> = RefCountedPtr<cdr_solver> (new cdr_gdnv());
  auto timestepper = RefCountedPtr<advection_diffusion_stepper<cdr_gdnv> >
     (new advection_diffusion_stepper<cdr_gdnv>());

  // Set up the driver and run it
  RefCountedPtr<driver> engine = RefCountedPtr<driver> (new driver(compgeom, timestepper, amr, tagger, geocoarsen));
  engine->setup_and_run();

#ifdef CH_MPI
  CH_TIMER_REPORT();
  MPI_Finalize();
#endif
}
