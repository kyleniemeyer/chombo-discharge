/*!
  @file perlin_plane_if.cpp
  @brief Implementation of perlin_plane_if.hpp
  @author Robert Marskar
  @date Sep. 2017
*/

#include "perlin_if.H"
#include "perlin_plane_if.H"

#include <PlaneIF.H>
#include <PolyGeom.H>

perlin_plane_if::perlin_plane_if(const RealVect& a_normal,
				 const RealVect& a_point,
				 const bool&     a_inside,
				 const Real&     a_noiseAmp,
				 const RealVect& a_noiseFreq,
				 const Real&     a_persistence,
				 const int&      a_octaves,
				 const bool&     a_reseed){
  
  // Create plane Perlin noise
  m_point  = a_point;
  m_normal = a_normal;
  m_plane  = RefCountedPtr<BaseIF> (new PlaneIF(a_normal, a_point, a_inside));
  m_perlin = RefCountedPtr<BaseIF> (new perlin_if(a_noiseAmp, a_noiseFreq, a_persistence, a_octaves, a_reseed));
  
}

perlin_plane_if::perlin_plane_if(const perlin_plane_if& a_inputIF){
  m_normal = a_inputIF.m_normal;
  m_point  = a_inputIF.m_point;
  m_plane  = a_inputIF.m_plane;
  m_perlin = a_inputIF.m_perlin;
}

perlin_plane_if::~perlin_plane_if(){

}

Real perlin_plane_if::value(const RealVect& a_pos) const {

  const RealVect x0 = m_point;
  const RealVect x1 = a_pos;
  const RealVect xp = x1 - PolyGeom::dot((x1-x0), m_normal)*m_normal;

  return m_plane->value(a_pos) + m_perlin->value(xp);
}

BaseIF* perlin_plane_if::newImplicitFunction() const {
  return static_cast<BaseIF*> (new perlin_plane_if(*this));
}