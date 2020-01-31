#ifdef CH_LANG_CC
/*
 *      _______              __
 *     / ___/ /  ___  __ _  / /  ___
 *    / /__/ _ \/ _ \/  V \/ _ \/ _ \
 *    \___/_//_/\___/_/_/_/_.__/\___/
 *    Please refer to Copyright.txt, in Chombo's root directory.
 */
#endif

#include <cmath>

#include "LoHiSide.H"
#include "BoxIterator.H"
#include "CompactSupportIBC.H"
#include "FourthOrderUtil.H"
#include "CoarseAverage.H"

// Null constructor
CompactSupportIBC::CompactSupportIBC()
{
    m_params_are_set = false;
}

// Sets parameters used by initial conditions
void CompactSupportIBC::setParams(Real a_mag,
                                  Real a_width,
                                  RealVect a_center)
{
   CH_assert(m_params_are_set == false);

   m_mag = a_mag;
   m_width = a_width;
   m_center = a_center;

   m_params_are_set = true;
}

/// set uniform velocity field
void
CompactSupportIBC::setUniformVel(const RealVect& a_vel)
{
   m_velType = UNIFORM;
   m_uniformVel = a_vel;
}

// set parameter for solid-body rotation
void
CompactSupportIBC::setSolidBodyRotation(const RealVect& a_rotationCenter,
                                        const Real a_omega)
{
  m_velType = SOLIDBODY;
  m_rotationCenter = a_rotationCenter;
  m_omega = a_omega;
}

// set parameters for translating-oscilating velocity
void
CompactSupportIBC::setTranslatingOscillation(
   const RealVect& a_translation_vel,
   const Real a_oscillation_amplitude)
{
  m_velType = TRANSOSC;
  m_uniformVel = a_translation_vel;
  m_oscAmp = a_oscillation_amplitude;
}

// Factory method - this object is its own factory:
//   Return a pointer to a new BasicIBC object with m_isDefined = false (i.e.,
//   its define() must be called before it is used) and m_isFortranCommonSet
//   set to value of m_isFortranCommonset in the current (factory) object.
BasicIBC* CompactSupportIBC::new_basicIBC()
{
  CompactSupportIBC* retval = new CompactSupportIBC();
  retval -> setParams(m_mag, m_width, m_center);
  //[NOTE: do this even though setParams() will set it true
  //       because it might not have been set in the source
  //       object (this).  Of course, that would be a bad idea
  //       because then any object created by the factor wont
  //       be usable.  Caveat usor.  -dbs]

  retval ->m_velType = m_velType;
  retval->m_uniformVel = m_uniformVel;
  retval->m_omega = m_omega;
  retval->m_oscAmp = m_oscAmp;
  retval->m_rotationCenter = m_rotationCenter;
  retval->m_params_are_set = m_params_are_set ;

  return static_cast<BasicIBC*>(retval);
}

// Set up initial conditions
void CompactSupportIBC::initialize(LevelData<FArrayBox>& a_phi,
                                   const ProblemDomain& a_domain,
                                   const CoordSys<FArrayBox,FluxBox>& a_coordSys,
                                   Real a_dx,
                                   Real a_time)
{
   CH_assert(m_params_are_set == true);

   LevelData<FArrayBox> phiAvg( a_phi.getBoxes(),
                                a_phi.nComp(),
                                a_phi.ghostVect() + IntVect::Unit );
   DataIterator dit = phiAvg.dataIterator();
   for (dit.begin(); dit.ok(); ++dit)
   {
      pointVal(phiAvg[dit], a_domain, a_coordSys, phiAvg[dit].box(),
               false, a_dx, a_time);
   }

   // convert point values into 4th-order cell averages
   fourthOrderAverage(phiAvg);

   // construct cell average of phi*J
   const LevelData<FArrayBox>& cellAvgJ = a_coordSys.getJ();
   fourthOrderCellProd( a_phi, phiAvg, cellAvgJ );
}


void
CompactSupportIBC::advVel(LevelData<FArrayBox>& a_advVel,
                          const ProblemDomain& a_domain,
                          const CoordSys<FArrayBox,FluxBox>& a_coordSys,
                          Real a_dXi,
                          Real a_time)
{
   Real twoPi = 8.0 * atan(1.0);
   DataIterator dit = a_advVel.dataIterator();
   for (dit.begin(); dit.ok(); ++dit)
   {
      if (m_velType == UNIFORM)
      {
         for (int dir=0; dir<SpaceDim; dir++)
         {
            a_advVel[dit].setVal(m_uniformVel[dir],dir);
         }
      }
      else if (m_velType == SOLIDBODY)
      {
         Real twoPiOmega = twoPi * m_omega;
         FArrayBox& advVel = a_advVel[dit];

         BoxIterator bit(advVel.box());
         for (bit.begin();bit.ok();++bit)
         {
            // get cell center
            IntVect iv = bit();
            RealVect xi;
            for (int idir=0; idir<SpaceDim; idir++)
            {
               xi[idir] = ( 0.5 + iv[idir] ) * a_dXi;
            }
            RealVect x( a_coordSys.realCoord( xi ) - m_rotationCenter );
            D_TERM( advVel(iv,0) = -twoPiOmega * x[1];,
                    advVel(iv,1) =  twoPiOmega * x[0];,
                    advVel(iv,2) =  0; );
         }
      }
      else if (m_velType == TRANSOSC)
      {
         FArrayBox& advVel = a_advVel[dit];
         Real speed = sqrt( m_uniformVel.dotProduct( m_uniformVel ) );
         Real L = speed / m_uniformVel.dotProduct( RealVect::Unit );

         BoxIterator bit(advVel.box());
         for (bit.begin();bit.ok();++bit)
         {
            // get cell center
            IntVect iv = bit();
            RealVect xi;
            for (int idir=0; idir<SpaceDim; idir++)
            {
               xi[idir] = ( 0.5 + iv[idir] ) * a_dXi;
            }
            RealVect x( a_coordSys.realCoord( xi ) );
#if 1
            Real zeta = x.dotProduct( m_uniformVel ) / ( L * speed );
            Real osc = m_oscAmp * cos( twoPi * zeta );
            D_TERM( advVel(iv,0) = L*L*(m_uniformVel[0]-m_uniformVel[1]*osc);,
                    advVel(iv,1) = L*L*(m_uniformVel[1]+m_uniformVel[0]*osc);,
                    advVel(iv,2) = 0; );
#else
            D_TERM( advVel(iv,0) = 1;,
                    advVel(iv,1) = 1+0.25*sin(twoPi*x[0])*sin(twoPi*x[1]);,
                    advVel(iv,2) = 0; );
#endif
         }

      }
      else
      {
         MayDay::Error("ConstantBC::advVel -- bad velType");
      }
   }
   fourthOrderAverage(a_advVel);
}


/// fill ghost cell values at domain boundaries
void
CompactSupportIBC::ghostCellBC(LevelData<FArrayBox>& a_phi,
                               const ProblemDomain& a_domain,
                               const CoordSys<FArrayBox,FluxBox>& a_coordSys,
                               Real a_dx,
                               Real a_time)
{
//  const LevelData<FArrayBox>& J = a_coordSys.getJ();

   IntVect ghostVect = a_phi.ghostVect();
   //const DisjointBoxLayout& grids = a_phi.getBoxes();
   const Box& domainBox = a_domain.domainBox();

   DataIterator dit = a_phi.dataIterator();
   for (dit.begin(); dit.ok(); ++dit)
   {
      FArrayBox& thisPhi = a_phi[dit];
      if (!a_domain.contains(thisPhi.box()) )
      {
        //const Box& gridBox = grids[dit];
         for (int dir=0; dir<SpaceDim; dir++)
         {
            // this is designed to ensure that
            // corner cells get filled
            IntVect tanGrow(ghostVect);
            tanGrow[dir] = 0;

            if (!a_domain.isPeriodic(dir))
            {
               SideIterator sit;
               for (sit.begin(); sit.ok(); ++sit)
               {
                  Box ghostBox;
                  if (sit() == Side::Lo)
                  {
                     ghostBox = adjCellLo(domainBox,
                                          dir,
                                          ghostVect[dir]);
                  }
                  else
                  {
                     ghostBox = adjCellHi(domainBox,
                                          dir,
                                          ghostVect[dir]);
                  }

                  // ensure that corner cells get filled
                  ghostBox.grow(tanGrow);
                  ghostBox &= thisPhi.box();


                  if (!ghostBox.isEmpty())
                  {
                     // need to grow the ghost box by
                     // one for 4th-order averaging
                     ghostBox.grow(1);
                     FArrayBox ghostData(ghostBox,
                                         thisPhi.nComp());
                     pointVal(ghostData,
                              a_domain,
                              a_coordSys,
                              ghostBox,
                              true,
                              a_dx,
                              a_time);

                     fourthOrderAverageCell(ghostData);

                     // now copy into ghost cells
                     ghostBox.grow(-1);

                     thisPhi.copy(ghostData, ghostBox);
                  }  // end if there are domain ghost cells here
               } // end loop over hi-lo
            } // end if not periodic in this direction
         } // end loop over directions
      } // end if phi sticks out of domain
   } // end loop over grids
}

static void computeFineCellAveragedJacobian(
   LevelData<FArrayBox>& a_fineJ,
   const CoordSys<FArrayBox,FluxBox>& a_coordSys,
   Real a_dx)
{
   LevelData<FArrayBox> Jpw( a_fineJ.getBoxes(),
                             a_fineJ.nComp(),
                             a_fineJ.ghostVect() + IntVect::Unit );
   DisjointBoxLayout finerGrids = a_fineJ.getBoxes();
   DataIterator dit = finerGrids.dataIterator();
   for (dit.begin(); dit.ok(); ++dit)
   {
      FArrayBox& thisJpw = Jpw[dit];
      BoxIterator bit(thisJpw.box());
      for (bit.begin();bit.ok();++bit)
      {
         IntVect iv = bit();
         RealVect xi( iv );
         xi += 0.5;
         xi *= a_dx;
         RealVect x( a_coordSys.realCoord( xi ) );
         thisJpw(iv) = a_coordSys.pointwiseJ( x );
      }
   }

   // convert point values to 4th-order cell averages
   fourthOrderAverage( Jpw );

   // copy into m_fineJ
   for (dit.begin(); dit.ok(); ++dit)
   {
      a_fineJ[dit].copy( Jpw[dit], a_fineJ[dit].box() );
   }
}

/// compute exact solution
/**
 */
void
CompactSupportIBC::exactSoln(
   LevelData<FArrayBox>& a_phiJ,
   const ProblemDomain& a_domain,
   const CoordSys<FArrayBox,FluxBox>& a_coordSys,
   Real a_dx,
   Real a_time)
{
   // fine grid domain
   const int nRef = 8;
   const DisjointBoxLayout& grids = a_phiJ.getBoxes();
   DisjointBoxLayout finerGrids;
   refine( finerGrids, grids, nRef );
   const Real fineDx = a_dx / nRef;
   ProblemDomain finerDomain( a_domain );
   finerDomain.refine( nRef );

   // fine grid data objects
   const int ncomp( a_phiJ.nComp() );
   const IntVect ghostVect( a_phiJ.ghostVect() );
   LevelData<FArrayBox> finerPhi( finerGrids, ncomp, ghostVect );
   LevelData<FArrayBox> finerPhiJ( finerGrids, ncomp, ghostVect );

   // compute 4th-order cell-averaged phi solution on finer computatonal mesh
   DataIterator dit = finerPhi.dataIterator();
   for (dit.begin(); dit.ok(); ++dit)
   {
      pointVal( finerPhi[dit], finerDomain, a_coordSys,
                finerPhi[dit].box(), false, fineDx, a_time );
   }
   fourthOrderAverage( finerPhi );

   // get the cell-averaged Jacobian on the finer computational mesh
   if ( !m_fineJ.isDefined() )
   {
      m_fineJ.define( finerGrids, 1, ghostVect );
      computeFineCellAveragedJacobian( m_fineJ, a_coordSys, fineDx );
      m_crseJ.define( grids, 1, ghostVect );
      CoarseAverage averager( finerGrids, grids, 1, nRef );
      averager.averageToCoarse( m_crseJ, m_fineJ );
   }

   // get the cell-averaged phi*J on the finer computational mesh
   fourthOrderCellProd( finerPhiJ, finerPhi, m_fineJ );

   // average down
   CoarseAverage averager( finerGrids, grids, ncomp, nRef );
   averager.averageToCoarse( a_phiJ, finerPhiJ );

   // get the cell-averaged phi on the finer physical mesh, if necessary
   if (m_exactOnPhysicalDomain)
   {
      DataIterator dit = a_phiJ.dataIterator();
      for (dit.begin(); dit.ok(); ++dit)
      {
         a_phiJ[dit].divide( m_crseJ[dit], grids[dit], 0, 0, ncomp );
      }
   }
//   if (m_exactOnPhysicalDomain) {
//      DataIterator dit = finerPhi.dataIterator();
//      for (dit.begin(); dit.ok(); ++dit) {
//       finerPhi[dit].divide( m_fineJ[dit], finerGrids[dit],
//                             0, 0, finerPhi.nComp() );
//      }
//   }

   return;
}


// Set up initial conditions
void CompactSupportIBC::pointVal(FArrayBox& a_phi,
                                 const ProblemDomain& a_domain,
                                 const CoordSys<FArrayBox,FluxBox>& a_coordSys,
                                 const Box& a_box,
                                 bool a_includeJ,
                                 Real a_dXi,
                                 Real a_time)
{
   CH_assert(m_params_are_set == true);
   Real Pi = 4.0 * atan(1.0);
   Real twoPi = 2.0 * Pi;
   Real speed = sqrt( m_uniformVel.dotProduct( m_uniformVel ) );
   Real cost = m_uniformVel[0] / speed;
   Real sint = m_uniformVel[1] / speed;
   Real L = 1.0 / ( cost + sint );

   // Box of current grid
   //Box intersectBox(a_phi.box());

   // compute current displaced center
   RealVect center = m_center;
   if (m_velType == UNIFORM)
   {
      center += a_time * m_uniformVel;
   }
   else if (m_velType == SOLIDBODY)
   {
      RealVect localCenter = m_center - m_rotationCenter;
      Real radCenter = sqrt( D_TERM( localCenter[0] * localCenter[0],
                                    +localCenter[1] * localCenter[1],
                                    +0) );
      Real oldTheta = acos( localCenter[0] / radCenter );
      Real newTheta = oldTheta + twoPi * m_omega * a_time;
      D_TERM(center[0] = radCenter * cos( newTheta );,
             center[1] = radCenter * sin( newTheta );,
             center[2] = center[2];)

      center += m_rotationCenter;
   }
   else if (m_velType == TRANSOSC)
   {
      center[0] = ( m_center[0] * cost + m_center[1] * sint ) / L;
      center[1] = ( m_center[1] * cost - m_center[0] * sint ) / L;
   }

   // enforce periodicity on center
   if (m_velType != SOLIDBODY)
   {
      for (int dir=0; dir<SpaceDim; dir++)
      {
         if (center[dir]>1.0 || center[dir]<0.0)
         {
            center[dir] -= floor(center[dir]);
         }
      }
   }

   // 0.5 for cell-centered, 0 for node-centering
   RealVect meshOffset(0.5*RealVect::Unit);
   for (int dir=0; dir<SpaceDim; dir++)
     {
       if (a_box.type(dir) == IndexType::NODE) meshOffset[dir] = 0.0;
     }

   // compute initial values
   Real scale = 0.5 * Pi / m_width;
   BoxIterator bit(a_box);
   for (bit.begin();bit.ok();++bit)
   {
      IntVect iv = bit();
      RealVect xi( iv );
      xi += meshOffset;
      xi *= a_dXi;
      RealVect x( a_coordSys.realCoord( xi ) );

      // transosc distorts
      if (m_velType == TRANSOSC)
      {
         // transform to rotated and scaled coordinate system
         RealVect z;
         z[0] = ( x[0] * cost + x[1] * sint) / L;
         z[1] = ( x[1] * cost - x[0] * sint) / L;
         // traced coordinates in rotated system
         x[0] = z[0] - m_uniformVel[0] * a_time;
         x[1] = z[1] + m_oscAmp / twoPi * ( sin( twoPi * x[0] ) -
                                            sin( twoPi * z[0] ) );
         // enforce periodicity
//       for (int dir=0; dir<SpaceDim; dir++) {
//          x[dir] -= floor(x[dir]);
//       }
      }

      // Compute coordinates relative to center
      RealVect xrel( x - center );

      if (m_velType != SOLIDBODY)
      {
         // enforce periodicity
         for (int dir=0; dir<SpaceDim; dir++)
         {
            while ( fabs(xrel[dir]) > (1.0-m_width) )
            {
               xrel[dir] -= xrel[dir]/fabs(xrel[dir]);
            }
         }
      }

      Real radius = sqrt( D_TERM(   xrel[0] * xrel[0],
                                  + xrel[1] * xrel[1],
                                  + 0) );
      Real g = 0.0;
      if ( radius <= m_width )
      {
         Real cosarg = cos( scale * radius );
         g = cosarg * cosarg;
         g *= g;
         g *= g;
      }
      Real phiVal = m_mag * g;

      // Compute phiJ pointwise
      if (a_includeJ)
      {
         phiVal *= a_coordSys.pointwiseJ( x );
      }

      for (int icomp=0; icomp<a_phi.nComp(); icomp++)
      {
         a_phi(iv,icomp) = phiVal;
      }
   }
}