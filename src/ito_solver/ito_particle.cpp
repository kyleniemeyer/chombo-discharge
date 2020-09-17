/*!
  @file   ito_particle.cpp
  @brief  Implementation of ito_particle.H
  @author Robert Marskar
  @date   April 2020
*/

#include "ito_particle.H"

ito_particle::ito_particle() : BinItem(){
}


ito_particle::ito_particle(const Real      a_mass,
			   const RealVect& a_position,
			   const RealVect& a_velocity,
			   const Real      a_diffusion,
			   const Real      a_mobility,
			   const Real      a_energy) : BinItem(a_position){
  m_mass        = a_mass;
  m_velocity    = a_velocity;
  m_diffusion   = a_diffusion;
  m_oldPosition = a_position;
  m_mobility    = a_mobility;
  m_energy      = a_energy;
}

ito_particle::~ito_particle(){

}


void ito_particle::define(const Real      a_mass,
			  const RealVect& a_position,
			  const RealVect& a_velocity,
			  const Real      a_diffusion,
			  const Real      a_mobility,
			  const Real      a_energy){
  setMass(a_mass);
  setPosition(a_position);
  setVelocity(a_velocity);
  setDiffusion(a_diffusion);
  setMobility(a_mobility);
  setEnergy(a_energy);
}

void ito_particle::setOldPosition(const RealVect a_oldPosition){
  m_oldPosition = a_oldPosition;
}

RealVect& ito_particle::oldPosition(){
  return m_oldPosition;
}


const RealVect& ito_particle::oldPosition() const{
  return m_oldPosition;
}

void ito_particle::setMass(const Real a_mass){
  m_mass = a_mass;
}

Real& ito_particle::mass(){
  return m_mass;
}

const Real& ito_particle::mass() const{
  return m_mass;
}

void ito_particle::setDiffusion(const Real a_diffusion){
  m_diffusion = a_diffusion;
}

Real& ito_particle::diffusion(){
  return m_diffusion;
}

const Real& ito_particle::diffusion() const{
  return m_diffusion;
}

void ito_particle::setMobility(const Real a_mobility){
  m_mobility = a_mobility;
}


Real& ito_particle::mobility(){
  return m_mobility;
}


const Real& ito_particle::mobility() const{
  return m_mobility;
}

void ito_particle::setEnergy(const Real a_energy){
  m_energy = a_energy;
}

Real& ito_particle::energy(){
  return m_energy;
}

const Real& ito_particle::energy() const{
  return m_energy;
}

void ito_particle::setVelocity(const RealVect& a_velocity){
  m_velocity = a_velocity;
}

void ito_particle::setVelocity(const Real& a_velocity, const int a_dir){
  m_velocity[a_dir] = a_velocity;
}

RealVect& ito_particle::velocity(){
  return m_velocity;
}

const RealVect& ito_particle::velocity() const{
  return m_velocity;
}

Real ito_particle::velocity(const int a_dir) const{
  return m_velocity[a_dir];
}

Real& ito_particle::tmp(){
  return m_tmp;
}

const Real& ito_particle::tmp() const{
  return m_tmp;
}

bool ito_particle::operator==(const ito_particle& a_p) const{
  return ( m_mass      == a_p.m_mass     &&
           m_position  == a_p.m_position &&
           m_velocity  == a_p.m_velocity &&
	   m_diffusion == a_p.m_diffusion);
}

bool ito_particle::operator==(const ito_particle* a_p) const{
  return (*this == *a_p);
}

bool ito_particle::operator!=(const ito_particle& a_p) const{
  return !(*this == a_p);
}

int ito_particle::size() const{
  return ( BinItem::size() + sizeof(m_mass) + sizeof(m_velocity) + sizeof(m_diffusion) + sizeof(m_oldPosition) + sizeof(m_mobility) + sizeof(m_energy));
}

void ito_particle::linearOut(void* buf) const{
  Real* buffer = (Real*)buf;
  D_TERM6( *buffer++ = m_position[0];,
	   *buffer++ = m_position[1];,
	   *buffer++ = m_position[2];,
	   *buffer++ = m_position[3];,
	   *buffer++ = m_position[4];,
	   *buffer++ = m_position[5];);

  D_TERM6( *buffer++ = m_velocity[0];,
	   *buffer++ = m_velocity[1];,
	   *buffer++ = m_velocity[2];,
	   *buffer++ = m_velocity[3];,
	   *buffer++ = m_velocity[4];,
	   *buffer++ = m_velocity[5];);

  D_TERM6( *buffer++ = m_oldPosition[0];,
	   *buffer++ = m_oldPosition[1];,
	   *buffer++ = m_oldPosition[2];,
	   *buffer++ = m_oldPosition[3];,
	   *buffer++ = m_oldPosition[4];,
	   *buffer++ = m_oldPosition[5];);

  *buffer++ = m_diffusion;
  *buffer++ = m_mass;
  *buffer++ = m_mobility;
  *buffer   = m_energy;
}

void ito_particle::linearIn(void* buf){
  Real* buffer = (Real*)buf;
  D_TERM6( m_position[0] = *buffer++;,
	   m_position[1] = *buffer++;,
	   m_position[2] = *buffer++;,
	   m_position[3] = *buffer++;,
	   m_position[4] = *buffer++;,
	   m_position[5] = *buffer++;);

  D_TERM6( m_velocity[0] = *buffer++;,
	   m_velocity[1] = *buffer++;,
	   m_velocity[2] = *buffer++;,
	   m_velocity[3] = *buffer++;,
	   m_velocity[4] = *buffer++;,
	   m_velocity[5] = *buffer++;);

  D_TERM6( m_oldPosition[0] = *buffer++;,
	   m_oldPosition[1] = *buffer++;,
	   m_oldPosition[2] = *buffer++;,
	   m_oldPosition[3] = *buffer++;,
	   m_oldPosition[4] = *buffer++;,
	   m_oldPosition[5] = *buffer++;);

  m_diffusion = *buffer++;
  m_mass      = *buffer++;
  m_mobility  = *buffer++;
  m_energy    = *buffer;
}

std::ostream & operator<<(std::ostream& ostr, const ito_particle& p){
  ostr << " ito_particle : " << std::endl;
  ostr << " mass " << p.mass() << std::endl;
  ostr << " position ( ";
  for ( int i=0; i<SpaceDim; ++i ){ ostr << " " << p.position(i); }
  ostr << " ) ";
  ostr << std::endl << " velocity ( ";
  for ( int i=0; i<SpaceDim; ++i ){ ostr << " " << p.velocity(i); }
  ostr << " ) ";
  ostr << std::endl << " diffusion " << p.diffusion() << std::endl;
  ostr << std::endl << " mobility " << p.mobility() << std::endl;
  ostr << std::endl << " energy " << p.energy() << std::endl;
  return ostr;
}
