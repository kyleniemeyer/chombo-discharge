/*!
  @file   massive_particle.H
  @brief  Implementation of massive_particle.H
  @author Robert Marskar
  @date   Sept. 2020
*/

#include "massive_particle.H"

massive_particle::massive_particle() : BinItem(){
}


massive_particle::massive_particle(const Real a_mass, const RealVect a_position, const Real a_energy) : BinItem(a_position) {
  m_mass   = a_mass;
  m_energy = a_energy;
}

massive_particle::~massive_particle(){

}


void massive_particle::define(const Real a_mass, const RealVect a_position, const Real a_energy){
  setMass(a_mass);
  setPosition(a_position);
  setEnergy(a_energy);
}

void massive_particle::setMass(const Real a_mass){
  m_mass = a_mass;
}

Real& massive_particle::mass(){
  return m_mass;
}

const Real& massive_particle::mass() const{
  return m_mass;
}

void massive_particle::setEnergy(const Real a_energy){
  m_energy = a_energy;
}

Real& massive_particle::energy(){
  return m_energy;
}

const Real& massive_particle::energy() const{
  return m_energy;
}

bool massive_particle::operator==(const massive_particle& a_p) const{
  return ( m_mass      == a_p.m_mass     &&
	   m_energy    == a_p.m_energy   &&
           m_position  == a_p.m_position);
}

bool massive_particle::operator==(const massive_particle* a_p) const{
  return (*this == *a_p);
}

bool massive_particle::operator!=(const massive_particle& a_p) const{
  return !(*this == a_p);
}

int massive_particle::size() const{
  return ( BinItem::size() + sizeof(m_mass) + sizeof(m_energy));
}

void massive_particle::linearOut(void* buf) const{
  Real* buffer = (Real*)buf;
  D_TERM6( *buffer++ = m_position[0];,
	   *buffer++ = m_position[1];,
	   *buffer++ = m_position[2];,
	   *buffer++ = m_position[3];,
	   *buffer++ = m_position[4];,
	   *buffer++ = m_position[5];);

  *buffer++ = m_mass;
  *buffer++ = m_energy;
}

void massive_particle::linearIn(void* buf){
  Real* buffer = (Real*)buf;
  D_TERM6( m_position[0] = *buffer++;,
	   m_position[1] = *buffer++;,
	   m_position[2] = *buffer++;,
	   m_position[3] = *buffer++;,
	   m_position[4] = *buffer++;,
	   m_position[5] = *buffer++;);

  m_mass   = *buffer++;
  m_energy = *buffer++;
}

std::ostream & operator<<(std::ostream& ostr, const massive_particle& p){
  ostr << " massive_particle : " << std::endl;
  ostr << " mass " << p.mass() << std::endl;
  ostr << " position ( ";
  for ( int i=0; i<SpaceDim; ++i ){ ostr << " " << p.position(i); }
  ostr << " ) ";
  ostr << " energy " << p.energy() << std::endl;
  return ostr;
}
