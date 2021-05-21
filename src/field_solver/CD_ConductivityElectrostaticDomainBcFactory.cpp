/*!
  @file   CD_ConductivityElectrostaticDomainBcFactory.cpp
  @brief  Implementation of ConductivityElectrostaticDomainBcFactory.H
  @author Robert Marskar
  @date   May 2021
*/

#include "CD_ConductivityElectrostaticDomainBcFactory.H"
#include "CD_NamespaceHeader.H"

ConductivityElectrostaticDomainBcFactory::ConductivityElectrostaticDomainBcFactory(const ElectrostaticDomainBc& a_domainBc, const RealVect a_probLo){
  m_domainBc = a_domainBc;
  m_probLo   = a_probLo;
}

ConductivityElectrostaticDomainBcFactory::~ConductivityElectrostaticDomainBcFactory(){

}

ConductivityElectrostaticDomainBc* ConductivityElectrostaticDomainBcFactory::create(const ProblemDomain& a_domain, const EBISLayout& a_ebisl, const RealVect& a_dx) {
  return new ConductivityElectrostaticDomainBc(m_domainBc, m_probLo);
}

#include "CD_NamespaceFooter.H"