/*!
  @file time_stepper.cpp
  @brief Implementation of time_stepper.H
  @author Robert Marskar
  @date Nov. 2017
*/

#include "time_stepper.H"
#include "poisson_multifluid_gmg.H"
#include "cdr_iterator.H"
#include "rte_iterator.H"
#include "units.H"

#include <ParmParse.H>
#include <EBLevelDataOps.H>


time_stepper::time_stepper(){
  this->set_verbosity(1);
  this->set_cfl(0.8);
  this->set_relax_time(1.0);
  this->set_relax_level(-1);
  this->set_source_growth(1.E10);
  this->set_source_growth_tolerance(0.1);
  this->set_source_growth_elec_only(true);
  this->set_min_dt(0.0);
  this->set_max_dt(1.E99);
  this->set_fast_rte(1);
  this->set_fast_poisson(1);
  this->set_solver_verbosity(0);
}

time_stepper::~time_stepper(){
  
}

int time_stepper::query_ghost(){
  return 3;
}

bool time_stepper::stationary_rte(){
  CH_TIME("time_stepper::stationary_rte");
  if(m_verbosity > 5){
    pout() << "time_stepper::stationary_rte" << endl;
  }

  return m_rte->is_stationary();
}

bool time_stepper::solve_poisson(){
  CH_TIME("time_stepper::solve_poisson()");
  if(m_verbosity > 5){
    pout() << "time_stepper::solve_poisson()" << endl;
  }

  this->compute_rho();
  const bool converged = m_poisson->solve(m_poisson->get_state(),
					  m_poisson->get_source(),
					  m_sigma->get_state(),
					  false);

  return converged;
}

bool time_stepper::solve_poisson(MFAMRCellData&                a_potential,
				 MFAMRCellData&                a_rhs,
				 const Vector<EBAMRCellData*>  a_densities,
				 const EBAMRIVData&            a_sigma,
				 const centering::which_center a_centering){
  CH_TIME("time_stepper::solve_poisson(full)");
  if(m_verbosity > 5){
    pout() << "time_stepper::solve_poisson(full)" << endl;
  }

  this->compute_rho(a_rhs, a_densities, a_centering);

  const bool converged = m_poisson->solve(a_potential, a_rhs, a_sigma, false);

  return converged;
}

void time_stepper::compute_cdr_diffco_face(Vector<EBAMRFluxData*>&       a_diffco_face,
					   const Vector<EBAMRCellData*>& a_cdr_densities,
					   const EBAMRCellData&          a_E,
					   const Real&                   a_time){
  CH_TIME("time_stepper::compute_cdr_diffco_face(full)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_cdr_diffco_face(full)" << endl;
  }

  const int comp         = 0;
  const int ncomp        = 1;
  const int num_species  = m_plaskin->get_num_species();
  const int finest_level = m_amr->get_finest_level();

  // Allocate data for cell-centered diffusion coefficients
  Vector<EBAMRCellData> diffco(num_species);
  Vector<Real> cdr_densities(num_species);
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    const int idx = solver_it.get_solver();

    m_amr->allocate(diffco[idx], m_cdr->get_phase(), ncomp);
  }

  for (int lvl = 0; lvl <= finest_level; lvl++){
    const DisjointBoxLayout& dbl  = m_amr->get_grids()[lvl];
    const EBISLayout& ebisl       = m_amr->get_ebisl(m_cdr->get_phase())[lvl];
    const Real dx                 = m_amr->get_dx()[lvl];
    const RealVect origin         = m_physdom->get_prob_lo();
    
    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const Box box          = dbl.get(dit());
      const EBISBox& ebisbox = ebisl[dit()];
      const EBGraph& ebgraph = ebisbox.getEBGraph();
      const IntVectSet ivs   = IntVectSet(box);
      const EBCellFAB& E     = (*a_E[lvl])[dit()];

      for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){
	const VolIndex& vof = vofit();

	const RealVect pos  = EBArith::getVofLocation(vof, dx*RealVect::Unit, origin);
	const RealVect e = RealVect(D_DECL(E(vof, 0), E(vof, 1), E(vof, 2)));

	for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	  const int idx = solver_it.get_solver();
	  cdr_densities[idx] = (*(*a_cdr_densities[idx])[lvl])[dit()](vof, 0);
	}

	const Vector<Real> coeffs = m_plaskin->compute_cdr_diffusion_coefficients(a_time,
										  pos,
										  e,
										  cdr_densities);

	for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	  const int idx = solver_it.get_solver();
	  (*(diffco[idx])[lvl])[dit()](vof, comp) = coeffs[idx];
	}
      }
    }
  }


  // Compute face-centered things by taking the average of cell-centered things
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    const RefCountedPtr<cdr_solver>& solver = solver_it();
    const int idx = solver_it.get_solver();

    if(solver->is_diffusive()){ // Only need to do this for diffusive things
      m_amr->average_down(diffco[idx], m_cdr->get_phase());
      m_amr->interp_ghost(diffco[idx], m_cdr->get_phase());

      data_ops::average_cell_to_face_allcomps(*a_diffco_face[idx], diffco[idx], m_amr->get_domains());
    }
    else { // Just set this to something for non-diffusive species
      data_ops::set_value(*a_diffco_face[idx], 0.0);
    }
  }
}

void time_stepper::compute_cdr_diffco_eb(Vector<EBAMRIVData*>&       a_diffco_eb,
					 const Vector<EBAMRIVData*>& a_cdr_densities,
					 const EBAMRIVData&          a_E,
					 const Real&                 a_time){
  CH_TIME("time_stepper::compute_cdr_diffco_eb(full)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_cdr_diffco_eb(full)" << endl;
  }

  const int comp         = 0;
  const int ncomp        = 1;
  const int num_species  = m_plaskin->get_num_species();
  const int finest_level = m_amr->get_finest_level();
  const Real zero        = 0.0;

  Vector<Real> cdr_densities(num_species);
  
  for (int lvl = 0; lvl <= finest_level; lvl++){
    const DisjointBoxLayout& dbl  = m_amr->get_grids()[lvl];
    const EBISLayout& ebisl       = m_amr->get_ebisl(m_cdr->get_phase())[lvl];
    const Real dx                 = m_amr->get_dx()[lvl];
    const RealVect origin         = m_physdom->get_prob_lo();

    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const Box box            = dbl.get(dit());
      const EBISBox& ebisbox   = ebisl[dit()];
      const EBGraph& ebgraph   = ebisbox.getEBGraph();
      const IntVectSet ivs     = ebisbox.getIrregIVS(box);
      const BaseIVFAB<Real>& E = (*a_E[lvl])[dit()];
      CH_assert(E.nComp() == SpaceDim);
      
      for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){
	const VolIndex& vof = vofit();
	const RealVect cntr = ebisbox.bndryCentroid(vof);
	const RealVect e    = RealVect(D_DECL(E(vof,0), E(vof,1), E(vof,2)));
	const RealVect pos  = EBArith::getVofLocation(vof, dx*RealVect::Unit, origin) + cntr*dx;

	for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	  const int idx  = solver_it.get_solver();
	  const Real phi = (*(*a_cdr_densities[idx])[lvl])[dit()](vof, 0);
	  cdr_densities[idx] = Max(zero, phi);
	}


	const Vector<Real> diffco = m_plaskin->compute_cdr_diffusion_coefficients(a_time,
										  pos,
										  e,
										  cdr_densities);
										  
	for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	  const int idx = solver_it.get_solver();

	  (*(*a_diffco_eb[idx])[lvl])[dit()](vof, comp) = diffco[idx];
	}
      }
    }
  }
}

void time_stepper::compute_cdr_fluxes(Vector<EBAMRIVData*>&       a_fluxes,
				      const Vector<EBAMRIVData*>& a_extrap_cdr_fluxes,
				      const Vector<EBAMRIVData*>& a_extrap_cdr_densities,
				      const Vector<EBAMRIVData*>& a_extrap_cdr_velocities,
				      const Vector<EBAMRIVData*>& a_extrap_cdr_gradients,
				      const Vector<EBAMRIVData*>& a_extrap_rte_fluxes,
				      const EBAMRIVData&          a_E,
				      const Real&                 a_time){
  CH_TIME("time_stepper::compute_cdr_fluxes(full)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_cdr_fluxes(full)" << endl;
  }

  const int num_species  = m_plaskin->get_num_species();
  const int num_photons  = m_plaskin->get_num_species();
  const int comp         = 0;
  const int ncomp        = 1;
  const int finest_level = m_amr->get_finest_level();

  // Things that will be passed into plaskin
  Vector<Real> extrap_cdr_fluxes(num_species);
  Vector<Real> extrap_cdr_densities(num_species);
  Vector<Real> extrap_cdr_velocities(num_species);
  Vector<Real> extrap_cdr_gradients(num_species);
  Vector<Real> extrap_rte_fluxes(num_photons);

  for (int lvl = 0; lvl <= finest_level; lvl++){
    const DisjointBoxLayout& dbl  = m_amr->get_grids()[lvl];
    const EBISLayout& ebisl       = m_amr->get_ebisl(m_cdr->get_phase())[lvl];
    const ProblemDomain& domain   = m_amr->get_domains()[lvl];
    const Real dx                 = m_amr->get_dx()[lvl];
    const MFLevelGrid& mflg       = *(m_amr->get_mflg()[lvl]);
    const RealVect origin         = m_physdom->get_prob_lo();

    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const Box& box              = dbl.get(dit());
      const EBISBox& ebisbox      = ebisl[dit()];
      const EBGraph& ebgraph      = ebisbox.getEBGraph();
      const IntVectSet& diel_ivs  = mflg.interface_region(box, dit());
      const IntVectSet& elec_ivs  = ebisbox.getIrregIVS(box) - diel_ivs;

      // Loop over conductor cells
      for (VoFIterator vofit(elec_ivs, ebgraph); vofit.ok(); ++vofit){
	const VolIndex& vof = vofit();

	// Define the electric field
	const BaseIVFAB<Real>& E = (*a_E[lvl])[dit()];
	const RealVect centroid  = ebisbox.bndryCentroid(vof);
	const RealVect normal    = ebisbox.normal(vof);
	const RealVect e         = RealVect(D_DECL(E(vof,0), E(vof,1), E(vof,2)));
	const RealVect pos       = EBArith::getVofLocation(vof, dx*RealVect::Unit, origin) + centroid*dx;

	// Build ion densities and velocities
	for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	  const int idx = solver_it.get_solver();
    	  extrap_cdr_fluxes[idx]     = (*(*a_extrap_cdr_fluxes[idx])[lvl])[dit()](vof,comp);
    	  extrap_cdr_densities[idx]  = (*(*a_extrap_cdr_densities[idx])[lvl])[dit()](vof,comp);
    	  extrap_cdr_velocities[idx] = (*(*a_extrap_cdr_velocities[idx])[lvl])[dit()](vof,comp);
	  extrap_cdr_gradients[idx]  = (*(*a_extrap_cdr_gradients[idx])[lvl])[dit()](vof,comp);
	}

	// Build photon intensities
	for (rte_iterator solver_it(*m_rte); solver_it.ok(); ++solver_it){
	  const int idx = solver_it.get_solver();
	  extrap_rte_fluxes[idx] = (*(*a_extrap_rte_fluxes[idx])[lvl])[dit()](vof,comp);
	}

	const Vector<Real> fluxes = m_plaskin->compute_cdr_electrode_fluxes(a_time,
									    pos,
									    normal,
									    e,
									    extrap_cdr_densities,
									    extrap_cdr_velocities,
									    extrap_cdr_gradients,
									    extrap_rte_fluxes,
									    extrap_cdr_fluxes);
	
	// Put the fluxes in their respective place
	for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	  const int idx = solver_it.get_solver();
	  (*(*a_fluxes[idx])[lvl])[dit()](vof, comp) = fluxes[idx];
	}
      }

      // Loop over dielectric cells
      for (VoFIterator vofit(diel_ivs, ebgraph); vofit.ok(); ++vofit){
	const VolIndex& vof = vofit();

	// Define the electric field
	const BaseIVFAB<Real>& E = (*a_E[lvl])[dit()];
	const RealVect centroid  = ebisbox.bndryCentroid(vof);
	const RealVect normal    = ebisbox.normal(vof);
	const RealVect e         = RealVect(D_DECL(E(vof,0), E(vof,1), E(vof,2)));
	const RealVect pos       = EBArith::getVofLocation(vof, dx*RealVect::Unit, origin) + centroid*dx;
	const Real     time      = 0.0;

	// Build ion densities and velocities
	for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	  const int idx = solver_it.get_solver();
    	  extrap_cdr_fluxes[idx]     = (*(*a_extrap_cdr_fluxes[idx])[lvl])[dit()](vof,comp);
    	  extrap_cdr_densities[idx]  = (*(*a_extrap_cdr_densities[idx])[lvl])[dit()](vof,comp);
    	  extrap_cdr_velocities[idx] = (*(*a_extrap_cdr_velocities[idx])[lvl])[dit()](vof,comp);
	  extrap_cdr_gradients[idx]  = (*(*a_extrap_cdr_gradients[idx])[lvl])[dit()](vof,comp);
	}

	// Build photon intensities
	for (rte_iterator solver_it(*m_rte); solver_it.ok(); ++solver_it){
	  const int idx = solver_it.get_solver();
	  extrap_rte_fluxes[idx] = (*(*a_extrap_rte_fluxes[idx])[lvl])[dit()](vof,comp);
	}

	const Vector<Real> fluxes = m_plaskin->compute_cdr_dielectric_fluxes(a_time,
									     pos,
									     normal,
									     e,
									     extrap_cdr_densities,
									     extrap_cdr_velocities,
									     extrap_cdr_gradients,
									     extrap_rte_fluxes,
									     extrap_cdr_fluxes);
	
	// Put the fluxes in their respective place
	for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	  const int idx = solver_it.get_solver();
	  (*(*a_fluxes[idx])[lvl])[dit()](vof, comp) = fluxes[idx];
	}
      }
    }
  }
}

void time_stepper::compute_cdr_domain_fluxes(Vector<EBAMRIFData*>&       a_fluxes,
					     const Vector<EBAMRIFData*>& a_extrap_cdr_fluxes,
					     const Vector<EBAMRIFData*>& a_extrap_cdr_densities,
					     const Vector<EBAMRIFData*>& a_extrap_cdr_velocities,
					     const Vector<EBAMRIFData*>& a_extrap_cdr_gradients,
					     const Vector<EBAMRIFData*>& a_extrap_rte_fluxes,
					     const EBAMRIFData&          a_E,
					     const Real&                 a_time){
  CH_TIME("time_stepper::compute_cdr_domain_fluxes(full)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_cdr_domain_fluxes(full)" << endl;
  }

  const int num_species  = m_plaskin->get_num_species();
  const int num_photons  = m_plaskin->get_num_species();
  const int comp         = 0;
  const int ncomp        = 1;
  const int finest_level = m_amr->get_finest_level();

  // Things that will be passed into plaskin
  Vector<Real> extrap_cdr_fluxes(num_species);
  Vector<Real> extrap_cdr_densities(num_species);
  Vector<Real> extrap_cdr_velocities(num_species);
  Vector<Real> extrap_cdr_gradients(num_species);
  Vector<Real> extrap_rte_fluxes(num_photons);

  for (int lvl = 0; lvl <= finest_level; lvl++){
    const DisjointBoxLayout& dbl  = m_amr->get_grids()[lvl];
    const EBISLayout& ebisl       = m_amr->get_ebisl(m_cdr->get_phase())[lvl];
    const ProblemDomain& domain   = m_amr->get_domains()[lvl];
    const Real dx                 = m_amr->get_dx()[lvl];
    const RealVect origin         = m_physdom->get_prob_lo();

    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const Box& box         = dbl.get(dit());
      const EBISBox& ebisbox = ebisl[dit()];
      const EBGraph& ebgraph = ebisbox.getEBGraph();

      const FaceStop::WhichFaces crit = FaceStop::AllBoundaryOnly;
      
      for (int dir = 0; dir < SpaceDim; dir++){
	for (SideIterator sit; sit.ok(); ++sit){
	  const IntVectSet& ivs  = (*(*a_fluxes[0])[lvl])[dit()](dir, sit()).getIVS();

	  for (FaceIterator faceit(ivs, ebgraph, dir, crit); faceit.ok(); ++faceit){
	    const FaceIndex& face = faceit();
	    const RealVect pos    = EBArith::getFaceLocation(face, dx*RealVect::Unit, origin);
	    
	    // Define the electric field
	    const RealVect E = RealVect(D_DECL((*a_E[lvl])[dit()](dir, sit())(face,0),
					       (*a_E[lvl])[dit()](dir, sit())(face,1),
					       (*a_E[lvl])[dit()](dir, sit())(face,2)));

	    // Ion densities. 
	    for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	      const int idx = solver_it.get_solver();
	      extrap_cdr_fluxes[idx]     = (*(*a_extrap_cdr_fluxes[idx])[lvl])[dit()](dir, sit())(face,comp);
	      extrap_cdr_densities[idx]  = (*(*a_extrap_cdr_densities[idx])[lvl])[dit()](dir, sit())(face,comp);
	      extrap_cdr_velocities[idx] = (*(*a_extrap_cdr_velocities[idx])[lvl])[dit()](dir, sit())(face,comp);
	      extrap_cdr_gradients[idx]  = (*(*a_extrap_cdr_gradients[idx])[lvl])[dit()](dir, sit())(face,comp);
	    }

	    // Photon fluxes
	    for (rte_iterator solver_it(*m_rte); solver_it.ok(); ++solver_it){
	      const int idx = solver_it.get_solver();
	      extrap_rte_fluxes[idx] = (*(*a_extrap_rte_fluxes[idx])[lvl])[dit()](dir, sit())(face,comp);
	    }

	    // Call plasma_kinetics
	    const Vector<Real> fluxes = m_plaskin->compute_cdr_domain_fluxes(a_time,
									     pos,
									     dir,
									     sit(),
									     E,
									     extrap_cdr_densities,
									     extrap_cdr_velocities,
									     extrap_cdr_gradients,
									     extrap_rte_fluxes,
									     extrap_cdr_fluxes);

	    // Put fluxes where they belong
	    for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	      const int idx = solver_it.get_solver();
	      (*(*a_fluxes[idx])[lvl])[dit()](dir, sit())(face, comp) = fluxes[idx];
	    }
	  }
	}
      }
    }
  }
}

void time_stepper::compute_gradients_at_eb(Vector<EBAMRIVData*>&         a_grad,
					   const phase::which_phase&     a_phase,
					   const Vector<EBAMRCellData*>& a_phi){
  CH_TIME("time_stepper::compute_gradients_at_eb");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_gradients_at_eb" << endl;
  }

  CH_assert(a_grad.size() == a_phi.size());

  EBAMRIVData   eb_gradient;
  EBAMRCellData gradient;
  m_amr->allocate(eb_gradient, a_phase, SpaceDim);
  m_amr->allocate(gradient,    a_phase, SpaceDim);

  for (int i = 0; i < a_phi.size(); i++){
    EBAMRIVData& grad_density    = *a_grad[i];
    const EBAMRCellData& density = *a_phi[i];

    CH_assert(grad_density[0]->nComp() == 1);
    CH_assert(density[0]->nComp()      == 1);
    
    m_amr->compute_gradient(gradient, density);                         // Compute cell-centered gradient
    m_amr->average_down(gradient, a_phase);                             // Average down
    m_amr->interp_ghost(gradient, a_phase);                             // Interpolate ghost cells (have to do this before interp)
    this->extrapolate_to_eb(eb_gradient, a_phase, gradient);            // Extrapolate to EB
    this->project_flux(grad_density, eb_gradient);                      // Project onto EB
  }
}



void time_stepper::compute_gradients_at_domain_faces(Vector<EBAMRIFData*>&         a_grad,
						     const phase::which_phase&     a_phase,
						     const Vector<EBAMRCellData*>& a_phi){
  CH_TIME("time_stepper::compute_gradients_at_eb");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_gradients_at_eb" << endl;
  }

  CH_assert(a_grad.size() == a_phi.size());

  EBAMRIFData   domain_gradient;
  EBAMRCellData gradient;
  m_amr->allocate(domain_gradient, a_phase, SpaceDim);
  m_amr->allocate(gradient,        a_phase, SpaceDim);

  for (int i = 0; i < a_phi.size(); i++){
    EBAMRIFData& grad_density    = *a_grad[i];
    const EBAMRCellData& density = *a_phi[i];

    CH_assert(grad_density[0]->nComp() == 1);
    CH_assert(density[0]->nComp()      == 1);
    
    m_amr->compute_gradient(gradient, density);                         
    m_amr->average_down(gradient, a_phase);                             
    m_amr->interp_ghost(gradient, a_phase);                             
    
    this->extrapolate_to_domain_faces(domain_gradient, a_phase, gradient);  // Extrapolate to EB
    this->project_domain(grad_density, domain_gradient);                    // Project normal compoent
  }
}

void time_stepper::compute_cdr_sources(Vector<EBAMRCellData*>&        a_sources,
				       const Vector<EBAMRCellData*>&  a_cdr_densities,
				       const Vector<EBAMRCellData*>&  a_rte_densities,
				       const EBAMRCellData&           a_E,
				       const Real&                    a_time,
				       const centering::which_center  a_centering){
  CH_TIME("time_stepper::compute_cdr_sources(full)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_cdr_sources(full)" << endl;
  }

  const int num_species = m_plaskin->get_num_species();

#if 1 // New code
  Vector<EBAMRCellData*> grad_cdr(num_species); // Holders for grad(cdr)
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    const int idx = solver_it.get_solver();
    grad_cdr[idx] = new EBAMRCellData();                            // This storage must be deleted
    m_amr->allocate(*grad_cdr[idx], m_cdr->get_phase(), SpaceDim);  // Allocate

    m_amr->compute_gradient(*grad_cdr[idx], *a_cdr_densities[idx]); // Compute grad()
    m_amr->average_down(*grad_cdr[idx], m_cdr->get_phase());        // Average down
    m_amr->interp_ghost(*grad_cdr[idx], m_cdr->get_phase());        // Interpolate ghost cells
  }

  this->compute_cdr_sources(a_sources, a_cdr_densities, grad_cdr, a_rte_densities, a_E, a_time, a_centering);
#else // Original code
  const Real zero = 0.0;

  const int num_photons  = m_plaskin->get_num_photons();
  const int num_species  = m_plaskin->get_num_species();

  EBAMRCellData grad_E, E_norm;
  m_amr->allocate(grad_E, m_cdr->get_phase(), SpaceDim);  // Allocate storage for grad(|E|)
  m_amr->allocate(E_norm, m_cdr->get_phase(), 1);         // Allocate storage for |E|

  data_ops::vector_length(E_norm, a_E);             // Compute |E|
  m_amr->average_down(E_norm, m_cdr->get_phase());  // Average down
  m_amr->interp_ghost(E_norm, m_cdr->get_phase());  // Interpolate ghost cells

  m_amr->compute_gradient(grad_E, E_norm);           // Compute grad(|E|)
  m_amr->average_down(grad_E, m_cdr->get_phase());   // Average down
  m_amr->interp_ghost(grad_E, m_cdr->get_phase());   // Interpolate ghost cells

  Vector<EBAMRCellData*> grad_cdr(num_species); // Holders for grad(cdr)
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    const int idx = solver_it.get_solver();
    grad_cdr[idx] = new EBAMRCellData();                            // This storage must be deleted
    m_amr->allocate(*grad_cdr[idx], m_cdr->get_phase(), SpaceDim);  // Allocate

    m_amr->compute_gradient(*grad_cdr[idx], *a_cdr_densities[idx]); // Compute grad()
    m_amr->average_down(*grad_cdr[idx], m_cdr->get_phase());        // Average down
    m_amr->interp_ghost(*grad_cdr[idx], m_cdr->get_phase());        // Interpolate ghost cells
  }

  // Stencils for extrapolating things to cell centroids
  const irreg_amr_stencil<centroid_interp>& interp_stencils = m_amr->get_centroid_interp_stencils(m_cdr->get_phase());

  Real t1 = MPI_Wtime();
  for (int lvl = 0; lvl <= m_amr->get_finest_level(); lvl++){
    const DisjointBoxLayout& dbl = m_amr->get_grids()[lvl];
    const EBISLayout& ebisl      = m_amr->get_ebisl(m_cdr->get_phase())[lvl];
    const Real dx                = m_amr->get_dx()[lvl];
    const RealVect origin        = m_physdom->get_prob_lo();
    
    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const Box box = dbl.get(dit());
      const EBISBox& ebisbox = ebisl[dit()];
      const EBGraph& ebgraph = ebisbox.getEBGraph();
      const EBCellFAB& E     = (*a_E[lvl])[dit()];
      const EBCellFAB& gradE = (*grad_E[lvl])[dit()];

      // Things to pass into plasma_kinetics
      RealVect         pos;
      RealVect         e;
      RealVect         grad_e;
      Vector<Real>     cdr_densities(num_species);
      Vector<Real>     rte_densities(num_photons);
      Vector<RealVect> cdr_grad(num_species);

      // Irregular and multi-level cells.
      IntVectSet ivs(box);
      for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){ // Regular cells
	const VolIndex& vof = vofit();

	pos    = EBArith::getVofLocation(vof, dx*RealVect::Unit, origin);
	e      = RealVect(D_DECL(E(vof, 0), E(vof, 1), E(vof, 2)));
	grad_e = RealVect(D_DECL(gradE(vof, 0), gradE(vof, 1), gradE(vof, 2)));

	for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	  const int idx  = solver_it.get_solver();
	  const Real phi = (*(*a_cdr_densities[idx])[lvl])[dit()](vof, 0);
	  cdr_densities[idx] = Max(zero, phi);
	  cdr_grad[idx]      = RealVect(D_DECL((*(*grad_cdr[idx])[lvl])[dit()](vof, 0),
					       (*(*grad_cdr[idx])[lvl])[dit()](vof, 1),
					       (*(*grad_cdr[idx])[lvl])[dit()](vof, 2)));
	}

	for (rte_iterator solver_it = m_rte->iterator(); solver_it.ok(); ++solver_it){
	  const int idx  = solver_it.get_solver();
	  const Real phi = (*(*a_rte_densities[idx])[lvl])[dit()](vof, 0);
	  rte_densities[idx] = Max(zero, phi);
	}

	// Compute sources
	const Vector<Real> sources = m_plaskin->compute_cdr_source_terms(a_time,
									 pos,
									 e,
									 grad_e,
									 cdr_densities,
									 rte_densities,
									 cdr_grad);

	for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	  const int idx = solver_it.get_solver();
	  (*(*a_sources[idx])[lvl])[dit()](vof, 0) = sources[idx];
	}
      }

      if(a_centering == centering::cell_center){ // Irregular cells
	ivs = ebisbox.getIrregIVS(box);
	for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){
	  const VolIndex& vof       = vofit();
	  const VoFStencil& stencil = interp_stencils[lvl][dit()](vof, 0);
	  
	  pos = EBArith::getVofLocation(vof, dx*RealVect::Unit, origin);

	  e      = RealVect::Zero;
	  grad_e = RealVect::Zero;
	  for (int i = 0; i < stencil.size(); i++){
	    const VolIndex& ivof = stencil.vof(i);
	    const Real& iweight  = stencil.weight(i);
	    for (int dir = 0; dir < SpaceDim; dir++){
	      e[dir] += E(ivof, dir)*iweight;
	      grad_e[dir] += gradE(ivof, dir)*iweight;
	    }
	  }

	  // Compute cdr_densities and their gradients
	  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	    const int idx = solver_it.get_solver();

	    Real phi = 0.0;
	    RealVect grad = RealVect::Zero;
	    for (int i = 0; i < stencil.size(); i++){
	      const VolIndex& ivof = stencil.vof(i);
	      const Real& iweight  = stencil.weight(i);
	      
	      phi += (*(*a_cdr_densities[idx])[lvl])[dit()](ivof, 0)*iweight;
	      for (int dir = 0; dir < SpaceDim; dir++){
		grad[dir] += (*(*grad_cdr[idx])[lvl])[dit()](ivof, dir);
	      }
	    }
	    cdr_densities[idx] = Max(zero, phi);
	    cdr_grad[idx] = grad;
	    
	  }

	  for (rte_iterator solver_it = m_rte->iterator(); solver_it.ok(); ++solver_it){
	    const int idx = solver_it.get_solver();

	    Real phi = 0.0;
	    for (int i = 0; i < stencil.size(); i++){
	      const VolIndex& ivof = stencil.vof(i);
	      const Real& iweight  = stencil.weight(i);
	      phi += (*(*a_rte_densities[idx])[lvl])[dit()](ivof, 0)*iweight;
	    }
	    rte_densities[idx] = Max(zero, phi);
	  }


	  // Compute sources
	  const Vector<Real> sources = m_plaskin->compute_cdr_source_terms(a_time,
									   pos,
									   e,
									   grad_e,
									   cdr_densities,
									   rte_densities,
									   cdr_grad);

	  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	    const int idx = solver_it.get_solver();
	    (*(*a_sources[idx])[lvl])[dit()](vof, 0) = sources[idx];
	  }
	}
      }
    }
  }
  Real t2 = MPI_Wtime();

  // Delete extra storage - didn't use smart pointers for this...
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    const int idx = solver_it.get_solver();
    delete grad_cdr[idx];
  }

  // Average down
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    const int idx = solver_it.get_solver();
    m_amr->average_down(*a_sources[idx], m_cdr->get_phase());
    m_amr->interp_ghost(*a_sources[idx], m_cdr->get_phase());
  }
#endif
}

void time_stepper::compute_cdr_sources(Vector<EBAMRCellData*>&        a_sources,
				       const Vector<EBAMRCellData*>&  a_cdr_densities,
				       const Vector<EBAMRCellData*>&  a_cdr_gradients,
				       const Vector<EBAMRCellData*>&  a_rte_densities,
				       const EBAMRCellData&           a_E,
				       const Real&                    a_time,
				       const centering::which_center  a_centering){
  CH_TIME("time_stepper::compute_cdr_sources(full)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_cdr_sources(full)" << endl;
  }

  const Real zero = 0.0;

  const int num_photons  = m_plaskin->get_num_photons();
  const int num_species  = m_plaskin->get_num_species();

  // Compute grad(|E|)
  EBAMRCellData grad_E, E_norm;
  m_amr->allocate(grad_E, m_cdr->get_phase(), SpaceDim);  // Allocate storage for grad(|E|)
  m_amr->allocate(E_norm, m_cdr->get_phase(), 1);         // Allocate storage for |E|

  data_ops::vector_length(E_norm, a_E);             // Compute |E|
  m_amr->average_down(E_norm, m_cdr->get_phase());  // Average down
  m_amr->interp_ghost(E_norm, m_cdr->get_phase());  // Interpolate ghost cells

  m_amr->compute_gradient(grad_E, E_norm);           // Compute grad(|E|)
  m_amr->average_down(grad_E, m_cdr->get_phase());   // Average down
  m_amr->interp_ghost(grad_E, m_cdr->get_phase());   // Interpolate ghost cells


  // Stencils for extrapolating things to cell centroids
  const irreg_amr_stencil<centroid_interp>& interp_stencils = m_amr->get_centroid_interp_stencils(m_cdr->get_phase());

  Real t1 = MPI_Wtime();
  for (int lvl = 0; lvl <= m_amr->get_finest_level(); lvl++){
    const DisjointBoxLayout& dbl = m_amr->get_grids()[lvl];
    const EBISLayout& ebisl      = m_amr->get_ebisl(m_cdr->get_phase())[lvl];
    const Real dx                = m_amr->get_dx()[lvl];
    const RealVect origin        = m_physdom->get_prob_lo();
    
    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const Box box = dbl.get(dit());
      const EBISBox& ebisbox = ebisl[dit()];
      const EBGraph& ebgraph = ebisbox.getEBGraph();
      const EBCellFAB& E     = (*a_E[lvl])[dit()];
      const EBCellFAB& gradE = (*grad_E[lvl])[dit()];

      // Things to pass into plasma_kinetics
      RealVect         pos;
      RealVect         e;
      RealVect         grad_e;
      Vector<Real>     cdr_densities(num_species);
      Vector<Real>     rte_densities(num_photons);
      Vector<RealVect> cdr_grad(num_species);

      // Irregular and multi-level cells.
      IntVectSet ivs(box);
      for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){ // Regular cells
	const VolIndex& vof = vofit();

	pos    = EBArith::getVofLocation(vof, dx*RealVect::Unit, origin);
	e      = RealVect(D_DECL(E(vof, 0), E(vof, 1), E(vof, 2)));
	grad_e = RealVect(D_DECL(gradE(vof, 0), gradE(vof, 1), gradE(vof, 2)));

	for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	  const int idx  = solver_it.get_solver();
	  const Real phi = (*(*a_cdr_densities[idx])[lvl])[dit()](vof, 0);
	  cdr_densities[idx] = Max(zero, phi);
	  cdr_grad[idx]      = RealVect(D_DECL((*(*a_cdr_gradients[idx])[lvl])[dit()](vof, 0),
					       (*(*a_cdr_gradients[idx])[lvl])[dit()](vof, 1),
					       (*(*a_cdr_gradients[idx])[lvl])[dit()](vof, 2)));
	}

	for (rte_iterator solver_it = m_rte->iterator(); solver_it.ok(); ++solver_it){
	  const int idx  = solver_it.get_solver();
	  const Real phi = (*(*a_rte_densities[idx])[lvl])[dit()](vof, 0);
	  rte_densities[idx] = Max(zero, phi);
	}

	// Compute sources
	const Vector<Real> sources = m_plaskin->compute_cdr_source_terms(a_time,
									 pos,
									 e,
									 grad_e,
									 cdr_densities,
									 rte_densities,
									 cdr_grad);

	for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	  const int idx = solver_it.get_solver();
	  (*(*a_sources[idx])[lvl])[dit()](vof, 0) = sources[idx];
	}
      }

      if(a_centering == centering::cell_center){ // Irregular cells
	ivs = ebisbox.getIrregIVS(box);
	for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){
	  const VolIndex& vof       = vofit();
	  const VoFStencil& stencil = interp_stencils[lvl][dit()](vof, 0);
	  
	  pos = EBArith::getVofLocation(vof, dx*RealVect::Unit, origin);

	  e      = RealVect::Zero;
	  grad_e = RealVect::Zero;
	  for (int i = 0; i < stencil.size(); i++){
	    const VolIndex& ivof = stencil.vof(i);
	    const Real& iweight  = stencil.weight(i);
	    for (int dir = 0; dir < SpaceDim; dir++){
	      e[dir] += E(ivof, dir)*iweight;
	      grad_e[dir] += gradE(ivof, dir)*iweight;
	    }
	  }

	  // Compute cdr_densities and their gradients
	  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	    const int idx = solver_it.get_solver();

	    Real phi = 0.0;
	    RealVect grad = RealVect::Zero;
	    for (int i = 0; i < stencil.size(); i++){
	      const VolIndex& ivof = stencil.vof(i);
	      const Real& iweight  = stencil.weight(i);
	      
	      phi += (*(*a_cdr_densities[idx])[lvl])[dit()](ivof, 0)*iweight;
	      for (int dir = 0; dir < SpaceDim; dir++){
		grad[dir] += (*(*a_cdr_gradients[idx])[lvl])[dit()](ivof, dir);
	      }
	    }
	    cdr_densities[idx] = Max(zero, phi);
	    cdr_grad[idx] = grad;
	    
	  }

	  for (rte_iterator solver_it = m_rte->iterator(); solver_it.ok(); ++solver_it){
	    const int idx = solver_it.get_solver();

	    Real phi = 0.0;
	    for (int i = 0; i < stencil.size(); i++){
	      const VolIndex& ivof = stencil.vof(i);
	      const Real& iweight  = stencil.weight(i);
	      phi += (*(*a_rte_densities[idx])[lvl])[dit()](ivof, 0)*iweight;
	    }
	    rte_densities[idx] = Max(zero, phi);
	  }


	  // Compute sources
	  const Vector<Real> sources = m_plaskin->compute_cdr_source_terms(a_time,
									   pos,
									   e,
									   grad_e,
									   cdr_densities,
									   rte_densities,
									   cdr_grad);

	  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	    const int idx = solver_it.get_solver();
	    (*(*a_sources[idx])[lvl])[dit()](vof, 0) = sources[idx];
	  }
	}
      }
    }
  }

  // Average down
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    const int idx = solver_it.get_solver();
    m_amr->average_down(*a_sources[idx], m_cdr->get_phase());
    m_amr->interp_ghost(*a_sources[idx], m_cdr->get_phase());
  }
}

void time_stepper::compute_cdr_velocities(Vector<EBAMRCellData*>&       a_velocities,
					  const Vector<EBAMRCellData*>& a_cdr_densities,
					  const EBAMRCellData&          a_E,
					  const Real&                   a_time){
  CH_TIME("time_stepper::compute_cdr_velocities(full)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_cdr_velocities(full)" << endl;
  }
  
  const phase::which_phase cdr_phase = m_cdr->get_phase();
  const int finest_level             = m_amr->get_finest_level();

  for (int lvl = 0; lvl <= finest_level; lvl++){
    const DisjointBoxLayout& dbl  = m_amr->get_grids()[lvl];
    const EBISLayout& ebisl       = m_amr->get_ebisl(cdr_phase)[lvl];
    const Real dx                 = m_amr->get_dx()[lvl];
    
    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const Box box          = dbl.get(dit());
      const EBISBox& ebisbox = ebisl[dit()];
      const EBGraph& ebgraph = ebisbox.getEBGraph();
      const EBCellFAB& E     = (*a_E[lvl])[dit()];
      const RealVect origin  = m_physdom->get_prob_lo();

      const IntVect lo = box.smallEnd();
      const IntVect hi = box.bigEnd();
      
      // Do all cells in the box. 
      IntVectSet ivs(box);
      for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){
	const VolIndex& vof = vofit();
	const RealVect e    = RealVect(D_DECL(E(vof, 0), E(vof, 1), E(vof, 2)));
	const RealVect pos  = EBArith::getVofLocation(vof, dx*RealVect::Unit, origin);

	// Get densities
	Vector<Real> cdr_densities;
	for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	  const int idx = solver_it.get_solver();
	  cdr_densities.push_back((*(*a_cdr_densities[idx])[lvl])[dit()](vof, 0));
	}

	// Compute velocities
	const Vector<RealVect> velocities = m_plaskin->compute_cdr_velocities(a_time, pos, e, cdr_densities);

	// Put velocities in the appropriate place. 
	for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	  const int idx = solver_it.get_solver();
	  for (int comp = 0; comp < SpaceDim; comp++){
	    (*(*a_velocities[idx])[lvl])[dit()](vof, comp) = velocities[idx][comp];
	  }
	}
      }
    }

    // Average down and interpolate ghost cells
    for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
      const int idx = solver_it.get_solver();
      m_amr->average_down(*a_velocities[idx], cdr_phase);
      m_amr->interp_ghost(*a_velocities[idx], cdr_phase);
    }
  }
}

void time_stepper::compute_rte_sources(Vector<EBAMRCellData*>        a_source,
				       const Vector<EBAMRCellData*>& a_cdr_states,
				       const EBAMRCellData&          a_E,
				       const Real&                   a_time,
				       const centering::which_center a_centering){
  CH_TIME("time_stepper::compute_rte_sources(full)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_rte_sources(full)" << endl;
  }

  const Real zero = 0.0;
  const int num_photons = m_plaskin->get_num_photons();

  if(num_photons > 0){
    const phase::which_phase rte_phase = m_rte->get_phase();
    const int comp         = 0;
    const int ncomp        = 1;
    const int finest_level = m_amr->get_finest_level();

    const irreg_amr_stencil<centroid_interp>& interp_stencils = m_amr->get_centroid_interp_stencils(rte_phase);

    Vector<Real> cdr_densities(m_plaskin->get_num_species());

    for (int lvl = 0; lvl <= finest_level; lvl++){
      const DisjointBoxLayout& dbl  = m_amr->get_grids()[lvl];
      const EBISLayout& ebisl       = m_amr->get_ebisl(rte_phase)[lvl];
      const Real dx                 = m_amr->get_dx()[lvl];
      const irreg_stencil& stencils = interp_stencils[lvl];
      const RealVect origin         = m_physdom->get_prob_lo();
    
      for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
	const Box box          = dbl.get(dit());
	const EBISBox& ebisbox = ebisl[dit()];
	const EBGraph& ebgraph = ebisbox.getEBGraph();
	const EBCellFAB& E     = (*a_E[lvl])[dit()];

	// Do all cells
	IntVectSet ivs(box);
	for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){
	  const VolIndex& vof = vofit();

	  const RealVect pos  = EBArith::getVofLocation(vof, dx*RealVect::Unit, origin);

	  const RealVect e = RealVect(D_DECL(E(vof, 0), E(vof, 1), E(vof, 2)));
	  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	    const int idx  = solver_it.get_solver();
	    const Real phi = (*(*a_cdr_states[idx])[lvl])[dit](vof, comp);
	    cdr_densities[idx] = Max(zero, phi);
	  }

	  const Vector<Real> sources = m_plaskin->compute_rte_source_terms(a_time, pos, e, cdr_densities);
	
	  for (rte_iterator solver_it(*m_rte); solver_it.ok(); ++solver_it){
	    const int idx = solver_it.get_solver();
	    (*(*a_source[idx])[lvl])[dit()](vof, comp) = sources[idx];
	  }
	}

	// Do irregular cells
	if(a_centering == centering::cell_center){
	  ivs = ebisbox.getIrregIVS(box);
	  for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){
	    const VolIndex& vof = vofit();
	    const VoFStencil& stencil = stencils[dit()](vof, comp);
	  
	    const RealVect pos  = EBArith::getVofLocation(vof, dx*RealVect::Unit, origin);

	    // Compute CDR densities
	    for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
	      const int idx             = solver_it.get_solver();

	      Real phi = 0.0;
	      for (int i = 0; i < stencil.size(); i++){
		const VolIndex& ivof = stencil.vof(i);
		const Real& iweight  = stencil.weight(i);
		phi += (*(*a_cdr_states[idx])[lvl])[dit()](ivof, comp)*iweight;
	      }
	      cdr_densities[idx] = Max(zero, phi);
	    }

	    // Compute E
	    RealVect e = RealVect::Zero;
	    for (int i = 0; i < stencil.size(); i++){
	      const VolIndex& ivof = stencil.vof(i);
	      const Real& iweight  = stencil.weight(i);
	      for (int dir = 0; dir < SpaceDim; dir++){
		e[dir] += E(ivof, dir)*iweight;
	      }
	    }

	    Vector<Real> sources = m_plaskin->compute_rte_source_terms(a_time, pos, e, cdr_densities);
	    for (rte_iterator solver_it(*m_rte); solver_it.ok(); ++solver_it){
	      const int idx = solver_it.get_solver();
	      (*(*a_source[idx])[lvl])[dit()](vof, comp) = sources[idx];
	    }
	  }
	}
      }
    }


    for (rte_iterator solver_it(*m_rte); solver_it.ok(); ++solver_it){
      const int idx = solver_it.get_solver();
      m_amr->average_down(*a_source[idx], rte_phase);
    }
  }
}

void time_stepper::cache_states(){
  CH_TIME("time_stepper::cache_states");
  if(m_verbosity > 5){
    pout() << "time_stepper::cache_states" << endl;
  }

  m_cdr->cache_states();
  m_poisson->cache_state();
  m_rte->cache_states();
  m_sigma->cache_state();
}

void time_stepper::compute_cdr_sources(){
  CH_TIME("time_stepper::compute_cdr_sources()");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_cdr_sources()" << endl;
  }

  Vector<EBAMRCellData*> sources, cdr_densities, rte_densities;
  EBAMRCellData E;

  m_amr->allocate(E, m_cdr->get_phase(), SpaceDim);
  this->compute_E(E, m_cdr->get_phase(), m_poisson->get_state());

  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    RefCountedPtr<cdr_solver>& solver = solver_it();
    sources.push_back(&(solver->get_source()));
    cdr_densities.push_back(&(solver->get_state()));
  }

  for (rte_iterator solver_it(*m_rte); solver_it.ok(); ++solver_it){
    RefCountedPtr<rte_solver>& solver = solver_it();
    rte_densities.push_back(&(solver->get_state()));
  }

  this->compute_cdr_sources(sources, cdr_densities, rte_densities, E, m_time, centering::cell_center);
}

void time_stepper::compute_cdr_velocities(){
  CH_TIME("time_stepper::compute_cdr_velocities()");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_cdr_velocities()" << endl;
  }

  // Compute the electric field (again)
  EBAMRCellData E;
  m_amr->allocate(E, m_cdr->get_phase(), SpaceDim);
  this->compute_E(E, m_cdr->get_phase(), m_poisson->get_state());

  Vector<EBAMRCellData*> states     = m_cdr->get_states();
  Vector<EBAMRCellData*> velocities = m_cdr->get_velocities();

  this->compute_cdr_velocities(velocities, states, E, m_time);
}

void time_stepper::compute_cdr_diffusion(){
  CH_TIME("time_stepper::compute_cdr_diffusion");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_cdr_diffusion" << endl;
  }

  const int ncomp       = 1;
  const int num_species = m_plaskin->get_num_species();

  EBAMRCellData E_cell;
  EBAMRIVData   E_eb;
  m_amr->allocate(E_cell, m_cdr->get_phase(), SpaceDim);
  m_amr->allocate(E_eb,   m_cdr->get_phase(), SpaceDim);
  
  this->compute_E(E_cell, m_cdr->get_phase(), m_poisson->get_state());
  this->compute_E(E_eb,   m_cdr->get_phase(), E_cell);

  Vector<EBAMRCellData*> cdr_states  = m_cdr->get_states();

  time_stepper::compute_cdr_diffusion(E_cell, E_eb);
#if 0

  // Extrapolate states to the EB
  Vector<EBAMRIVData*> cdr_extrap(num_species);
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    const int idx = solver_it.get_solver();
    cdr_extrap[idx] = new EBAMRIVData();  // This must be deleted
    m_amr->allocate(*cdr_extrap[idx], m_cdr->get_phase(), ncomp);

    const irreg_amr_stencil<eb_centroid_interp>& stencil = m_amr->get_eb_centroid_interp_stencils(m_cdr->get_phase());
    stencil.apply(*cdr_extrap[idx], *cdr_states[idx]);
  }
  
  Vector<EBAMRFluxData*> diffco_face = m_cdr->get_diffco_face();
  Vector<EBAMRIVData*> diffco_eb     = m_cdr->get_diffco_eb();

  this->compute_cdr_diffco_face(diffco_face, cdr_states, E_cell, m_time);
  this->compute_cdr_diffco_eb(diffco_eb,     cdr_extrap, E_eb,   m_time);

  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    const int idx = solver_it.get_solver();
    delete cdr_extrap[idx];
  }
#endif
}

void time_stepper::compute_cdr_diffusion(const EBAMRCellData& a_E_cell, const EBAMRIVData& a_E_eb){
  CH_TIME("time_stepper::compute_cdr_diffusion");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_cdr_diffusion" << endl;
  }

  const int ncomp       = 1;
  const int num_species = m_plaskin->get_num_species();

  Vector<EBAMRCellData*> cdr_states  = m_cdr->get_states();

  // Extrapolate states to the EB
  Vector<EBAMRIVData*> cdr_extrap(num_species);
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    const int idx = solver_it.get_solver();
    cdr_extrap[idx] = new EBAMRIVData();  // This must be deleted
    m_amr->allocate(*cdr_extrap[idx], m_cdr->get_phase(), ncomp);

    const irreg_amr_stencil<eb_centroid_interp>& stencil = m_amr->get_eb_centroid_interp_stencils(m_cdr->get_phase());
    stencil.apply(*cdr_extrap[idx], *cdr_states[idx]);
  }
  
  Vector<EBAMRFluxData*> diffco_face = m_cdr->get_diffco_face();
  Vector<EBAMRIVData*> diffco_eb     = m_cdr->get_diffco_eb();

  this->compute_cdr_diffco_face(diffco_face, cdr_states, a_E_cell, m_time);
  this->compute_cdr_diffco_eb(diffco_eb,     cdr_extrap, a_E_eb,   m_time);

  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    const int idx = solver_it.get_solver();
    delete cdr_extrap[idx];
  }
}

void time_stepper::compute_dt(Real& a_dt, time_code::which_code& a_timecode){
  CH_TIME("time_stepper::compute_dt");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_dt" << endl;
  }

  Real dt = 1.E99;

  const Real dt_cfl = m_cfl*m_cdr->compute_cfl_dt();
  if(dt_cfl < dt){
    dt = dt_cfl;
    a_timecode = time_code::cfl;
  }
  
  const Real dt_dif = m_cfl*m_cdr->compute_diffusive_dt();
  if(dt_dif < dt){
    dt = dt_dif;
    a_timecode = time_code::diffusion;
  }

  const Real dt_src = m_src_growth*m_cdr->compute_source_dt(m_src_tolerance, m_src_elec_only);
  if(dt_src < dt){
    dt = dt_src;
    a_timecode = time_code::source;
  }

  const Real dt_relax = m_relax_time*this->compute_relaxation_time();
  if(dt_relax < dt){
    dt = dt_relax;
    a_timecode = time_code::relaxation_time;
  }

  const Real dt_restrict = this->restrict_dt();
  if(dt_restrict < dt){
    dt = dt_restrict;
    a_timecode = time_code::restricted;
  }

  if(dt < m_min_dt){
    dt = m_min_dt;
    a_timecode = time_code::hardcap;
  }

  if(dt > m_max_dt){
    dt = m_max_dt;
    a_timecode = time_code::hardcap;
  }

  a_dt = dt;
}

void time_stepper::compute_E(MFAMRCellData& a_E, const MFAMRCellData& a_potential){
  CH_TIME("time_stepper::compute_E(mfamrcell, mfamrcell)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_E(mfamrcell, mfamrcell)" << endl;
  }

  m_amr->compute_gradient(a_E, a_potential);
  data_ops::scale(a_E, -1.0);

  m_amr->average_down(a_E);
  m_amr->interp_ghost(a_E);
}

void time_stepper::compute_E(EBAMRCellData& a_E, const phase::which_phase a_phase){
  CH_TIME("time_stepper::compute_E(ebamrcell, phase)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_E(ebamrcell, phase)" << endl;
  }

  this->compute_E(a_E, a_phase, m_poisson->get_state());
}

void time_stepper::compute_E(EBAMRCellData& a_E, const phase::which_phase a_phase, const MFAMRCellData& a_potential){
  CH_TIME("time_stepper::compute_E(ebamrcell, phase, mfamrcell)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_E(ebamrcell, phase, mfamrcell)" << endl;
  }

  EBAMRCellData pot_gas;
  m_amr->allocate_ptr(pot_gas);
  m_amr->alias(pot_gas, a_phase, a_potential);

  m_amr->compute_gradient(a_E, pot_gas);
  data_ops::scale(a_E, -1.0);

  m_amr->average_down(a_E, a_phase);
  m_amr->interp_ghost(a_E, a_phase);
}

void time_stepper::compute_E(EBAMRFluxData& a_E_face, const phase::which_phase a_phase, const EBAMRCellData& a_E_cell){
  CH_TIME("time_stepper::compute_E(ebamrflux, phase, ebamrcell)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_E(ebamrflux, phase, ebamrcell)" << endl;
  }

  CH_assert(a_E_face[0]->nComp() == SpaceDim);
  CH_assert(a_E_cell[0]->nComp() == SpaceDim);

  const int finest_level = m_amr->get_finest_level();
  for (int lvl = 0; lvl <= finest_level; lvl++){

    const DisjointBoxLayout& dbl = m_amr->get_grids()[lvl];
    const EBISLayout& ebisl      = m_amr->get_ebisl(a_phase)[lvl];
    const ProblemDomain& domain  = m_amr->get_domains()[lvl];

    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const EBCellFAB& E_cell = (*a_E_cell[lvl])[dit()];
      const EBISBox& ebisbox  = ebisl[dit()];
      const EBGraph& ebgraph  = ebisbox.getEBGraph();
      const Box& box          = dbl.get(dit());
      
      for (int dir = 0; dir < SpaceDim; dir++){
      	EBFaceFAB& E_face = (*a_E_face[lvl])[dit()][dir];
	E_face.setVal(0.0);

      	EBLevelDataOps::averageCellToFace(E_face,
      					  E_cell,
      					  ebgraph,
      					  box,
      					  0,
      					  dir,
      					  domain,
      					  dir,
      					  dir);
      }

    }
    
    //    data_ops::average_cell_to_face_allcomps(*a_E_face[lvl], *a_E_cell[lvl], m_amr->get_domains()[lvl]);

    a_E_face[lvl]->exchange();
  }
}

void time_stepper::compute_E(EBAMRIVData& a_E_eb, const phase::which_phase a_phase, const EBAMRCellData& a_E_cell){
  CH_TIME("time_stepper::compute_E(ebamriv, phase, ebamrcell)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_E(ebamriv, phase, ebamrcell)" << endl;
  }

  CH_assert(a_E_eb[0]->nComp()   == SpaceDim);
  CH_assert(a_E_cell[0]->nComp() == SpaceDim);

  const irreg_amr_stencil<eb_centroid_interp>& interp_stencil = m_amr->get_eb_centroid_interp_stencils(a_phase);
  interp_stencil.apply(a_E_eb, a_E_cell);
}

void time_stepper::compute_Emax(Real& a_Emax, const phase::which_phase a_phase){
  CH_TIME("time_stepper::compute_Emax(Real, phase)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_Emax(Real, phase)" << endl;
  }

  EBAMRCellData E;
  m_amr->allocate(E, a_phase, SpaceDim);

  this->compute_E(E, a_phase, m_poisson->get_state());

  Real max, min;
  data_ops::get_max_min_norm(max, min, E);

  a_Emax = max;
}

void time_stepper::compute_charge_flux(EBAMRIVData& a_flux, Vector<EBAMRIVData*>& a_cdr_fluxes){
  CH_TIME("time_stepper::compute_charge_flux");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_charge_flux" << endl;
  }

  data_ops::set_value(a_flux, 0.0);

  for (cdr_iterator solver_it(*m_cdr); solver_it.ok(); ++solver_it){
    const RefCountedPtr<cdr_solver>& solver = solver_it();
    const RefCountedPtr<species>& spec      = solver_it.get_species();
    const EBAMRIVData& solver_flux          = *a_cdr_fluxes[solver_it.get_solver()];

    data_ops::incr(a_flux, solver_flux, spec->get_charge()*units::s_Qe);
  }

  m_sigma->reset_cells(a_flux);
}

void time_stepper::compute_extrapolated_fluxes(Vector<EBAMRIVData*>&        a_fluxes,
					       const Vector<EBAMRCellData*> a_densities,
					       const Vector<EBAMRCellData*> a_velocities,
					       const phase::which_phase     a_phase){
  CH_TIME("time_stepper::compute_extrapolated_fluxes");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_extrapolated_fluxes" << endl;
  }

#if 0 // Original code
  EBAMRCellData cell_flux;
  EBAMRIVData   eb_flux;

  m_amr->allocate(cell_flux, a_phase, SpaceDim);
  m_amr->allocate(eb_flux, a_phase, SpaceDim);

  for (int i = 0; i < a_fluxes.size(); i++){
    this->compute_flux(cell_flux, *a_densities[i], *a_velocities[i]);
    
    m_amr->average_down(cell_flux, a_phase);
    m_amr->interp_ghost(cell_flux, a_phase);

    this->extrapolate_to_eb(eb_flux, a_phase, cell_flux);
    
    this->project_flux(*a_fluxes[i], eb_flux);
  }
#else // New way of doing this. Extrapolate everything to the BC first. Then compute the flux and project it.
  EBAMRIVData eb_flx; 
  EBAMRIVData eb_vel;
  EBAMRIVData eb_phi;

  m_amr->allocate(eb_flx, a_phase, SpaceDim);
  m_amr->allocate(eb_vel, a_phase, SpaceDim);
  m_amr->allocate(eb_phi, a_phase, 1);

  for (int i = 0; i < a_fluxes.size(); i++){
    const irreg_amr_stencil<eb_centroid_interp>& interp_stencils = m_amr->get_eb_centroid_interp_stencils(a_phase);

    interp_stencils.apply(eb_vel, *a_velocities[i]);
    interp_stencils.apply(eb_phi, *a_densities[i]);

    data_ops::set_value(eb_flx, 0.0);
    data_ops::incr(eb_flx, eb_vel, 1.0);
    data_ops::multiply_scalar(eb_flx, eb_phi);

    this->project_flux(*a_fluxes[i], eb_flx);

    m_amr->average_down(*a_fluxes[i], a_phase);
  }
#endif
}

void time_stepper::compute_extrapolated_domain_fluxes(Vector<EBAMRIFData*>&        a_fluxes,
						      const Vector<EBAMRCellData*> a_densities,
						      const Vector<EBAMRCellData*> a_velocities,
						      const phase::which_phase     a_phase){
  CH_TIME("time_stepper::compute_extrapolated_domain_fluxes");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_extrapolated_domain_fluxes" << endl;
  }

  EBAMRCellData cell_flux;
  EBAMRIFData domain_flux;

  m_amr->allocate(cell_flux,   a_phase, SpaceDim);
  m_amr->allocate(domain_flux, a_phase, SpaceDim);

  for (int i = 0; i < a_fluxes.size(); i++){
    data_ops::copy(cell_flux, *a_velocities[i]);
    data_ops::multiply_scalar(cell_flux, *a_densities[i]); // cell_flux = n*v

    // Extrapolate cell-centered to domain faces
    this->extrapolate_to_domain_faces(domain_flux, a_phase, cell_flux);

    // Project normal component onto domain face
    this->project_domain(*a_fluxes[i], domain_flux);
  }
}

void time_stepper::compute_flux(EBAMRCellData& a_flux, const EBAMRCellData& a_density, const EBAMRCellData& a_velocity){
  CH_TIME("time_stepper::compute_flux");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_flux" << endl;
  }
  
  const int finest_level = m_amr->get_finest_level();

  for (int lvl = 0; lvl <= finest_level; lvl++){
    CH_assert(a_flux[lvl]->nComp()     == SpaceDim);
    CH_assert(a_density[lvl]->nComp()  == 1);
    CH_assert(a_velocity[lvl]->nComp() == SpaceDim);

    data_ops::set_value(*a_flux[lvl], 0.0);
    data_ops::incr(*a_flux[lvl], *a_velocity[lvl], 1.0);
    data_ops::multiply_scalar(*a_flux[lvl], *a_density[lvl]);
  }
}

void time_stepper::compute_J(EBAMRCellData& a_J){
  CH_TIME("time_stepper::compute_J");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_J" << endl;
  }
  const int density_comp = 0;
  const int finest_level = m_amr->get_finest_level();

  for (int lvl = 0; lvl <= finest_level; lvl++){
    const DisjointBoxLayout& dbl = m_amr->get_grids()[lvl];
    const EBISLayout& ebisl      = m_amr->get_ebisl(m_cdr->get_phase())[lvl];

    data_ops::set_value(*a_J[lvl], 0.0);

    for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
      RefCountedPtr<cdr_solver>& solver = solver_it();
      RefCountedPtr<species>& spec      = solver_it.get_species();

      const int q                       = spec->get_charge();
      const EBAMRCellData& density      = solver->get_state();
      const EBAMRCellData& velo         = solver->get_velo_cell();

      for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
	const Box& box         = dbl.get(dit());
	const EBISBox& ebisbox = ebisl[dit()];
	const EBGraph& ebgraph = ebisbox.getEBGraph();
	const IntVectSet ivs(box);

	EBCellFAB& J       = (*a_J[lvl])[dit()];
	const EBCellFAB& n = (*density[lvl])[dit()];
	const EBCellFAB& v = (*velo[lvl])[dit()];

#if 1 // Optimized code. Need to check that this works. 
	EBCellFAB cdr_j(ebisbox, box, SpaceDim);
	cdr_j.setVal(0.0);
	for (int comp = 0; comp < SpaceDim; comp++){
	  cdr_j.plus(n, 0, comp, 1);
	}
	cdr_j *= v;
	cdr_j *= q;

	J += cdr_j;

#else // Original code
	for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){
	  const VolIndex& vof = vofit();

	  for (int comp = 0; comp < SpaceDim; comp++){
	    J(vof, comp) += q*n(vof,density_comp)*v(vof, comp);
	  }
	}
#endif
      }
    }

    data_ops::scale(*a_J[lvl], units::s_Qe);
  }

  m_amr->average_down(a_J, m_cdr->get_phase());
  m_amr->interp_ghost(a_J, m_cdr->get_phase());
}

void time_stepper::compute_rte_sources(){
  CH_TIME("time_stepper::compute_rte_sources(full)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_rte_sources(full)" << endl;
  }

  EBAMRCellData E;
  m_amr->allocate(E, m_rte->get_phase(), SpaceDim);
  this->compute_E(E, m_rte->get_phase(), m_poisson->get_state());

  Vector<EBAMRCellData*> cdr_states;
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    RefCountedPtr<cdr_solver>& solver = solver_it();
    cdr_states.push_back(&(solver->get_state()));
  }
  
  Vector<EBAMRCellData*> sources;
  for (rte_iterator solver_it(*m_rte); solver_it.ok(); ++solver_it){
    RefCountedPtr<rte_solver>& solver = solver_it();
    sources.push_back(&(solver->get_source()));
  }

  this->compute_rte_sources(sources, cdr_states, E, m_time, centering::cell_center);
}

void time_stepper::compute_rho(){
  CH_TIME("time_stepper::compute_rho()");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_rho()" << endl;
  }

  Vector<EBAMRCellData*> densities;
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    RefCountedPtr<cdr_solver> solver = solver_it();
    densities.push_back(&(solver->get_state()));
  }

  this->compute_rho(m_poisson->get_source(),
		    densities,
		    centering::cell_center);
}

void time_stepper::compute_rho(EBAMRCellData& a_rho, const phase::which_phase a_phase){
  CH_TIME("time_stepper::compute_rho(ebamrcelldata, phase)");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_rho(ebamrcelldata, phase)" << endl;;
  }

  CH_assert(a_phase == m_cdr->get_phase());

  data_ops::set_value(a_rho, 0.0);

  Vector<EBAMRCellData*> densities = m_cdr->get_states(); // Get densities from solver
  
  const int finest_level = m_amr->get_finest_level();
  for (int lvl = 0; lvl <= finest_level; lvl++){

    // Add volumetric charge 
    for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
      const EBAMRCellData& density       = *(densities[solver_it.get_solver()]);
      const RefCountedPtr<species>& spec = solver_it.get_species();

      data_ops::incr(*a_rho[lvl], *density[lvl], spec->get_charge());
    }

    // Scale by s_Qe/s_eps0
    data_ops::scale(*a_rho[lvl], units::s_Qe);
  }

  m_amr->average_down(a_rho, a_phase);
  m_amr->interp_ghost(a_rho, a_phase);
}

void time_stepper::compute_rho(MFAMRCellData&                 a_rho,
			       const Vector<EBAMRCellData*>&  a_densities,
			       const centering::which_center  a_centering){
  CH_TIME("time_stepper::compute_rho(mfamrcell, vec(ebamrcell))");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_rho(mfamrcell, vec(ebamrcell))" << endl;
  }

  data_ops::set_value(a_rho, 0.0);


  EBAMRCellData rho_gas;
  m_amr->allocate_ptr(rho_gas); 
  m_amr->alias(rho_gas, phase::gas, a_rho); 
  const int finest_level = m_amr->get_finest_level();
  for (int lvl = 0; lvl <= finest_level; lvl++){

    // Add volumetric charge 
    for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
      const EBAMRCellData& density       = *(a_densities[solver_it.get_solver()]);
      const RefCountedPtr<species>& spec = solver_it.get_species();

      data_ops::incr(*rho_gas[lvl], *density[lvl], spec->get_charge());
    }

    // Scale by s_Qe/s_eps0
    data_ops::scale(*a_rho[lvl], units::s_Qe);
  }


#if 0 // Debug
  MayDay::Warning("time_stepper::compute_rho - debug mode");
  data_ops::set_value(a_rho, 0.0);
#endif

  m_amr->average_down(a_rho);
  m_amr->interp_ghost(a_rho);

  // Transform to centroids
  if(a_centering == centering::cell_center){
    m_amr->interpolate_to_centroids(rho_gas, phase::gas);
  }
}

void time_stepper::deallocate_solver_internals(){
  CH_TIME("time_stepper::deallocate_solver_internals");
  if(m_verbosity > 5){
    pout() << "time_stepper::deallocate_solver_internals" << endl;
  }

  m_cdr->deallocate_internals();
  m_rte->deallocate_internals();
  m_poisson->deallocate_internals();
  m_sigma->deallocate_internals();
}

void time_stepper::extrapolate_to_eb(EBAMRIVData& a_extrap, const phase::which_phase a_phase, const EBAMRCellData& a_data){
  CH_TIME("time_stepper::extrapolate_to_eb");
  if(m_verbosity > 5){
    pout() << "time_stepper::extrapolate_to_eb" << endl;
  }

  const irreg_amr_stencil<eb_centroid_interp>& stencils = m_amr->get_eb_centroid_interp_stencils(a_phase);
  
  for (int lvl = 0; lvl <= m_amr->get_finest_level(); lvl++){
    stencils.apply(*a_extrap[lvl], *a_data[lvl], lvl);
  }
}

void time_stepper::extrapolate_to_eb(Vector<EBAMRIVData*>&         a_extrap,
				     const phase::which_phase      a_phase,
				     const Vector<EBAMRCellData*>& a_data){
  CH_TIME("time_stepper::extrapolate_to_eb(vec)");
  if(m_verbosity > 5){
    pout() << "time_stepper::extrapolate_to_eb(vec)" << endl;
  }

  CH_assert(a_extrap.size() == a_data.size());

  for (int i = 0; i < a_extrap.size(); i++){
    this->extrapolate_to_eb(*a_extrap[i], a_phase, *a_data[i]);
  }
}

void time_stepper::extrapolate_to_domain_faces(EBAMRIFData&             a_extrap,
					       const phase::which_phase a_phase,
					       const EBAMRCellData&     a_data){
  CH_TIME("time_stepper::extrapolate_to_domain_faces");
  if(m_verbosity > 5){
    pout() << "time_stepper::extrapolate_to_domain_faces" << endl;
  }


  for (int lvl = 0; lvl <= m_amr->get_finest_level(); lvl++){
    const int ncomp = a_data[lvl]->nComp();
      
    const DisjointBoxLayout& dbl = m_amr->get_grids()[lvl];
    const EBISLayout& ebisl      = m_amr->get_ebisl(a_phase)[lvl];
    
    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const EBCellFAB& data         = (*a_data[lvl])[dit()];
      const EBISBox& ebisbox        = ebisl[dit()];
      const BaseFab<Real>& data_fab = data.getSingleValuedFAB();
      
      for (int dir = 0; dir < SpaceDim; dir++){
	for (SideIterator sit; sit.ok(); ++sit){
	  BaseIFFAB<Real>& extrap = (*a_extrap[lvl])[dit()](dir, sit());

	  const IntVectSet& ivs  = extrap.getIVS();
	  const EBGraph& ebgraph = extrap.getEBGraph();

	  // Extrapolate to the boundary. Use face-centered stuff for all faces (also multivalued ones)
	  const FaceStop::WhichFaces crit = FaceStop::AllBoundaryOnly;
	  for (FaceIterator faceit(ivs, ebgraph, dir, crit); faceit.ok(); ++faceit){
	    const FaceIndex& face = faceit();

	    const int sgn = sign(sit()); // Lo = -1, Hi = 1
	    
	    const VolIndex& vof = face.getVoF(flip(sit()));
	    const IntVect iv0   = vof.gridIndex();
	    const IntVect iv1   = iv0 - sgn*BASISV(dir);

	    if(ebisbox.isCovered(iv0)){ // Just provide some bogus data because the face 
	      for (int comp = 0; comp < ncomp; comp++){
		extrap(face, comp) = 0.0;
	      }
	    }
	    else{
	      if(!ebisbox.isCovered(iv1)){ // linear extrapolation
		for (int comp = 0; comp < ncomp; comp++){
		  extrap(face, comp) = 1.5*data_fab(iv0, comp) - 0.5*data_fab(iv1, comp); // Should be ok
		}
	      }
	      else{ // Not enough cells available, use cell-centered only
		for (int comp = 0; comp < ncomp; comp++){
		  extrap(face, comp) = data_fab(iv0, comp);
		}
	      }

#if 0 // Debug
	      for (int comp = 0; comp < ncomp; comp++){
		extrap(face, comp) = data_fab(iv0, comp);
	      }
#endif
	    }
	  }
	}
      }
    }
  }
}

void time_stepper::extrapolate_to_domain_faces(Vector<EBAMRIFData*>&         a_extrap,
					       const phase::which_phase      a_phase,
					       const Vector<EBAMRCellData*>& a_data){
  CH_TIME("time_stepper::extrapolate_to_domain_faces(vec)");
  if(m_verbosity > 5){
    pout() << "time_stepper::extrapolate_to_domain_faces(vec)" << endl;
  }

  CH_assert(a_extrap.size() == a_data.size());

  for (int i = 0; i < a_extrap.size(); i++){
    this->extrapolate_to_domain_faces(*a_extrap[i], a_phase, *a_data[i]);
  }
}

void time_stepper::get_cdr_max(Real& a_cdr_max, std::string& a_solver_name){
  CH_TIME("time_stepper::get_cdr_max");
  if(m_verbosity > 5){
    pout() << "time_stepper::get_cdr_max" << endl;
  }

  const int comp = 0;
  
  a_cdr_max = -1.E99;
  a_solver_name = "invalid solver";
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    RefCountedPtr<cdr_solver>& solver = solver_it();
    Real max, min;
    data_ops::get_max_min(max, min, solver->get_state(), comp);

    if(max > a_cdr_max){
      a_cdr_max     = max;
      a_solver_name = solver->get_name();
    }
  }
}

void time_stepper::instantiate_solvers(){
  CH_TIME("time_stepper::instantiate_solvers");
  if(m_verbosity > 5){
    pout() << "time_stepper::instantiate_solvers" << endl;
  }

  this->sanity_check();

  this->setup_cdr();
  this->setup_rte();
  this->setup_poisson();
  this->setup_sigma();

  this->set_solver_verbosity(m_solver_verbosity);
}

void time_stepper::initial_data(){
  CH_TIME("time_stepper::initial_data");
  if(m_verbosity > 5){
    pout() << "time_stepper::initial_data" << endl;
  }

  m_cdr->initial_data();
  if(!m_rte->is_stationary()){
    m_rte->initial_data();
  }
  m_sigma->initial_data();
}

void time_stepper::initial_cdr_data(){
  CH_TIME("time_stepper::initial_cdr_data");
  if(m_verbosity > 5){
    pout() << "time_stepper::initial_cdr_data" << endl;
  }

  m_cdr->initial_data();
}

void time_stepper::initial_rte_data(){
  CH_TIME("time_stepper::initial_rte_data");
  if(m_verbosity > 5){
    pout() << "time_stepper::initial_rte_data" << endl;
  }

  m_rte->initial_data();
}

void time_stepper::initial_sigma_data(){
  CH_TIME("time_stepper::initial_sigma_data");
  if(m_verbosity > 5){
    pout() << "time_stepper::initial_sigma_data" << endl;
  }

  m_sigma->initial_data();
}

void time_stepper::project_flux(EBAMRIVData& a_projected_flux, const EBAMRIVData& a_flux){
  CH_TIME("time_stepper::project_flux");
  if(m_verbosity > 5){
    pout() << "time_stepper::project_flux" << endl;
  }

  const int comp         = 0;
  const int finest_level = m_amr->get_finest_level();

  for (int lvl = 0; lvl <= finest_level; lvl++){
    CH_assert(a_projected_flux[lvl]->nComp() == 1);
    CH_assert(a_flux[lvl]->nComp()           == SpaceDim);

    const DisjointBoxLayout& dbl  = m_amr->get_grids()[lvl];
    const EBISLayout& ebisl       = m_amr->get_ebisl(m_cdr->get_phase())[lvl];

    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const Box& box         = dbl.get(dit());
      const EBISBox& ebisbox = ebisl[dit()];
      const EBGraph& ebgraph = ebisbox.getEBGraph();
      const IntVectSet& ivs  = ebisbox.getIrregIVS(box);

      BaseIVFAB<Real>& proj_flux  = (*a_projected_flux[lvl])[dit()];
      const BaseIVFAB<Real>& flux = (*a_flux[lvl])[dit()];

      for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){
	const VolIndex& vof     = vofit();
	const RealVect& normal  = ebisbox.normal(vof);
	const RealVect vec_flux = RealVect(D_DECL(flux(vof,0), flux(vof,1), flux(vof,2)));

	// For EB's, the geometrical normal vector is opposite of the finite volume method normal
	proj_flux(vof,0) = PolyGeom::dot(vec_flux, -normal);
      }
    }
  }
}

void time_stepper::project_domain(EBAMRIFData& a_projected_flux, const EBAMRIFData& a_flux){
  CH_TIME("time_stepper::project_domain");
  if(m_verbosity > 5){
    pout() << "time_stepper::project_domain" << endl;
  }

  const int comp         = 0;
  const int finest_level = m_amr->get_finest_level();

  for (int lvl = 0; lvl <= finest_level; lvl++){
    CH_assert(a_projected_flux[lvl]->nComp() == 1);
    CH_assert(a_flux[lvl]->nComp()           == SpaceDim);

    const DisjointBoxLayout& dbl  = m_amr->get_grids()[lvl];
    const EBISLayout& ebisl       = m_amr->get_ebisl(m_cdr->get_phase())[lvl];

    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){

      const FaceStop::WhichFaces crit = FaceStop::AllBoundaryOnly;
      for (int dir = 0; dir < SpaceDim; dir++){
	for (SideIterator sit; sit.ok(); ++sit){
	  BaseIFFAB<Real>& normal_comp    = (*a_projected_flux[lvl])[dit()](dir, sit());
	  const BaseIFFAB<Real>& gradient = (*a_flux[lvl])[dit()](dir, sit());

	  const int sgn          = sign(sit());
	  const EBGraph& ebgraph = normal_comp.getEBGraph();
	  const IntVectSet& ivs  = normal_comp.getIVS();

	  for (FaceIterator faceit(ivs, ebgraph, dir, crit); faceit.ok(); ++faceit){
	    const FaceIndex& face = faceit();
	    normal_comp(face, comp) = gradient(face, dir);
	  }
	}
      }
    }
  }
}

void time_stepper::regrid(const int a_old_finest, const int a_new_finest){
  CH_TIME("time_stepper::regrid");
  if(m_verbosity > 5){
    pout() << "time_stepper::regrid" << endl;
  }

  this->regrid_solvers(a_old_finest, a_new_finest);
  this->regrid_internals();
}

void time_stepper::regrid_solvers(const int a_old_finest, const int a_new_finest){
  CH_TIME("time_stepper::regrid_solvers");
  if(m_verbosity > 5){
    pout() << "time_stepper::regrid_solvers" << endl;
  }

  m_cdr->regrid(a_old_finest,     a_new_finest);
  m_poisson->regrid(a_old_finest, a_new_finest);
  m_rte->regrid(a_old_finest,     a_new_finest);
  m_sigma->regrid(a_old_finest,   a_new_finest);
}

void time_stepper::reset_dielectric_cells(EBAMRIVData& a_data){
  CH_TIME("time_stepper::reset_dielectric_cells");
  if(m_verbosity > 5){
    pout() << "time_stepper::reset_dielectric_cells" << endl;
  }

  const int finest_level = m_amr->get_finest_level();

  for (int lvl = 0; lvl <= finest_level; lvl++){
    const DisjointBoxLayout& dbl = m_amr->get_grids()[lvl];
    const Real dx                = m_amr->get_dx()[lvl];
    const MFLevelGrid& mflg      = *m_amr->get_mflg()[lvl];

    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const Box box          = dbl.get(dit());
      BaseIVFAB<Real>& data  = (*a_data[lvl])[dit()];
      const IntVectSet ivs   = data.getIVS() & mflg.interface_region(box, dit());
      const EBGraph& ebgraph = data.getEBGraph();

      for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){
	const VolIndex& vof = vofit();
	for (int comp = 0; comp < data.nComp(); comp++){
	  data(vof, comp) = 0.0;
	}
      }
    }
  }
}

void time_stepper::sanity_check(){
  CH_TIME("time_stepper::sanity_check");
  if(m_verbosity > 5){
    pout() << "time_stepper::sanity_check" << endl;
  }

  CH_assert(!m_compgeom.isNull());
  CH_assert(!m_physdom.isNull());
  CH_assert(!m_amr.isNull());
  CH_assert(!m_plaskin.isNull());
}

void time_stepper::set_amr(const RefCountedPtr<amr_mesh>& a_amr){
  CH_TIME("time_stepper::set_amr");
  if(m_verbosity > 5){
    pout() << "time_stepper::set_amr" << endl;
  }

  m_amr = a_amr;
}

void time_stepper::set_computational_geometry(const RefCountedPtr<computational_geometry>& a_compgeom){
  CH_TIME("time_stepper::set_computational_geometry");
  if(m_verbosity > 5){
    pout() << "time_stepper::set_computational_geometry" << endl;
  }

  m_compgeom = a_compgeom;
}

void time_stepper::set_plasma_kinetics(const RefCountedPtr<plasma_kinetics>& a_plaskin){
  CH_TIME("time_stepper::set_plasma_kinetics");
  if(m_verbosity > 5){
    pout() << "time_stepper::set_plasma_kinetics" << endl;
  }

  m_plaskin = a_plaskin;
}

void time_stepper::set_physical_domain(const RefCountedPtr<physical_domain>& a_physdom){
  CH_TIME("time_stepper::set_physical_domain");
  if(m_verbosity > 5){
    pout() << "time_stepper::set_physical_domain" << endl;
  }

  m_physdom = a_physdom;
}

void time_stepper::set_potential(Real (*a_potential)(const Real a_time)){
  CH_TIME("time_stepper::set_potential");
  if(m_verbosity > 5){
    pout() << "time_stepper::set_potential" << endl;
  }
  m_potential     = a_potential;
}

void time_stepper::set_poisson_wall_func(const int a_dir, const Side::LoHiSide a_side, Real (*a_func)(const RealVect a_pos)){
  CH_TIME("time_stepper::set_poisson_wall_func(dir, side, func)");
  if(m_verbosity > 4){
    pout() << "time_stepper::set_poisson_wall_func(dir, side, func)" << endl;
  }

  if(a_dir == 0){
    if(a_side == Side::Lo){
      m_wall_func_x_lo = a_func;
    }
    else if(a_side == Side::Hi){
      m_wall_func_x_hi = a_func;
    }
  }
  else if(a_dir == 1){
    if(a_side == Side::Lo){
      m_wall_func_y_lo = a_func;
    }
    else if(a_side == Side::Hi){
      m_wall_func_y_hi = a_func;
    }
  }
#if CH_SPACEDIM==3
  else if(a_dir == 2){
    if(a_side == Side::Lo){
      m_wall_func_z_lo = a_func;
    }
    else if(a_side == Side::Hi){
      m_wall_func_z_hi = a_func;
    }
  }
#endif
}

void time_stepper::set_verbosity(const int a_verbosity){
  m_verbosity = a_verbosity;

  ParmParse pp("time_stepper");
  pp.query("verbosity", m_verbosity);

  CH_TIME("time_stepper::set_verbosity");
  if(m_verbosity > 5){
    pout() << "time_stepper::set_verbosity" << endl;
  }
}

void time_stepper::set_solver_verbosity(const int a_verbosity){
  CH_TIME("time_stepper::set_solver_verbosity");
  if(m_verbosity > 5){
    pout() << "time_stepper::set_solver_verbosity" << endl;
  }

  m_solver_verbosity = a_verbosity;
  
  ParmParse pp("time_stepper");
  pp.query("solver_verbosity", m_solver_verbosity);

  if(!m_cdr.isNull()){
    m_cdr->set_verbosity(m_solver_verbosity);
  }
  if(!m_poisson.isNull()){
    m_poisson->set_verbosity(m_solver_verbosity);
  }
  if(!m_rte.isNull()){
    m_rte->set_verbosity(m_solver_verbosity);
  }
  if(!m_sigma.isNull()){
    m_sigma->set_verbosity(m_solver_verbosity);
  }
}

void time_stepper::set_stationary_rte(const bool a_stationary){
  CH_TIME("time_stepper::set_stationary_rte");
  if(m_verbosity > 5){
    pout() << "time_stepper::set_stationary_rte" << endl;
  }
}

void time_stepper::set_fast_rte(const int a_fast_rte){
  m_fast_rte = a_fast_rte;

  ParmParse pp("time_stepper");
  pp.query("fast_rte", m_fast_rte);
  if(m_fast_rte <= 0){
    m_fast_rte = a_fast_rte;
  }

}

void time_stepper::set_fast_poisson(const int a_fast_poisson){
  m_fast_poisson = a_fast_poisson;

  ParmParse pp("time_stepper");
  pp.query("fast_poisson", m_fast_poisson);
  if(m_fast_poisson <= 0){
    m_fast_poisson = a_fast_poisson;
  }
}

void time_stepper::set_min_dt(const Real a_min_dt){
  const Real zero = 0.0;
  m_min_dt = Max(a_min_dt, zero);

  ParmParse pp("time_stepper");
  pp.query("min_dt", m_min_dt);
  if(m_min_dt < 0.0){
    m_min_dt = a_min_dt;
  }
}

void time_stepper::set_max_dt(const Real a_max_dt){
  const Real zero = 0.0;
  m_max_dt = Max(a_max_dt, zero);

  ParmParse pp("time_stepper");
  pp.query("max_dt", m_max_dt);
  if(m_max_dt < 0.0){
    m_max_dt = a_max_dt;
  }
}

void time_stepper::set_cfl(const Real a_cfl){
  m_cfl = a_cfl;

  ParmParse pp("time_stepper");
  pp.query("cfl", m_cfl);
  if(m_cfl < 0.0){
    m_cfl = a_cfl;
  }
}

void time_stepper::set_relax_time(const Real a_relax_time){
  m_relax_time = a_relax_time;

  ParmParse pp("time_stepper");
  pp.query("relax_time", m_relax_time);
  if(m_relax_time < 0.0){
    m_relax_time = a_relax_time;
  }
}

void time_stepper::set_relax_level(const Real a_relax_level){
  m_relax_level = a_relax_level;

  ParmParse pp("time_stepper");
  pp.query("relax_level", m_relax_level);
}



void time_stepper::set_source_growth(const Real a_src_growth){
  m_src_growth = a_src_growth;

  ParmParse pp("time_stepper");
  pp.query("source_growth", m_src_growth);
  if(m_src_growth < 0.0){
    m_src_growth = a_src_growth;
  }
}

void time_stepper::set_source_growth_tolerance(const Real a_src_tolerance){
  m_src_tolerance = a_src_tolerance;

  ParmParse pp("time_stepper");
  pp.query("source_tolerance", m_src_tolerance);
  if(m_src_tolerance < 0.0){
    m_src_tolerance = a_src_tolerance;
  }
}

void time_stepper::set_source_growth_elec_only(const bool a_src_elec_only){
  m_src_elec_only = a_src_elec_only;

  ParmParse pp("time_stepper");
  std::string str;
  if(pp.contains("source_elec_only")){
    pp.get("source_elec_only", str);
    if(str == "true"){
      m_src_elec_only = true;
    }
    else if(str == "false"){
      m_src_elec_only = false;
    }
    else{
      MayDay::Abort("time_stepper::set_source_growth_elec_only - ParmParse can't get 'source_elec_only'");
    }
  }
}

void time_stepper::setup_cdr(){
  CH_TIME("time_stepper::setup_cdr");
  if(m_verbosity > 5){
    pout() << "time_stepper::setup_cdr" << endl;
  }

  m_cdr = RefCountedPtr<cdr_layout> (new cdr_layout(m_plaskin));
  m_cdr->set_verbosity(m_solver_verbosity);
  m_cdr->set_amr(m_amr);
  m_cdr->set_computational_geometry(m_compgeom);
  m_cdr->set_physical_domain(m_physdom);
  m_cdr->set_phase(phase::gas);
  m_cdr->sanity_check();
  m_cdr->allocate_internals();
}

void time_stepper::setup_poisson(){
  CH_TIME("time_stepper::setup_poisson");
  if(m_verbosity > 5){
    pout() << "time_stepper::setup_poisson" << endl;
  }

  m_poisson = RefCountedPtr<poisson_solver> (new poisson_multifluid_gmg());
  m_poisson->set_verbosity(m_solver_verbosity);
  m_poisson->set_amr(m_amr);
  m_poisson->set_computational_geometry(m_compgeom);
  m_poisson->set_physical_domain(m_physdom);

  m_poisson->set_poisson_wall_func(0, Side::Lo, m_wall_func_x_lo); // Set function-based Poisson on xlo
  m_poisson->set_poisson_wall_func(0, Side::Hi, m_wall_func_x_hi); // Set function-based Poisson on xhi
  m_poisson->set_poisson_wall_func(1, Side::Lo, m_wall_func_y_lo); // Set function-based Poisson on ylo
  m_poisson->set_poisson_wall_func(1, Side::Hi, m_wall_func_y_hi); // Set function-based Poisson on yhi
#if CH_SPACEDIM==3
  m_poisson->set_poisson_wall_func(2, Side::Lo, m_wall_func_z_lo); // Set function-based Poisson on zlo
  m_poisson->set_poisson_wall_func(2, Side::Hi, m_wall_func_z_hi); // Set function-based Poisson on zhi
#endif
  m_poisson->set_potential(m_potential); // Needs to happen AFTER set_poisson_wall_func

  m_poisson->sanity_check();
  m_poisson->allocate_internals();
}

void time_stepper::setup_rte(){
  CH_TIME("time_stepper::setup_rte");
  if(m_verbosity > 5){
    pout() << "time_stepper::setup_rte" << endl;
  }

  m_rte = RefCountedPtr<rte_layout> (new rte_layout(m_plaskin));
  m_rte->set_phase(phase::gas);
  m_rte->set_verbosity(m_solver_verbosity);
  m_rte->set_amr(m_amr);
  m_rte->set_computational_geometry(m_compgeom);
  m_rte->set_physical_domain(m_physdom);
  m_rte->sanity_check();
  m_rte->allocate_internals();
}

void time_stepper::setup_sigma(){
  CH_TIME("time_stepper::setup_sigma");
  if(m_verbosity > 5){
    pout() << "time_stepper::setup_sigma" << endl;
  }

  m_sigma = RefCountedPtr<sigma_solver> (new sigma_solver());
  m_sigma->set_amr(m_amr);
  m_sigma->set_verbosity(m_solver_verbosity);
  m_sigma->set_computational_geometry(m_compgeom);
  m_sigma->set_plasma_kinetics(m_plaskin);
  m_sigma->set_physical_domain(m_physdom);
  m_sigma->allocate_internals();
}

void time_stepper::solver_dump(){
  CH_TIME("time_stepper::solver_dump");
  if(m_verbosity > 5){
    pout() << "time_stepper::solver_dump" << endl;
  }

  m_cdr->write_plot_file();
  m_poisson->write_plot_file();
  m_rte->write_plot_file();
}

void time_stepper::solve_rte(const Real a_dt){
  CH_TIME("time_stepper::solve_rte()");
  if(m_verbosity > 5){
    pout() << "time_stepper::solve_rte()" << endl;
  }

  const phase::which_phase rte_phase = m_rte->get_phase();

  EBAMRCellData E;
  m_amr->allocate(E, rte_phase, SpaceDim);
  this->compute_E(E, rte_phase, m_poisson->get_state());

  Vector<EBAMRCellData*> states     = m_rte->get_states();
  Vector<EBAMRCellData*> rhs        = m_rte->get_sources();
  Vector<EBAMRCellData*> cdr_states = m_cdr->get_states();

  this->solve_rte(states, rhs, cdr_states, E, m_time, a_dt, centering::cell_center);
}

void time_stepper::solve_rte(Vector<EBAMRCellData*>&       a_rte_states,
			     Vector<EBAMRCellData*>&       a_rte_sources,
			     const Vector<EBAMRCellData*>& a_cdr_states,
			     const EBAMRCellData&          a_E,
			     const Real                    a_time,
			     const Real                    a_dt,
			     const centering::which_center a_centering){
  CH_TIME("time_stepper::solve_rte(full)");
  if(m_verbosity > 5){
    pout() << "time_stepper::solve_rte(full)" << endl;
  }

  //  const Real t0 = MPI_Wtime();
  this->compute_rte_sources(a_rte_sources, a_cdr_states, a_E, a_time, a_centering);
  //  const Real t1 = MPI_Wtime();

  for (rte_iterator solver_it(*m_rte); solver_it.ok(); ++solver_it){
    const int idx = solver_it.get_solver();
    
    RefCountedPtr<rte_solver>& solver = solver_it();
    EBAMRCellData& state              = *a_rte_states[idx];
    EBAMRCellData& rhs                = *a_rte_sources[idx];
    solver->advance(a_dt, state, rhs);
  }
  //  const Real t2 = MPI_Wtime();

  //  pout() << "source time = " << 100.*(t1-t0)/(t2-t0) << endl;
  //  pout() << "solve time = " << 100.*(t2-t1)/(t2-t0) << endl;
}

void time_stepper::synchronize_solver_times(const int a_step, const Real a_time, const Real a_dt){
  CH_TIME("time_stepper::synchronize_solver_times");
  if(m_verbosity > 5){
    pout() << "time_stepper::synchronize_solver_times" << endl;
  }

  m_step = a_step;
  m_time = a_time;
  m_dt   = a_dt;

  m_cdr->set_time(a_step,     a_time, a_dt);
  m_poisson->set_time(a_step, a_time, a_dt);
  m_rte->set_time(a_step,     a_time, a_dt);
  m_sigma->set_time(a_step,   a_time, a_dt);
}

Real time_stepper::compute_electrode_current(){
  CH_TIME("time_stepper::compute_electrode_current");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_electrode_current" << endl;
  }

  // Need to copy onto temporary storage because 
  EBAMRIVData charge_flux;
  m_amr->allocate(charge_flux, m_cdr->get_phase(), 1);
  data_ops::set_value(charge_flux, 0.0);
  
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    const RefCountedPtr<cdr_solver>& solver = solver_it();
    const RefCountedPtr<species>& spec      = solver_it.get_species();
    const EBAMRIVData& solver_flux          = solver->get_ebflux();

    data_ops::incr(charge_flux, solver_flux, spec->get_charge()*units::s_Qe);
  }

  this->reset_dielectric_cells(charge_flux);
  m_amr->conservative_average(charge_flux, m_cdr->get_phase());

  const int compute_lvl = 0;
  Real sum = 0.0;
  const Real dx = m_amr->get_dx()[compute_lvl];
  for (DataIterator dit = m_amr->get_grids()[compute_lvl].dataIterator(); dit.ok(); ++dit){
    const BaseIVFAB<Real>& flx = (*charge_flux[compute_lvl])[dit()];

    const IntVectSet ivs = flx.getIVS() & m_amr->get_grids()[compute_lvl].get(dit());
    for (VoFIterator vofit(ivs, flx.getEBGraph()); vofit.ok(); ++vofit){
      const VolIndex& vof   = vofit();
      const Real& bndryFrac = m_amr->get_ebisl(m_cdr->get_phase())[compute_lvl][dit()].bndryArea(vof);
      const Real& flux = flx(vof, 0);
      sum += flux*bndryFrac;
    }
  }

  sum *= pow(dx, SpaceDim-1);


#ifdef CH_MPI
  const Real sum1 = sum;
  sum = EBLevelDataOps::parallelSum(sum1);  
#endif

  return sum;
}

Real time_stepper::compute_dielectric_current(){
  CH_TIME("time_stepper::compute_dielectric_current");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_dielectric_current" << endl;
  }

  // Need to copy onto temporary storage because 
  EBAMRIVData charge_flux;
  m_amr->allocate(charge_flux, m_cdr->get_phase(), 1);
  data_ops::set_value(charge_flux, 0.0);
  
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    const RefCountedPtr<cdr_solver>& solver = solver_it();
    const RefCountedPtr<species>& spec      = solver_it.get_species();
    const EBAMRIVData& solver_flux          = solver->get_ebflux();

    data_ops::incr(charge_flux, solver_flux, spec->get_charge()*units::s_Qe);
  }

  m_sigma->reset_cells(charge_flux);
  m_amr->conservative_average(charge_flux, m_cdr->get_phase());

  const int compute_lvl = 0;
  Real sum = 0.0;
  const Real dx = m_amr->get_dx()[compute_lvl];
  for (DataIterator dit = m_amr->get_grids()[compute_lvl].dataIterator(); dit.ok(); ++dit){
    const BaseIVFAB<Real>& flx = (*charge_flux[compute_lvl])[dit()];

    const IntVectSet ivs = flx.getIVS() & m_amr->get_grids()[compute_lvl].get(dit());
    for (VoFIterator vofit(ivs, flx.getEBGraph()); vofit.ok(); ++vofit){
      const VolIndex& vof   = vofit();
      const Real& bndryFrac = m_amr->get_ebisl(m_cdr->get_phase())[compute_lvl][dit()].bndryArea(vof);
      const Real& flux = flx(vof, 0);
      sum += flux*bndryFrac;
    }
  }

  sum *= pow(dx, SpaceDim-1);


#ifdef CH_MPI
  const Real sum1 = sum;
  sum = EBLevelDataOps::parallelSum(sum1);  
#endif

  return sum;
}

Real time_stepper::compute_domain_current(){
  CH_TIME("time_stepper::compute_domain_current");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_domain_current" << endl;
  }

  const int comp = 0;

  // Need to copy onto temporary storage because 
  EBAMRIFData charge_flux;
  m_amr->allocate(charge_flux, m_cdr->get_phase(), 1);
  data_ops::set_value(charge_flux, 0.0);
  
  for (cdr_iterator solver_it = m_cdr->iterator(); solver_it.ok(); ++solver_it){
    const RefCountedPtr<cdr_solver>& solver = solver_it();
    const RefCountedPtr<species>& spec      = solver_it.get_species();
    const EBAMRIFData& solver_flux          = solver->get_domainflux();

    data_ops::incr(charge_flux, solver_flux, spec->get_charge()*units::s_Qe);
  }

  const int compute_lvl = 0;
  Real sum = 0.0;
  const Real dx = m_amr->get_dx()[compute_lvl];
  for (DataIterator dit = m_amr->get_grids()[compute_lvl].dataIterator(); dit.ok(); ++dit){
    const DomainFluxIFFAB& flux = (*charge_flux[compute_lvl])[dit()];

    for (int dir = 0; dir < SpaceDim; dir++){
      for (SideIterator sit; sit.ok(); ++sit){
	const BaseIFFAB<Real>& fluxdir = flux(dir, sit());

	FaceStop::WhichFaces stopcrit = FaceStop::AllBoundaryOnly;
	const IntVectSet& ivs  = fluxdir.getIVS();
	const EBGraph& ebgraph = fluxdir.getEBGraph();

	for (FaceIterator faceit(ivs, ebgraph, dir, stopcrit); faceit.ok(); ++faceit){
	  sum += sign(sit())*fluxdir(faceit(), comp);
	}
      }
    }
  }

  sum *= pow(dx, SpaceDim-1);


#ifdef CH_MPI
  const Real sum1 = sum;
  sum = EBLevelDataOps::parallelSum(sum1);  
#endif

  return sum;
}

Real time_stepper::compute_ohmic_induction_current(){
  CH_TIME("time_stepper::compute_relaxation_time");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_relaxation_time" << endl;
  }

  Real current = 0.0;

  EBAMRCellData J, E, JdotE;
  m_amr->allocate(J, m_cdr->get_phase(), SpaceDim);
  m_amr->allocate(E, m_cdr->get_phase(), SpaceDim);
  m_amr->allocate(JdotE, m_cdr->get_phase(), SpaceDim);

  this->compute_E(E, m_cdr->get_phase(), m_poisson->get_state());
  this->compute_J(J);

  // Compute J.dot.E 
  data_ops::dot_prod(JdotE, J,E);
  m_amr->average_down(JdotE, m_cdr->get_phase());

  // Only compue on coarsest level
  const int coar = 0;
  const Real dx  = m_amr->get_dx()[coar];
  data_ops::kappa_sum(current, *JdotE[coar]);
  current *= pow(dx, SpaceDim);

  return current;
}

Real time_stepper::compute_relaxation_time(){
  CH_TIME("time_stepper::compute_relaxation_time");
  if(m_verbosity > 5){
    pout() << "time_stepper::compute_relaxation_time" << endl;
  }

  const int comp         = 0;
  const int finest_level = 0;
  const Real SAFETY      = 1.E-20;

  Real t1 = MPI_Wtime();
  EBAMRCellData E, J, dt;
  m_amr->allocate(E,  m_cdr->get_phase(), SpaceDim);
  m_amr->allocate(J,  m_cdr->get_phase(), SpaceDim);
  m_amr->allocate(dt, m_cdr->get_phase(), 1);

  data_ops::set_value(dt, 1.234567E89);

  this->compute_E(E, m_cdr->get_phase(), m_poisson->get_state());
  this->compute_J(J);

  // Find the largest electric field in each direction
  Vector<Real> max_E(SpaceDim);
  for (int dir = 0; dir < SpaceDim; dir++){

    Real max, min;
    data_ops::get_max_min(max, min, E, dir);
    max_E[dir] = Max(Abs(max), Abs(min));
  }

  const int finest_relax_level = (m_relax_level < 0) ? finest_level : Min(m_relax_level, finest_level);
  
  for (int lvl = 0; lvl <= finest_relax_level; lvl++){
    const DisjointBoxLayout& dbl = m_amr->get_grids()[lvl];
    const EBISLayout& ebisl      = m_amr->get_ebisl(m_cdr->get_phase())[lvl];

    for (DataIterator dit = dbl.dataIterator(); dit.ok(); ++dit){
      const Box& box         = dbl.get(dit());
      const EBISBox& ebisbox = ebisl[dit()];
      const EBGraph& ebgraph = ebisbox.getEBGraph();
      const IntVectSet ivs(box);
      
      EBCellFAB& dt_fab  = (*dt[lvl])[dit()];
      const EBCellFAB& e = (*E[lvl])[dit()];
      const EBCellFAB& j = (*J[lvl])[dit()];

#if 1 // Optimized code
      EBCellFAB e_magnitude(ebisbox, box, 1);
      EBCellFAB j_magnitude(ebisbox, box, 1);

      // Compute magnitudes, increment with safety factor to avoid division by zero
      e_magnitude.setVal(0.0);
      j_magnitude.setVal(0.0);
      
      data_ops::vector_length(e_magnitude, e, box);
      data_ops::vector_length(j_magnitude, j, box);
      j_magnitude += SAFETY;      

      dt_fab.setVal(units::s_eps0);
      dt_fab *= e_magnitude;
      dt_fab /= j_magnitude;

      for (VoFIterator vofit(ebisbox.getIrregIVS(box), ebgraph); vofit.ok(); ++vofit){
	const VolIndex& vof = vofit();
	const RealVect ee = RealVect(D_DECL(e(vof, 0), e(vof, 1), e(vof, 2)));
	const RealVect jj = RealVect(D_DECL(j(vof, 0), j(vof, 1), j(vof, 2)));

	dt_fab(vof, comp) = Abs(units::s_eps0*ee.vectorLength()/(1.E-20 + jj.vectorLength()));
      }
#else // Original code
      for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){
	const VolIndex& vof = vofit();
	const RealVect ee = RealVect(D_DECL(e(vof, 0), e(vof, 1), e(vof, 2)));
	const RealVect jj = RealVect(D_DECL(j(vof, 0), j(vof, 1), j(vof, 2)));

	dt_fab(vof, comp) = Abs(units::s_eps0*ee.vectorLength()/(1.E-20 + jj.vectorLength()));
      }
#endif
    }
  }

  // Find the smallest dt
  Real min_dt = 1.E99;
  Real max, min;
  data_ops::get_max_min(max, min, dt, comp);
  min_dt = Min(min_dt, min);

  return min_dt;
}

Real time_stepper::get_time(){
  return m_time;
}

Real time_stepper::get_dt(){
  return m_dt;
}


RefCountedPtr<cdr_layout>& time_stepper::get_cdr(){
  return m_cdr;
}

RefCountedPtr<poisson_solver>& time_stepper::get_poisson(){
  return m_poisson;
}

RefCountedPtr<rte_layout>& time_stepper::get_rte(){
  return m_rte;
}

RefCountedPtr<sigma_solver>& time_stepper::get_sigma(){
  return m_sigma;
}
