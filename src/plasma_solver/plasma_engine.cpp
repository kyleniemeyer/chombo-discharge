/*!
  @file plasma_engine.cpp
  @brief Implementation of plasma_engine.H
  @author Robert Marskar
  @date Nov. 2017
*/

#include "plasma_engine.H"

#include "LevelData.H"
#include "EBCellFAB.H"
#include "EBAMRIO.H"
#include "EBAMRDataOps.H"

plasma_engine::plasma_engine(){
  CH_TIME("plasma_engine::plasma_engine(weak)");
  if(m_verbosity > 5){
    pout() << "plasma_engine::plasma_engine(weak)" << endl;
  }
}

plasma_engine::plasma_engine(const RefCountedPtr<physical_domain>&        a_physdom,
			     const RefCountedPtr<computational_geometry>& a_compgeom,
			     const RefCountedPtr<plasma_kinetics>&        a_plaskin,
			     const RefCountedPtr<time_stepper>&           a_timestepper,
			     const RefCountedPtr<amr_mesh>&               a_amr,
			     const RefCountedPtr<cell_tagger>&            a_celltagger){
  CH_TIME("plasma_engine::plasma_engine(full)");
  if(m_verbosity > 5){
    pout() << "plasma_engine::plasma_engine(full)" << endl;
  }

  this->set_physical_domain(a_physdom);         // Set physical domain
  this->set_computational_geometry(a_compgeom); // Set computational geometry
  this->set_plasma_kinetics(a_plaskin);         // Set plasma kinetics
  this->set_time_stepper(a_timestepper);        // Set time stepper
  this->set_amr(a_amr);                         // Set amr
  this->set_cell_tagger(a_celltagger);          // Set cell tagger

  m_amr->sanity_check();  // Sanity check, make sure everything is set up correctly
  m_amr->build_domains(); // Build domains and resolutions, nothing else

  if(!m_celltagger.isNull()){ 
    m_celltagger->define(m_plaskin, m_timestepper, m_amr, m_compgeom, m_physdom);
  }


}

plasma_engine::~plasma_engine(){
  CH_TIME("plasma_engine::~plasma_engine");
}



void plasma_engine::set_verbosity(const int a_verbosity){
  CH_TIME("plasma_engine::set_verbosity");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_verbosity" << endl;
  }
  m_verbosity = a_verbosity;
}

void plasma_engine::set_computational_geometry(const RefCountedPtr<computational_geometry>& a_compgeom){
  CH_TIME("plasma_engine::set_computational_geometry");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_computational_geometry" << endl;
  }
  m_compgeom = a_compgeom;
  m_mfis     = a_compgeom->get_mfis();
}

void plasma_engine::set_plasma_kinetics(const RefCountedPtr<plasma_kinetics>& a_plaskin){
  CH_TIME("plasma_engine::set_plasma_kinetics");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_plasma_kinetics" << endl;
  }
  m_plaskin = a_plaskin;
}

void plasma_engine::set_time_stepper(const RefCountedPtr<time_stepper>& a_timestepper){
  CH_TIME("plasma_engine::set_time_stepper");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_time_stepper" << endl;
  }
  m_timestepper = a_timestepper;
}

void plasma_engine::set_cell_tagger(const RefCountedPtr<cell_tagger>& a_celltagger){
  CH_TIME("plasma_engine::set_cell_tagger");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_cell_tagger" << endl;
  }
  m_celltagger = a_celltagger;
}

void plasma_engine::set_geom_refinement_depth(const int a_depth){
  CH_TIME("plasma_engine::set_geom_refinement_depth");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_geom_refinement_depth" << endl;
  }

  const int max_depth = m_amr->get_max_amr_depth();
  
  m_geom_tag_depth = (a_depth == -1) ? max_depth : a_depth;
  m_geom_tag_depth = Min(m_geom_tag_depth, max_depth); 
}

void plasma_engine::set_amr(const RefCountedPtr<amr_mesh>& a_amr){
  CH_TIME("plasma_engine::set_amr");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_amr" << endl;
  }

  m_amr = a_amr;
  m_amr->set_mfis(m_compgeom->get_mfis());
}

void plasma_engine::setup_fresh(){
  CH_TIME("plasma_engine::setup_fresh");
  if(m_verbosity > 5){
    pout() << "plasma_engine::setup_fresh" << endl;
  }

  this->sanity_check();                                    // Sanity check before doing anything expensive
  
  m_compgeom->build_geometries(*m_physdom,                 // Build the multifluid geometries
			       m_amr->get_finest_domain(),
			       m_amr->get_finest_dx(),
			       m_amr->get_max_box_size());

  this->get_geom_tags();       // Get geometric tags. 

  if(!m_timestepper.isNull()){ // Remove this later.
    pout() << "plasma_engine::setup_fresh - remove this stuff when timestepper is finally instantiated" << endl;
    m_timestepper->instantiate_solvers();
    m_amr->set_num_ghost(m_timestepper->query_ghost()); // Query solvers for ghost cells. Give it to amr_mesh before grid gen. 
  }
  else {
    m_amr->set_num_ghost(3);
  }

  m_amr->regrid(m_geom_tags, m_geom_tag_depth); // Regrid using geometric tags for now


  // // Write a plot file
  // const int finest = m_amr->get_finest_level();
  // const Vector<EBISLayout>& ebisl = m_amr->get_ebisl(phase::gas);
  // const Vector<DisjointBoxLayout>& grids = m_amr->get_grids();
  // const Vector<ProblemDomain>& domains = m_amr->get_domains();
  // const Vector<Real>& dx = m_amr->get_dx();
  // EBAMRCellData data;
  // m_amr->allocate(data, phase::gas, 1);
  // m_amr->average_down(data, phase::gas);
  // m_amr->interp_ghost(data, phase::gas);

  // // Apply a centroid-interpolation stencil
  // //  irreg_amr_stencil<centroid_interp>& stencils = m_amr->get_centroid_interp_stencils(phase::gas);
  // irreg_amr_stencil<eb_centroid_interp>& stencils = m_amr->get_eb_centroid_interp_stencils(phase::gas);
  // stencils.apply(data);

  // Vector<std::string> names(1);
  // Vector<Real> coveredVals;

  // Vector<LevelData<EBCellFAB>* > data_write(data.size());
  // for (int lvl = 0; lvl <= finest; lvl++){
  //   data_write[lvl] = &(*data[lvl]);
  // }
  // names[0] = "data";
  // writeEBHDF5("test.hdf5",
  // 	      grids,
  // 	      data_write,
  // 	      names,
  // 	      domains[0],
  // 	      dx[0],
  // 	      0.0,
  // 	      0.0,
  // 	      m_amr->get_ref_rat(),
  // 	      1 + finest,
  // 	      false,
  // 	      coveredVals);
	      
      
}

void plasma_engine::setup_for_restart(const std::string a_restart_file){
  CH_TIME("plasma_engine::setup_for_restart");
  if(m_verbosity > 5){
    pout() << "plasma_engine::setup_for_restart" << endl;
  }
}

void plasma_engine::initial_regrids(const int a_init_regrids){
  CH_TIME("plasma_engine::initia_regrids");
  if(m_verbosity > 5){
    pout() << "plasma_engine::initial_regrids" << endl;
  }
}



void plasma_engine::set_physical_domain(const RefCountedPtr<physical_domain>& a_physdom){
  CH_TIME("plasma_engine::set_physical_domain");
  if(m_verbosity > 5){
    pout() << "plasma_engine::set_physical_domain" << endl;
  }
  m_physdom = a_physdom;
}

void plasma_engine::sanity_check(){
  CH_TIME("plasma_engine::sanity_check");
  if(m_verbosity > 4){
    pout() << "plasma_engine::sanity_check" << endl;
  }
}

void plasma_engine::regrid(){
  CH_TIME("plasma_engine::regrid");
  if(m_verbosity > 2){
    pout() << "plasma_engine::regrid" << endl;
  }
}

void plasma_engine::write_plot_file(){
  CH_TIME("plasma_engine::write_plot_file");
  if(m_verbosity > 3){
    pout() << "plasma_engine::write_plot_file" << endl;
  }
}

void plasma_engine::write_checkpoint_file(){
  CH_TIME("plasma_engine::write_checkpoint_file");
  if(m_verbosity > 3){
    pout() << "plasma_engine::write_checkpoint_file" << endl;
  }
}

void plasma_engine::read_checkpoint_file(){
  CH_TIME("plasma_engine::read_checkpoint_file");
  if(m_verbosity > 3){
    pout() << "plasma_engine::read_checkpoint_file" << endl;
  }  
}

void plasma_engine::get_geom_tags(){
  CH_TIME("plasma_engine::get_geom_tags");
  if(m_verbosity > 5){
    pout() << "plasma_engine::get_geom_tags" << endl;
  }

  const int maxdepth = m_amr->get_max_amr_depth();

  m_geom_tags.resize(maxdepth);

  const RefCountedPtr<EBIndexSpace> ebis_gas = m_mfis->get_ebis(phase::gas);
  const RefCountedPtr<EBIndexSpace> ebis_sol = m_mfis->get_ebis(phase::solid);

  CH_assert(ebis_sol != NULL);
  CH_assert(ebis_gas != NULL);

  for (int lvl = 0; lvl < maxdepth; lvl++){ // Don't need tags on maxdepth, we will never generate grids below that. 
    const ProblemDomain& cur_dom = m_amr->get_domains()[lvl];
    const int which_level = ebis_gas->getLevel(cur_dom);

    m_geom_tags[lvl].makeEmpty();
    m_geom_tags[lvl] |= ebis_gas->irregCells(which_level);

    if(!ebis_sol.isNull()){
      m_geom_tags[lvl] |= ebis_sol->irregCells(which_level);
    }
  }

  // Grow tags by 2, this is an ad-hoc fix that prevents ugly grid near EBs
  for (int lvl = 0; lvl < maxdepth; lvl++){
    m_geom_tags[lvl].grow(1);
  }
}
