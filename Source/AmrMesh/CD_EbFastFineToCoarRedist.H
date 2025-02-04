/* chombo-discharge
 * Copyright © 2021 SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @file   CD_EbFastFineToCoarRedist.H
  @brief  New EBFineToCoarRedist class with faster define function.
  @author Robert Marskar
*/

#ifndef CD_EbFastFineToCoarRedist_H
#define CD_EbFastFineToCoarRedist_H

// Chombo includes
#include <EBFineToCoarRedist.H>

// Our includes
#include <CD_NamespaceHeader.H>

/*!
  @brief EBFineToCoarRedist with faster define function
  @details This is just like Chombo's EBFineToCoarRedist except that it is much faster to define. 
  @note To use run-time debugging and profiling, use EbFastFineToCoarRedist.debug=true and
  EbFastFineToCoarRedist.profile=true
*/
class EbFastFineToCoarRedist : public EBFineToCoarRedist
{
public:
  /*!
    @brief Weak constructor.
  */
  EbFastFineToCoarRedist();

  /*!
    @brief Destructor.
  */
  ~EbFastFineToCoarRedist();

  /*!
    @brief New define function for EBFastCoarToFine
    @param[in] a_eblgFine      Fine level EBLevelGrid
    @param[in] a_eblgCoar      Coarse level EBLevelGrid
    @param[in] a_refRat        Refinement ratio between levels
    @param[in] a_nComp         Number of variables
    @param[in] a_redistRad     Redistribution radius
  */
  void
  fastDefine(const EBLevelGrid& a_eblgFine,
             const EBLevelGrid& a_eblgCoar,
             const int&         a_refRat,
             const int&         a_nComp,
             const int&         a_redistRad);

protected:
  /*!
    @brief Make a mask which holds true for cells within the specified radius on the inside of the refinement boundary.
    @param[out] a_fineShellMask Fine grid shell mask, holding the cells. 
  */
  void
  makeFineMask(LevelData<BaseFab<bool>>& a_fineShellMask);

  /*!
    @brief Fine sets, as per EBFineToCoarRedist.
    @param[in] a_fineMask Mask which is true on the inside of the CFIVS. 
  */
  void
  makeFineSets(const LevelData<BaseFab<bool>>& a_fineMask);

  /*!
    @brief Make coarse sets, as per EBFineToCoarRedist
    @param[in] a_refCoarMask Mask defined on the refined version of the coarse grid. 
    @details This 
  */
  void
  makeCoarSets(const LevelData<BaseFab<bool>>& a_refCoarMask);

  /*!
    @brief Debugging function. Gathers and broadcasts a set
    @param[inout] a_set Set to gather and broadcast
  */
  void
  gatherBroadcast(IntVectSet& a_set);

  /*!
    @brief Debugging function. Gathers and broadcasts m_setsCedFine onto a_setsCedFine
    @param[inout] a_setsCedFine Fine set
  */
  void
  gatherSetsFine(IntVectSet& a_setsFine);

  /*!
    @brief Debugging function. Gathers and broadcasts m_setsCoar onto a_setsCoar. 
    @param[inout] a_setsCoar Fine set
  */
  void
  gatherSetsRefCoar(IntVectSet& a_setsRefCoar);
};

#include <CD_NamespaceFooter.H>

#endif
