/*!
  @file data_ops.cpp
  @brief Implementation of data_ops.H
  @author Robert Marskar
  @date Nov. 2017
*/

#include "data_ops.H"
#include "EBLevelDataOps.H"
#include "MFLevelDataOps.H"

void data_ops::average_cell_to_face(EBAMRFluxData&               a_facedata,
				    const EBAMRCellData&         a_celldata,
				    const Vector<ProblemDomain>& a_domains){
  for (int lvl = 0; lvl < a_facedata.size(); lvl++){
    data_ops::average_cell_to_face(*a_facedata[lvl], *a_celldata[lvl], a_domains[lvl]);
  }
}

void data_ops::average_cell_to_face(LevelData<EBFluxFAB>&       a_facedata,
				    const LevelData<EBCellFAB>& a_celldata,
				    const ProblemDomain&        a_domain){

  CH_assert(a_facedata.nComp() == 1);
  CH_assert(a_celldata.nComp() == SpaceDim);
  
  for (DataIterator dit = a_facedata.dataIterator(); dit.ok(); ++dit){
    EBFluxFAB& flux_vel       = a_facedata[dit()];
    const EBCellFAB& cell_vel = a_celldata[dit()];
    const EBISBox& ebisbox    = cell_vel.getEBISBox();
    const EBGraph& ebgraph    = ebisbox.getEBGraph();
    const Box& box            = a_celldata.disjointBoxLayout().get(dit());
    
    for (int dir = 0; dir < SpaceDim; dir++){
      EBLevelDataOps::averageCellToFace(flux_vel[dir], cell_vel, ebgraph, box, dir, dir, a_domain, dir, 0);
    }
  }
}

void data_ops::incr(EBAMRCellData& a_lhs, const EBAMRCellData& a_rhs, const Real& a_scale){
  for (int lvl = 0; lvl < a_lhs.size(); lvl++){
    data_ops::incr(*a_lhs[lvl], *a_rhs[lvl], a_scale);
  }
}

void data_ops::incr(LevelData<EBCellFAB>& a_lhs, const LevelData<EBCellFAB>& a_rhs, const Real& a_scale){
  EBLevelDataOps::incr(a_lhs, a_rhs, a_scale);
}

void data_ops::copy(EBAMRCellData& a_dst, const EBAMRCellData& a_src){
  for (int lvl = 0; lvl < a_dst.size(); lvl++){
    if(a_src[lvl] != NULL && a_dst[lvl] != NULL){
      a_src[lvl]->copyTo(*a_dst[lvl]);
    }
  }
}

void data_ops::exponentiate(EBAMRCellData& a_lhs, const Real a_factor){
  for (int lvl = 0; lvl <= a_lhs.size(); lvl++){
    data_ops::exponentiate(*a_lhs[lvl], a_factor);
  }
}

void data_ops::exponentiate(LevelData<EBCellFAB>& a_lhs, const Real a_factor){
  for (DataIterator dit = a_lhs.dataIterator(); dit.ok(); ++dit){
    EBCellFAB& lhs         = a_lhs[dit()];
    const Box box          = a_lhs.disjointBoxLayout().get(dit());
    const EBISBox& ebisbox = lhs.getEBISBox();
    const EBGraph& ebgraph = ebisbox.getEBGraph();
    const IntVectSet ivs(box);

    
    for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){
      const VolIndex& vof = vofit();
      for (int comp = 0; comp < lhs.nComp(); comp++){
	const Real value = lhs(vof, comp);
	lhs(vof,comp) = exp(a_factor*value);
      }
    }
  }
}

void data_ops::scale(MFAMRCellData& a_lhs, const Real& a_scale){
  for (int lvl = 0; lvl < a_lhs.size(); lvl++){
    MFLevelDataOps::scale(*a_lhs[lvl], a_scale);
  }
}

void data_ops::scale(EBAMRIVData& a_lhs, const Real& a_scale){
  for (int lvl = 0; lvl < a_lhs.size(); lvl++){
    data_ops::scale(*a_lhs[lvl], a_scale);
  }
}

void data_ops::scale(EBAMRCellData& a_lhs, const Real a_scale){
  for (int lvl = 0; lvl < a_lhs.size(); lvl++){
    EBLevelDataOps::scale(*a_lhs[lvl], a_scale);
  }
}

void data_ops::scale(EBAMRFluxData& a_lhs, const Real a_scale){
  for (int lvl = 0; lvl < a_lhs.size(); lvl++){
    EBLevelDataOps::scale(*a_lhs[lvl], a_scale);
  }
}

void data_ops::scale(LevelData<BaseIVFAB<Real> >& a_lhs, const Real& a_scale){
  for (DataIterator dit = a_lhs.dataIterator(); dit.ok(); ++dit){
    BaseIVFAB<Real>& lhs = a_lhs[dit()];

    for (VoFIterator vofit(lhs.getIVS(), lhs.getEBGraph()); vofit.ok(); ++vofit){
      for (int comp = 0; comp < a_lhs.nComp(); comp++){
	lhs(vofit(), comp) *= a_scale;
      }
    }
  }
}

void data_ops::divide(EBAMRCellData& a_lhs, const EBAMRCellData& a_rhs, const int a_lcomp, const int a_rcomp){
  for (int lvl = 0; lvl < a_lhs.size(); lvl++){
    data_ops::divide(*a_lhs[lvl], *a_rhs[lvl], a_lcomp, a_rcomp);
  }
}

void data_ops::divide(LevelData<EBCellFAB>& a_lhs, const LevelData<EBCellFAB>& a_rhs, const int a_lcomp, const int a_rcomp){
  for (DataIterator dit = a_lhs.dataIterator(); dit.ok(); ++dit){
    EBCellFAB& lhs       = a_lhs[dit()];
    const EBCellFAB& rhs = a_rhs[dit()];

    lhs.divide(rhs, a_rcomp, a_lcomp, 1);
  }
}

void data_ops::divide_scalar(EBAMRCellData& a_lhs, const EBAMRCellData& a_rhs){
  for (int lvl = 0; lvl < a_lhs.size(); lvl++){
    data_ops::divide_scalar(*a_lhs[lvl], *a_rhs[lvl]);
  }
}

void data_ops::divide_scalar(LevelData<EBCellFAB>& a_lhs, const LevelData<EBCellFAB>& a_rhs){
  const int lcomps = a_lhs.nComp();
  const int rcomps = a_rhs.nComp();
  
  CH_assert(a_rhs.nComp() == 1);
  CH_assert(a_lhs.nComp() >= 1);

  for (int comp = 0; comp < lcomps; comp++){
    data_ops::divide(a_lhs, a_rhs, comp, 0);
  }
}

void data_ops::floor(EBAMRCellData& a_lhs, const Real a_value){
  for (int lvl = 0; lvl < a_lhs.size(); lvl++){
    data_ops::floor(*a_lhs[lvl], a_value);
  }
}

void data_ops::floor(LevelData<EBCellFAB>& a_lhs, const Real a_value){
  for (DataIterator dit = a_lhs.dataIterator(); dit.ok(); ++dit){
    EBCellFAB& lhs = a_lhs[dit()];
    const Box box          = a_lhs.disjointBoxLayout().get(dit());
    const EBISBox& ebisbox = lhs.getEBISBox();
    const EBGraph& ebgraph = ebisbox.getEBGraph();
    const IntVectSet ivs(lhs.getRegion());

    for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){
      const VolIndex& vof = vofit();
      for (int comp = 0; comp < a_lhs.nComp(); comp++){
	const Real value = lhs(vof, comp);
	lhs(vof, comp) = Max(value, a_value);
      }
    }
  }
}

void data_ops::kappa_sum(Real& a_mass, const LevelData<EBCellFAB>& a_lhs){


  Real mass = 0.;

  CH_assert(a_lhs.nComp() == 1);
  
  const int comp  = 0;
  const int ncomp = 1;
  
  for (DataIterator dit = a_lhs.dataIterator(); dit.ok(); ++dit){
    const Box box          = a_lhs.disjointBoxLayout().get(dit());
    const EBCellFAB& lhs   = a_lhs[dit()];
    const EBISBox& ebisbox = lhs.getEBISBox();
    const EBGraph& ebgraph = ebisbox.getEBGraph();
    const IntVectSet ivs(box);
    
    for (VoFIterator vofit(ivs, ebgraph); vofit.ok(); ++vofit){
      const VolIndex& vof = vofit();

      mass += ebisbox.volFrac(vof)*lhs(vof, comp);
    }
  }

  a_mass = EBLevelDataOps::parallelSum(mass);
}

void data_ops::multiply(EBAMRCellData& a_lhs, const EBAMRCellData& a_rhs){
  for (int lvl = 0; lvl < a_lhs.size(); lvl++){
    data_ops::multiply(*a_lhs[lvl], *a_rhs[lvl]);
  }
}

void data_ops::multiply(LevelData<EBCellFAB>& a_lhs, const LevelData<EBCellFAB>& a_rhs){
  for (DataIterator dit = a_lhs.dataIterator(); dit.ok(); ++dit){
    a_lhs[dit()] *= a_rhs[dit()];
  }
}

void data_ops::set_value(EBAMRCellData& a_data, const Real& a_value){
  for (int lvl = 0; lvl < a_data.size(); lvl++){
    EBLevelDataOps::setVal(*a_data[lvl], a_value);
  }
}

void data_ops::set_value(EBAMRCellData& a_lhs, const Real a_value, const int a_comp){
  for (int lvl = 0; lvl < a_lhs.size(); lvl++){
    data_ops::set_value(*a_lhs[lvl], a_value, a_comp);
  }
}

void data_ops::set_value(LevelData<EBCellFAB>& a_lhs, const Real a_value, const int a_comp){
  EBLevelDataOps::setVal(a_lhs, a_value, a_comp);
}

void data_ops::set_value(LevelData<EBFluxFAB>& a_lhs, const Real a_value){
  EBLevelDataOps::setVal(a_lhs, a_value);
}
  
void data_ops::set_value(LevelData<BaseIVFAB<Real> >& a_lhs, const Real a_value){
  EBLevelDataOps::setVal(a_lhs, a_value);
}

void data_ops::set_value(EBAMRFluxData& a_data, const Real& a_value){
  for (int lvl = 0; lvl < a_data.size(); lvl++){
    EBLevelDataOps::setVal(*a_data[lvl], a_value);
  }
}

void data_ops::set_value(EBAMRIVData& a_data, const Real& a_value){
  for (int lvl = 0; lvl < a_data.size(); lvl++){
    EBLevelDataOps::setVal(*a_data[lvl], a_value);
  }
}

void data_ops::set_value(MFAMRCellData& a_lhs, const Real& a_value){
  for (int lvl = 0; lvl < a_lhs.size(); lvl++){
    data_ops::set_value(*a_lhs[lvl], a_value);
  }
}

void data_ops::set_value(LevelData<MFCellFAB>& a_lhs, const Real& a_value){
  for (DataIterator dit = a_lhs.dataIterator(); dit.ok(); ++dit){
    MFCellFAB& lhs = a_lhs[dit()];
    lhs.setVal(a_value);
  }
}

void data_ops::set_value(MFAMRFluxData& a_lhs, const Real& a_value){
  for (int lvl = 0; lvl < a_lhs.size(); lvl++){
    data_ops::set_value(*a_lhs[lvl] , a_value);
  }
}

void data_ops::set_value(LevelData<MFFluxFAB>& a_lhs, const Real& a_value){
  for (DataIterator dit = a_lhs.dataIterator(); dit.ok(); ++dit){
    MFFluxFAB& lhs = a_lhs[dit()];
    lhs.setVal(a_value);
  }
}

void data_ops::set_value(MFAMRIVData& a_lhs, const Real& a_value){
  for (int lvl = 0; lvl < a_lhs.size(); lvl++){
    data_ops::set_value(*a_lhs[lvl] , a_value);
  }
}

void data_ops::set_value(LevelData<MFBaseIVFAB>& a_lhs, const Real& a_value){
  for (DataIterator dit = a_lhs.dataIterator(); dit.ok(); ++dit){
    a_lhs[dit()].setVal(a_value);
  }
}
