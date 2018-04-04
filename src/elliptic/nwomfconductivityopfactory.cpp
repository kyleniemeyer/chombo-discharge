/*!
  @file nwomfconductivityopfactory.cpp
  @brief Implementation of nwomfconductivityopfactory.H
  @author Robert Marskar
  @date Dec. 2017
  @todo Boundary condition order and bottom drop should be constructor arguments
*/

#include "nwomfconductivityopfactory.H"

#define verb 0

nwomfconductivityopfactory::nwomfconductivityopfactory(const RefCountedPtr<mfis>&                a_mfis,
						       const Vector<MFLevelGrid>&                a_mflg,
						       const Vector<NWOMFQuadCFInterp>&          a_mfquadcfi,
						       const Vector<int>&                        a_ref_rat,
						       const Vector<DisjointBoxLayout>&          a_grids,
						       const MFAMRCellData&                      a_aco,
						       const MFAMRFluxData&                      a_bco,
						       const MFAMRIVData&                        a_bco_irreg,
						       const Real&                               a_alpha,
						       const Real&                               a_beta,
						       const Real&                               a_coarsest_dx,
						       const ProblemDomain&                      a_coarsest_domain,
						       const RefCountedPtr<BaseDomainBCFactory>& a_dombc,
						       const RealVect&                           a_origin,
						       const IntVect&                            a_ghost_phi,
						       const IntVect&                            a_ghost_rhs,
						       const int                                 a_order_ebbc,
						       const int                                 a_drop_bottom,
						       int                                       a_num_levels){
#if verb
  pout() << "nwomfconductivityopfactory::nwomfconductivityopfactory" << endl;
#endif

  CH_assert(a_mflg[0].num_phases() <= 2); 

  m_num_levels = (a_num_levels > 0) ? a_num_levels : a_grids.size();
  m_mfis       = a_mfis;
  m_mflg       = a_mflg;
  m_mfquadcfi  = a_mfquadcfi;
  m_aco        = a_aco;
  m_bco        = a_bco;
  m_bco_irreg  = a_bco_irreg;
  m_alpha      = a_alpha;
  m_beta       = a_beta;
  m_dombc      = a_dombc;
  m_ref_rat    = a_ref_rat;
  m_grids      = a_grids;
  m_ghost_phi  = a_ghost_phi;
  m_ghost_rhs  = a_ghost_rhs;
  m_origin     = a_origin;
  m_ebbc_order = a_order_ebbc;

    
  m_domains.resize(m_num_levels);
  m_dx.resize(m_num_levels);
    
  m_dx[0]      = a_coarsest_dx;
  m_domains[0] = a_coarsest_domain;
    
  for (int lvl = 1; lvl < m_num_levels; lvl++){
    m_dx[lvl]      = m_dx[lvl-1]/m_ref_rat[lvl-1];


    m_domains[lvl] = m_domains[lvl-1];
    m_domains[lvl].refine(m_ref_rat[lvl-1]);
  }

  const int botdrop  = (a_drop_bottom == -1) ? a_coarsest_domain.size()[0]/4 : a_drop_bottom;

  this->set_ebbc_order(m_ebbc_order); // Default is second order BCs
  this->set_relax_type(2);            // Default relaxation type
  this->set_bottom_drop(botdrop);     // Default bottom drop
  this->set_max_box_size(32);         // Default max box size
  this->define_jump_stuff();          // Define jump cell stuff
  this->define_multigrid_stuff();     // Define things for lower levels of multigrid. Must happen after define_jump_stuff
#if 0
  this->set_jump(0.0, 1.0);           // Default, no surface charge.
#endif


#if verb // Debugging hook
  for (int lvl = 0; lvl < m_num_levels; lvl++){
    pout() << "lvl = "  << lvl
	   << "\t has MG objects = " << m_has_mg_objects[lvl]
	   << "\t MG levels = " << m_grids_mg[lvl].size()
	   << endl;
  }
#endif

}

nwomfconductivityopfactory::~nwomfconductivityopfactory(){

}

void nwomfconductivityopfactory::define_multigrid_stuff(){
  CH_TIME("nwomfconductivityopfactory::define_multigrid_stuff");
  m_aco_mg.resize(m_num_levels);
  m_bco_mg.resize(m_num_levels);
  m_bco_irreg_mg.resize(m_num_levels);
  m_mflg_mg.resize(m_num_levels);
  m_grids_mg.resize(m_num_levels);
  m_aveop_mg.resize(m_num_levels);
  m_domains_mg.resize(m_num_levels);
  m_has_mg_objects.resize(m_num_levels, false);
  m_layout_changed.resize(m_num_levels);
  m_layout_changed_mg.resize(m_num_levels);
  m_aveop_mg.resize(m_num_levels);
  m_jump_mg.resize(m_num_levels);

  for (int lvl = 0; lvl < m_num_levels; lvl++){
    if(lvl == 0 || m_ref_rat[lvl] > 2) { // Must be able to generate MultiGrid objects for bottom level and if ref > 2
	
      m_has_mg_objects[lvl] = true;

      const int mg_refi = 2;             // MultiGrid uses VCycling, refinement of 2. 

      m_aco_mg[lvl].resize(0);            // aco for all MG levels at this level
      m_bco_mg[lvl].resize(0);            // bco for all MG levels at this level
      m_bco_irreg_mg[lvl].resize(0);      // bco for all MG levels at this level
      m_mflg_mg[lvl].resize(0);           // MFLevelGrids for all MG levels at this level
      m_grids_mg[lvl].resize(0);          // Grids for all MG levels at this level
      m_domains_mg[lvl].resize(0);        // Domains for all MG levels at this level
      m_layout_changed_mg[lvl].resize(0); // Layout changed for all MG levels at this level
      m_aveop_mg[lvl].resize(0);          //
      m_jump_mg[lvl].resize(0);          // sigma for all MG levels at this AMR level

      m_aco_mg[lvl].push_back(m_aco[lvl]);                         // MG depth 0 is an AMR level
      m_bco_mg[lvl].push_back(m_bco[lvl]);                         //  
      m_bco_irreg_mg[lvl].push_back(m_bco_irreg[lvl]);             // 
      m_mflg_mg[lvl].push_back(m_mflg[lvl]);                       // 
      m_grids_mg[lvl].push_back(m_grids[lvl]);                     // 
      m_domains_mg[lvl].push_back(m_domains[lvl]);                 // 
      m_layout_changed_mg[lvl].push_back(m_layout_changed[lvl]);   //
      m_aveop_mg[lvl].push_back(m_aveop[lvl]);                     // This is null for lvl = 0, but not otherwise
      m_jump_mg[lvl].push_back(m_jump[lvl]);                       //

      bool has_coarser = true;
      bool at_amr_lvl  = true;
      ProblemDomain cur_domain = m_domains[lvl];

      has_coarser = m_test_ref < m_max_box_size;

      if(!has_coarser){
	m_has_mg_objects[lvl] = false;
      }
      while(has_coarser){ 

	int imgsize = m_grids_mg[lvl].size();
	const DisjointBoxLayout& fine_grid = m_grids_mg[lvl][imgsize - 1];   // Finer grid for current MG level
	const MFLevelGrid& mflg_fine       = m_mflg_mg[lvl][imgsize - 1];    // Finer MFLevelGrid for current MG level
	const ProblemDomain& domain_fine   = m_domains_mg[lvl][imgsize - 1]; // Finer domain for current MG level

	DisjointBoxLayout grid_coar_mg;
	ProblemDomain domain_coar_mg;

	bool layout_changed;

	// Check if we have coarser stuff
	has_coarser = EBArith::getCoarserLayouts(grid_coar_mg,   // Coarsened grid
						 domain_coar_mg, // Coarsened domain  
						 fine_grid,      // Fine/current level
						 domain_fine,    // Fine/current domain
						 mg_refi,        // Refinement factor
						 m_max_box_size, // 
						 layout_changed, //
						 m_test_ref);    //


	// For some reason getCoarserLayouts doesn't trigger correctly - I don't know what's wrong... :)
	// Here is a (bad) solution
#if 1
	if(has_coarser){
	  if(fine_grid.coarsenable(2*mg_refi)){
	    has_coarser = true;
	  }
	  else{
	    has_coarser = false;
	  }
	}
#endif

#if verb
	pout() << endl;
	pout() << "===================" << endl;
	pout() << "fine domain = " << domain_fine << endl;
	int testref = m_test_ref*mg_refi;
	ProblemDomain schme = coarsen(domain_fine, testref);
	schme.refine(testref);
	if(schme != domain_fine){
	  pout() << "test 1 false" << endl;
	}
	else{
	  pout() << "test 1 true" << endl;
	}


	pout() << "layout changed = " << layout_changed << endl;
	
	pout() << "fine grid = " << endl << fine_grid << endl;
	pout() << "coar grid = " << endl << grid_coar_mg << endl;
	pout() << "has coar = " << has_coarser << endl;
	pout() << "===================" << endl;
	pout() << endl;
#endif

	if(at_amr_lvl && !has_coarser){
	  m_has_mg_objects[lvl] = false;
	}

	if(at_amr_lvl){
	  m_layout_changed[lvl] = layout_changed;
	  at_amr_lvl = false;
	}

	if(has_coarser){

	  m_grids_mg[lvl].push_back(grid_coar_mg);
	  m_domains_mg[lvl].push_back(domain_coar_mg);
	  m_layout_changed_mg[lvl].push_back(layout_changed);
	  cur_domain.coarsen(mg_refi);

	  const int  ncomps = 1; // Number of components we solve for. Always 1. 
	  const int ebghost = 4; // Ghost cells for MG, using 4 since that allows refinement of 4
	  const int   ghost = 1; // Ghost cells

	  m_mflg_mg[lvl].push_back(MFLevelGrid(grid_coar_mg, domain_coar_mg, ebghost, m_mfis));

	  const int img = m_mflg_mg[lvl].size() - 1; // Last one added, i.e. the coarsest that we have so far

	  const MFLevelGrid& mflg_coar = m_mflg_mg[lvl][img  ];    // Coarsened EBLevelGrids for all phases
	  const MFLevelGrid& mflg_fine = m_mflg_mg[lvl][img-1];    // Fine EBLevelGrids for all phases

	  //	    Vector<EBISLayout> ebisl_coar = mflg_coar.get_ebisl();  // Need this stuff for factories. 
	  Vector<int> comps;
	  Vector<EBISLayout> ebisl_coar;
	  for (int i = 0; i < mflg_coar.num_phases(); i++){
	    const EBLevelGrid& eblg = mflg_coar.get_eblg(i);
	    const EBISLayout& ebisl = eblg.getEBISL();
	    ebisl_coar.push_back(ebisl);
	    comps.push_back(ncomps);
	  }


	  // Averaging operator for m_jump_mg
	  const int ncomp      = 1;
	  const int main_phase = 0;
	  const EBLevelGrid& eblg_fine = mflg_fine.get_eblg(main_phase);
	  const EBLevelGrid& eblg_coar = mflg_coar.get_eblg(main_phase);
	  RefCountedPtr<ebcoarseaverage> aveop(new ebcoarseaverage(eblg_fine.getDBL(),    eblg_coar.getDBL(),
								   eblg_fine.getEBISL(),  eblg_coar.getEBISL(),
								   eblg_coar.getDomain(), mg_refi, ncomp,
								   eblg_coar.getEBIS()));

#if verb
	  //	  pout() << "AMR level = lvl " << "\tMG level = " << "\t coar domain = " << eblg_coar.getDomain() << endl;
#endif
	  // Interface cells on MG level img. 
	  LayoutData<IntVectSet> isect_cells (eblg_coar.getDBL());
	  for (DataIterator dit = isect_cells.dataIterator(); dit.ok(); ++dit){
	    isect_cells[dit()] = mflg_coar.interface_region(grid_coar_mg[dit()], dit());
	  }

	  MFCellFactory      cellfact(ebisl_coar, comps);
	  MFFluxFactory      fluxfact(ebisl_coar, comps);
	  MFBaseIVFABFactory ivfact  (ebisl_coar, comps);
	  BaseIVFactory<Real> fact   (eblg_coar.getEBISL(), isect_cells);

	  RefCountedPtr<LevelData<BaseIVFAB<Real> > > jump_coar = RefCountedPtr<LevelData<BaseIVFAB<Real> > >
	    (new LevelData<BaseIVFAB<Real> > (grid_coar_mg, ncomps, ghost*IntVect::Unit, fact)); 
	  RefCountedPtr<LevelData<MFCellFAB> > aco_coar = RefCountedPtr<LevelData<MFCellFAB> >
	    (new LevelData<MFCellFAB>(grid_coar_mg, ncomps, ghost*IntVect::Unit, cellfact));
	  RefCountedPtr<LevelData<MFFluxFAB> > bco_coar = RefCountedPtr<LevelData<MFFluxFAB> >
	    (new LevelData<MFFluxFAB>(grid_coar_mg, ncomps, ghost*IntVect::Unit, fluxfact));
	  RefCountedPtr<LevelData<MFBaseIVFAB> > bco_irreg_coar = RefCountedPtr<LevelData<MFBaseIVFAB> >
	    (new LevelData<MFBaseIVFAB>(grid_coar_mg, ncomps, ghost*IntVect::Unit, ivfact));
	    

	  this->coarsen_coefficients(*aco_coar, // Coarsen coefficients
				     *bco_coar,
				     *bco_irreg_coar,
				     mflg_coar,
				     mflg_fine,
				     *m_aco_mg[lvl][img-1],
				     *m_bco_mg[lvl][img-1],
				     *m_bco_irreg_mg[lvl][img-1],
				     mg_refi);

	  m_aco_mg[lvl].push_back(aco_coar);
	  m_bco_mg[lvl].push_back(bco_coar);
	  m_bco_irreg_mg[lvl].push_back(bco_irreg_coar);
	  m_aveop_mg[lvl].push_back(aveop);
	  m_jump_mg[lvl].push_back(jump_coar);
	}
      }
    }
    else {
      m_has_mg_objects[lvl] = false;
    }


  }

}

void nwomfconductivityopfactory::coarsen_coefficients(LevelData<MFCellFAB>&         a_aco_coar,
						      LevelData<MFFluxFAB>&         a_bco_coar,
						      LevelData<MFBaseIVFAB>&       a_bco_irreg_coar,
						      const MFLevelGrid&            a_mflg_coar,
						      const MFLevelGrid&            a_mflg_fine,
						      const LevelData<MFCellFAB>&   a_aco_fine,
						      const LevelData<MFFluxFAB>&   a_bco_fine,
						      const LevelData<MFBaseIVFAB>& a_bco_irreg_fine,
						      const int&                    a_ref_to_depth){
  CH_assert(a_ref_to_depth > 0);
    
  const int ncomp = 1;
  const Interval interv(0,ncomp - 1);

  if(a_ref_to_depth == 1){
    a_aco_fine.copyTo(interv,       a_aco_coar,       interv);
    a_bco_fine.copyTo(interv,       a_bco_coar,       interv);
    a_bco_irreg_fine.copyTo(interv, a_bco_irreg_coar, interv);
  }
  else {
    for (int i = 0; i < a_mflg_coar.num_phases(); i++){
      const EBLevelGrid& eblg_coar = a_mflg_coar.get_eblg(i);
      const EBLevelGrid& eblg_fine = a_mflg_fine.get_eblg(i);
      ebcoarseaverage aveop(eblg_fine.getDBL(),    eblg_coar.getDBL(),
			    eblg_fine.getEBISL(),  eblg_coar.getEBISL(),
			    eblg_coar.getDomain(), a_ref_to_depth, ncomp,
			    eblg_coar.getEBIS());

      LevelData<EBCellFAB>        aco_coar;
      LevelData<EBCellFAB>        aco_fine;
      LevelData<EBFluxFAB>        bco_coar;
      LevelData<EBFluxFAB>        bco_fine;
      LevelData<BaseIVFAB<Real> > bco_irreg_coar;
      LevelData<BaseIVFAB<Real> > bco_irreg_fine;

      mfalias::aliasMF(aco_coar,       i, a_aco_coar);
      mfalias::aliasMF(aco_fine,       i, a_aco_fine);
      mfalias::aliasMF(bco_coar,       i, a_bco_coar); 
      mfalias::aliasMF(bco_fine,       i, a_bco_fine);       
      mfalias::aliasMF(bco_irreg_coar, i, a_bco_irreg_coar); 
      mfalias::aliasMF(bco_irreg_fine, i, a_bco_irreg_fine);
	
      aveop.average(aco_coar,       aco_fine,       interv);
      aveop.average(bco_coar,       bco_fine,       interv);
      aveop.average(bco_irreg_coar, bco_irreg_fine, interv);

      aco_coar.exchange();
      bco_coar.exchange();
      bco_irreg_coar.exchange();
    }
  }
}

void nwomfconductivityopfactory::set_ebbc_order(const int a_ebbc_order){
  m_ebbc_order = a_ebbc_order;
}

void nwomfconductivityopfactory::set_bottom_drop(const int a_bottom_drop){
  m_test_ref = a_bottom_drop;
}

void nwomfconductivityopfactory::set_relax_type(const int a_relax_type){
  m_relax_type = a_relax_type;
}

void nwomfconductivityopfactory::set_max_box_size(const int a_max_box_size){
  m_max_box_size = a_max_box_size;
}

void nwomfconductivityopfactory::reclaim(MGLevelOp<LevelData<EBCellFAB> >* a_reclaim){
  delete a_reclaim;
}

void nwomfconductivityopfactory::AMRreclaim(nwomfconductivityop* a_reclaim){
  delete a_reclaim;
}

void nwomfconductivityopfactory::define_jump_stuff(){
  CH_TIME("nwomfconductivityopfactory::define_jump_cells");

  m_aveop.resize(m_num_levels);
  m_jump.resize(m_num_levels);
  m_jumpcells.resize(m_num_levels);

  const int ghost      = 1; // Using one ghost cell for this
  const int ncomp      = 1; // Only single-component stuff
  const int main_phase = 0; // Interface region is the intersection between gas-side irregular cells and solid phase cells
    
  for (int lvl = 0; lvl < m_num_levels; lvl++){
    const EBLevelGrid& eblg  = m_mflg[lvl].get_eblg(main_phase);
    const EBISLayout& ebisl = eblg.getEBISL();

    m_jumpcells[lvl] = RefCountedPtr<LayoutData<IntVectSet> > (new LayoutData<IntVectSet> (m_grids[lvl]));
    for (DataIterator dit = m_jumpcells[lvl]->dataIterator(); dit.ok(); ++dit){
      Box box = m_grids[lvl][dit()];
      box.grow(ghost);
      box &= m_domains[lvl];
      
      (*m_jumpcells[lvl])[dit()] = m_mflg[lvl].interface_region(box, dit());

      
    }

    BaseIVFactory<Real> fact(ebisl, *m_jumpcells[lvl]);
    m_jump[lvl] = RefCountedPtr<LevelData<BaseIVFAB<Real> > >
      (new LevelData<BaseIVFAB<Real> >(m_grids[lvl], ncomp, ghost*IntVect::Unit, fact));
  }

  for (int lvl = 0; lvl < m_num_levels; lvl++){

    const bool has_coar = lvl > 0;

    if(has_coar){
      const EBLevelGrid& eblg_fine = m_mflg[lvl].get_eblg(main_phase);
      const EBLevelGrid& eblg_coar = m_mflg[lvl-1].get_eblg(main_phase);
      const int ref_ratio          = m_ref_rat[lvl-1];
	
      m_aveop[lvl] = RefCountedPtr<ebcoarseaverage> (new ebcoarseaverage(eblg_fine.getDBL(),    eblg_coar.getDBL(),
									 eblg_fine.getEBISL(),  eblg_coar.getEBISL(),
									 eblg_coar.getDomain(), ref_ratio, ncomp,
									 eblg_coar.getEBIS()));
    }
  }
}

void nwomfconductivityopfactory::average_down_amr(){
  CH_TIME("nwomfconductivityopfactory::average_down_amr");
    
  const int ncomp        = 0;
  const Interval interv  = Interval(0, ncomp -1);
  const int finest_level = m_num_levels - 1;

  for (int lvl = finest_level; lvl > 0; lvl--){ // Average down AMR levels
    m_aveop[lvl]->average(*m_jump[lvl-1], *m_jump[lvl], interv);
#if verb // Debug
    pout() << "nwomfconductivityopfactory::average_down_amr from AMR level = " << lvl << " and onto AMR level = " << lvl - 1 << endl;
#endif
  }
}

void nwomfconductivityopfactory::average_down_mg(){
  CH_TIME("nwomfconductivityopfactory::average_down_mg");

  const int ncomp        = 0;
  const Interval interv  = Interval(0, ncomp -1);
  
  for (int lvl = 0; lvl < m_num_levels; lvl++){ // Average down the MG stuff
    if(m_has_mg_objects[lvl]){
      EBAMRIVData& jump_mg = m_jump_mg[lvl]; // m_jump_mg[lvl][0] is the AMR level, which has already been coarsened

      const int finest_mg_level   = 0;
      const int coarsest_mg_level = jump_mg.size() - 1;

      for (int img = finest_mg_level + 1; img <= coarsest_mg_level; img++){ 
#if verb // DEBUG
	pout() << "nwomfconductivityopfactory::average_down_mg from AMR level = " << lvl
	       << " from MG level = " << img-1
	       << " to   MG level = " << img 
	       << " fine domain = " << m_domains_mg[lvl][img] 
	       << endl;
#endif
	m_aveop_mg[lvl][img]->average(*jump_mg[img-1], *jump_mg[img], interv); // Average down onto level img
#if verb
	pout() << "nwomfconductivityopfactory::average_down_mg - done" << endl;
#endif
      }
    }
  }
#if verb // DEBUG
  pout() << "nwomfconductivityopfactory::average_down_mg - done exit " << endl;
#endif
}

void nwomfconductivityopfactory::set_jump(const Real& a_sigma, const Real& a_scale){
  CH_TIME("nwomfconductivityopfactory::set_jump(scalar)");
  for (int lvl = 0; lvl < m_num_levels; lvl++){
    EBLevelDataOps::setVal(*m_jump[lvl], a_sigma);
    data_ops::scale(*m_jump[lvl], a_scale);
  }
#if verb
  pout() << "nwomfconductivityopfactory::set_jump" << endl;
#endif
  this->average_down_amr();
  this->average_down_mg();
#if verb
  pout() << "nwomfconductivityopfactory::set_jump - done" << endl;
#endif
}

void nwomfconductivityopfactory::set_jump(const EBAMRIVData& a_sigma, const Real& a_scale){
  CH_TIME("nwomfconductivityopfactory::set_jump(data based)");
#if verb
  pout() << "nwomfconductivityopfactory::set_jump(data based)" << endl;
#endif

  // Note: copyTo is a little bit volatile since it drops to linearization functions. Going to copy directly instead.
  for (int lvl = 0; lvl < m_num_levels; lvl++){
    for (DataIterator dit = m_grids[lvl].dataIterator(); dit.ok(); ++dit){
      const Box box = m_grids[lvl].get(dit());
      const Interval interv(0,0);
      (*m_jump[lvl])[dit()].copy(box, interv, box, (*a_sigma[lvl])[dit()], interv);
    }

    data_ops::scale(*m_jump[lvl], a_scale);

    m_jump[lvl]->exchange();
  }

  this->average_down_amr();
  this->average_down_mg();

#if verb
  pout() << "nwomfconductivityopfactory::set_jump(data based) - done" << endl;
#endif
}

void nwomfconductivityopfactory::set_electrodes(const Vector<electrode>&            a_electrodes,
						const RefCountedPtr<BaseBCFuncEval> a_potential){
  CH_TIME("nwomfconductivityopfactory::set_electrodes");
  
  m_electrodes = a_electrodes;
  m_potential  = a_potential;
}

int nwomfconductivityopfactory::refToFiner(const ProblemDomain& a_domain) const{
  int retval = -1;
  bool found = false;

  for (int lvl = 0; lvl < m_domains.size(); lvl++){
    if(m_domains[lvl] == a_domain){
      retval = m_ref_rat[lvl];
      found  = true;
    }
  }
  
  if(!found){
    MayDay::Error("nwomfconductivityopfactory::refToFiner - domain not found in AMR hierarchy");
  }
  
  return retval;
}

MGLevelOp<LevelData<MFCellFAB> >* nwomfconductivityopfactory::MGnewOp(const ProblemDomain& a_domain_fine,
								      int                  a_depth,
								      bool                 a_homo_only){
  CH_TIME("nwomfconductivityopfactory::MGnewOp");
#if verb // Test
  pout() << "nwomfconductivityopfactory::MGnewOp" << endl;
#endif
  int ref    = -1;
  bool found = false;

  for (int lvl = 0; lvl < m_num_levels; lvl++){
    if(a_domain_fine == m_domains[lvl]){
      found = true;
      ref   = lvl;
      break;
    }
  }

  if(!found){
    MayDay::Abort("nwomfconductivityopfactory::MGnewOp - no corresponding starting level to a_domain_fine");
  }
  

  // All this shit must be set.
  RefCountedPtr<LevelData<MFCellFAB> >        aco;
  RefCountedPtr<LevelData<MFFluxFAB> >        bco;
  RefCountedPtr<LevelData<MFBaseIVFAB> >      bco_irreg;
  RefCountedPtr<LevelData<BaseIVFAB<Real> > > jump;

  NWOMFQuadCFInterp quadcfi;
  
  MFLevelGrid mflg_fine;
  MFLevelGrid mflg;
  MFLevelGrid mflg_coar;
  MFLevelGrid mflg_coar_mg;
  
  DisjointBoxLayout dbl;
  DisjointBoxLayout dbl_fine;
  DisjointBoxLayout dbl_coar;
  DisjointBoxLayout dbl_coar_mg;

  ProblemDomain domain;

  bool layout_changed;
  bool has_mg   = false;
  bool has_fine = false;
  bool has_coar = false;

  int bog_ref     = 2;
  int ref_to_fine = bog_ref;
  int ref_to_coar = bog_ref;
  int relax_type  = m_relax_type;
  int ebbc_order  = m_ebbc_order;

  IntVect ghost_phi = m_ghost_phi;
  IntVect ghost_rhs = m_ghost_rhs;

  Real dx;
  Real dx_coar;
  Real alpha = m_alpha;
  Real beta  = m_beta;

  dx_coar = -1.0;
  if(ref > 0){
    dx_coar = m_dx[ref-1];
  }

#if verb
  pout() << "trying to define stuff" << endl;
#endif

  

  if(a_depth == 0){ // this is an AMR level
    aco       = m_aco[ref];
    bco       = m_bco[ref];
    bco_irreg = m_bco_irreg[ref];
    jump      = m_jump[ref];
    quadcfi   = m_mfquadcfi[ref];
    mflg      = m_mflg[ref];
    domain    = m_domains[ref];
    layout_changed = m_layout_changed[ref];

    has_mg = m_has_mg_objects[ref];

    if(has_mg){
      mflg_coar_mg = m_mflg_mg[ref][1];
    }

    dbl = m_grids[ref];
    dx  = m_dx[ref];
  }
  else{ // MG levels
    bool found_mg_level = false;
    
    const int icoar  = pow(2, a_depth); 
    const int num_mg = m_mflg_mg[ref].size();

    const ProblemDomain domain_fine     = m_domains[ref];
    const ProblemDomain domain_mg_level = coarsen(domain_fine, icoar);


    for (int img = 0; img < num_mg; img++){
      if(m_domains_mg[ref][img] == domain_mg_level){

	found_mg_level = true;
	
	mflg = m_mflg_mg[ref][img];
	domain = m_domains_mg[ref][img];

	aco       = m_aco_mg[ref][img];
	bco       = m_bco_mg[ref][img];
	bco_irreg = m_bco_irreg_mg[ref][img];
	jump      = m_jump_mg[ref][img];
	layout_changed = m_layout_changed_mg[ref][img];
	dx = m_dx[ref]*Real(icoar);
	has_mg = img + 1 < num_mg;
	if(has_mg){
	  mflg_coar_mg = m_mflg_mg[ref][img + 1];
	}

	break;
      }
    }

    const bool coarsenable = found_mg_level;
    if(!coarsenable){
#if verb
      pout() << "mgnewop::no MG" << endl;
#endif
      return NULL;
    }
  }

#if verb
  pout() << "creating oper" << endl;
#endif

  nwomfconductivityop* oper = new nwomfconductivityop();

  oper->define(m_mfis,           // Set from factory
	       m_dombc,          // Set from factory
	       aco,              // Set to m_aco[ref] (for AMR) or m_aco_mg[ref][img] for MG
	       bco,              // Set to m_bco[ref] (for AMR) or m_bco_mg[ref][img] for MG
	       bco_irreg,        // Set to m_bco_irreg[ref] (for AMR) or m_bco_irreg_mg[ref][img] for MG
	       quadcfi,          // Set to m_mfquadcfi[ref] (for AMR). Undefined for MG. 
	       mflg_fine,        // Undefined. 
	       mflg,             // Set to m_mflg[ref] (for AMR) or m_mflg_mg[ref][img] (for MG)
	       mflg_coar,        // Undefined.
	       mflg_coar_mg,     // Set
	       domain,           // Set to m_domains[ref] (for AMR) or m_domains_mg[ref][img] (for MG)
	       layout_changed,   // Set to m_layout_changed[ref] (for AMR), or m_layout_changed_mg[ref][img] (for MG)
	       has_mg,           // Set to has_mg_objects[ref] (for AMR), or (img + 1) < num_mg (for MG)
	       has_fine,         // Always false
	       has_coar,         // Always false
	       ref_to_fine,      // Set to bogus ref
	       ref_to_coar,      // Set to bogus ref
	       relax_type,       // Set to m_relax_type
	       ebbc_order,       // Set to m_ebbc_order
	       ghost_phi,        // Set to m_ghost_phi
	       ghost_rhs,        // Set to m_ghost_rhs
	       dx,               // Set to m_dx[ref] (for AMR) or m_dx[ref]*icoar (for MG)
	       dx_coar,          // Set to m_dx[ref-1] (for AMR). Undefined for MG
	       alpha,            // Set to m_alpha
	       beta,             // Set to m_beta
	       m_origin);        // Origin

  oper->set_jump(jump);
  oper->set_electrodes(m_electrodes, m_potential);

#if verb
  pout() << "done defining oper" << endl;
#endif
     
  
  

#if 0 // Debug-stop
  MayDay::Abort("nwomfconductivityopfactory::mgnewop - implementation is not finished!");
#endif
#if verb
  pout() << "nwomfconductivityopfactory::MGnewOp - returning new op" << endl;
#endif
  return static_cast<MGLevelOp<LevelData<MFCellFAB> >* > (oper);
}

AMRLevelOp<LevelData<MFCellFAB> >* nwomfconductivityopfactory::AMRnewOp(const ProblemDomain& a_domain_fine){
  CH_TIME("nwomfconductivityopfactory::AMRnewOp");
#if verb
  pout() << "nwomfconductivityopfactory::AMRnewOp" << endl;
#endif
  int ref    = -1;
  bool found = false;

  for (int lvl = 0; lvl < m_num_levels; lvl++){
    if(a_domain_fine == m_domains[lvl]){
      found = true;
      ref   = lvl;
      break;
    }
  }

#if verb
  pout() << "amrnewop:: ref = " << ref << " domain = " << m_domains[ref] << endl;
#endif

  if(!found){
    MayDay::Abort("nwomfconductivityopfactory::AMRnewOp - no corresponding starting level to a_domain_fine");
  }
  

  // All this shit must be set.
  RefCountedPtr<LevelData<MFCellFAB> >   aco       = m_aco[ref];
  RefCountedPtr<LevelData<MFFluxFAB> >   bco       = m_bco[ref];
  RefCountedPtr<LevelData<MFBaseIVFAB> > bco_irreg = m_bco_irreg[ref];
  RefCountedPtr<LevelData<BaseIVFAB<Real> > > jump = m_jump[ref];

  NWOMFQuadCFInterp quadcfi = m_mfquadcfi[ref];
  
  MFLevelGrid mflg_fine;
  MFLevelGrid mflg = m_mflg[ref];
  MFLevelGrid mflg_coar;
  MFLevelGrid mflg_coar_mg;

  ProblemDomain domain = m_domains[ref];

  bool layout_changed = m_layout_changed[ref];
  bool has_mg         = m_has_mg_objects[ref];
  bool has_fine       = ref < m_num_levels -1;
  bool has_coar       = ref > 0;

  int ref_to_fine;
  int ref_to_coar;
  int relax_type = m_relax_type;
  int ebbc_order = m_ebbc_order; 

  IntVect ghost_phi = m_ghost_phi;
  IntVect ghost_rhs = m_ghost_rhs;

#if verb
  pout() << "factory ghost = " << m_ghost_phi << endl;
#endif

  Real dx = m_dx[ref];
  Real dx_coar;
  Real alpha = m_alpha;
  Real beta  = m_beta;


  has_fine = ref < m_num_levels - 1;
  has_coar = ref > 0;

#if verb
  pout() << "has_fine = " << has_fine << endl;
#endif


  if(has_coar){
#if verb
    pout() << "setting coarse stuff" << endl;
#endif
    const int coar_lvl = ref - 1;
    mflg_coar   = m_mflg[coar_lvl];
    ref_to_coar = m_ref_rat[coar_lvl];
    dx_coar     = m_dx[coar_lvl];
  }

  if(has_fine){
#if verb
    pout() << "setting fine stuff" << endl;
#endif
    mflg_fine   = m_mflg[ref + 1];
    ref_to_fine = m_ref_rat[ref];
  }

  if(has_mg){
#if verb
    pout() << "setting mg stuff" << endl;
#endif
    const int next_coarser = 1;
    mflg_coar_mg = m_mflg_mg[ref][next_coarser];  // Coarser state 
  }

#if verb
  pout() << "creating oper" << endl;
#endif
  nwomfconductivityop* oper = new nwomfconductivityop();

  oper->define(m_mfis,
	       m_dombc,
	       aco,
	       bco,
	       bco_irreg,
	       quadcfi,
	       mflg_fine,
	       mflg,
	       mflg_coar,
	       mflg_coar_mg,
	       domain,
	       layout_changed,
	       has_mg,
	       has_fine,
	       has_coar,
	       ref_to_fine,
	       ref_to_coar,
	       m_relax_type,
	       ebbc_order,
	       ghost_phi,
	       ghost_rhs,
	       dx,
	       dx_coar,
	       alpha,
	       beta,
	       m_origin);
#if verb
  pout() << "setting jump" << endl;
#endif
  oper->set_jump(jump);
  oper->set_electrodes(m_electrodes, m_potential);

#if 0 // Debug-stop
  MayDay::Abort("nwomfconductivityopfactory::AMRnewOp - implementation is not finished!");
#endif
#if verb
  pout() << "nwomfconductivityopfactory::AMRnewOp - returning new op" << endl;
#endif
  return static_cast<AMRLevelOp<LevelData<MFCellFAB> >* > (oper);

}