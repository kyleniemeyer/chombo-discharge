/* chombo-discharge
 * Copyright © 2021 SINTEF Energy Research.
 * Please refer to Copyright.txt and LICENSE in the chombo-discharge root directory.
 */

/*!
  @file   CD_ItoKMCAir3LFA.H
  @brief  Ito plasma three-species model for air
  @author Robert Marskar
*/

#ifndef CD_ItoKMCAir3LFA_H
#define CD_ItoKMCAir3LFA_H

// Our includes
#include <CD_ItoKMCPhysics.H>
#include <CD_LookupTable1D.H>
#include <CD_NamespaceHeader.H>

namespace Physics {
  namespace ItoKMC {

    /*!
      @brief Hard-coded implementation of ItoKMCPhysics for simplified discharges in STP air. 
      @detail Transport data for the electrons are taken from BOLSIG+.
    */
    class ItoKMCAir3LFA : public ItoKMCPhysics
    {
    public:
      class Electron;
      class Positive;
      class Negative;
      class PhotonZ;
      class TestSpecies;

      /*!
	@brief Full constructor. Initializes species and sets up reactions. 
      */
      ItoKMCAir3LFA();

      /*!
	@brief Destructor. Does nothing of importance
      */
      virtual ~ItoKMCAir3LFA();

      /*!
	@brief Parse run-time options
      */
      virtual void
      parseRuntimeOptions() noexcept override;

      /*!
	@brief Compute a physics-baseded time step. 
	@param[in] a_E            Electric field. 
	@param[in] a_pos          Position
	@param[in] a_numParticles Number of particles per cell
	@return Returns m_deltaX/k where k is Townsend ionization coefficient and m_deltaX is user-specified. 
      */
      virtual Real
      computeDt(const RealVect a_E, const RealVect a_pos, const Vector<Real> a_densities) const noexcept override;

      /*!
	@brief Compute Townsend ionization coefficient
	@param[in] a_E Electric field. 
      */
      virtual Real
      computeAlpha(const RealVect a_E) const override;

      /*!
	@brief Update reaction rates
	@param[in] a_E       Electric field
	@param[in] a_phi     Plasma species densities
	@param[in] a_gradPhi Density gradients for plasma species. 
	@param[in] a_dx      Grid resolution
	@param[in] a_kappa   Cut-cell volume fraction
      */
      virtual void
      updateReactionRates(const RealVect          a_E,
                          const Vector<Real>&     a_phi,
                          const Vector<RealVect>& a_gradPhi,
                          const Real              a_dx,
                          const Real              a_kappa) const noexcept override;

      /*!
	@brief Compute the Ito solver mobilities.
	@param[in] a_time Time
	@param[in] a_pos  Position
	@param[in] a_E    Electric field
      */
      Vector<Real>
      computeMobilities(const Real a_time, const RealVect a_pos, const RealVect a_E) const noexcept override;

      /*!
	@brief Compute the Ito solver diffusion coefficients
	@param[in] a_time      Time
	@param[in] a_pos       Position
	@param[in] a_E         Electric field
      */
      Vector<Real>
      computeDiffusionCoefficients(const Real a_time, const RealVect a_pos, const RealVect a_E) const noexcept override;

      /*!
	@brief Resolve secondary emission at the EB.
	@details Routine is here to handle charge injection, secondary emission etc.
	@param[out] a_secondaryParticles Outgoing plasma species particles.
	@param[out] a_secondaryCDRFluxes Outgoing plasma species CDR fluxes
	@param[out] a_secondaryPhotons   Photons injected through the EB
	@param[in]  a_primaryParticles   Particles that left the computational domain through the EB
	@param[in]  a_primaryCDRFluxes   CDR fluxes leaving the computational domain through the EB
	@param[in]  a_primaryPhotons     Photons that left the computational domain through the EB
	@param[in]  a_newNumParticles    Total number of particles in the cut-cell AFTER the transport step
	@param[in]  a_oldNumParticles    Total number of particles in the cut-cell BEFORE the transport step
	@param[in]  a_electricField      Electric field                  
	@param[in]  a_cellCenter         Physical position of the cell center. 
	@param[in]  a_cellCentroid       Cell centroid relative to the cell center (not multiplied by dx)
	@param[in]  a_bndryCentroid      EB face centroid relative to the cell center (not multiplied by dx)
	@param[in]  a_bndryNormal        Cut-cell normal vector. 
	@param[in]  a_bndryArea          Cut-cell boundary area - not multiplied by dx (2D) or dx^2 (3D)
	@param[in]  a_dx                 Grid resolution on this level. 
	@param[in]  a_dt                 Time step
	@param[in]  a_isDielectric       Dielectric or electrode. 
	@param[in]  a_matIndex           Material index (taken from computationalGeometry)
      */
      virtual void
      secondaryEmissionEB(Vector<List<ItoParticle>>&       a_secondaryParticles,
                          Vector<Real>&                    a_secondaryCDRFluxes,
                          Vector<List<Photon>>&            a_secondaryPhotons,
                          const Vector<List<ItoParticle>>& a_primaryParticles,
                          const Vector<Real>&              a_primaryCDRFluxes,
                          const Vector<List<Photon>>&      a_primaryPhotons,
                          const RealVect&                  a_E,
                          const RealVect&                  a_cellCenter,
                          const RealVect&                  a_cellCentroid,
                          const RealVect&                  a_bndryCentroid,
                          const RealVect&                  a_bndryNormal,
                          const Real                       a_bndryArea,
                          const Real                       a_dx,
                          const Real                       a_dt,
                          const bool                       a_isDielectric,
                          const int                        a_matIndex) const noexcept override;

    protected:
      /*!
	@brief Extrapolate BC or not
      */
      bool m_extrapBC;

      /*!
	@brief User-defined parameter for time step restriction
      */
      Real m_deltaX;

      /*!
	@brief Number of plasma species
      */
      int m_numPlasmaSpecies;

      /*!
	@brief Number of radiative transfer species
      */
      int m_numRtSpecies;

      /*!
	@brief Ion mobility
      */
      Real m_ionMobility;

      /*!
	@brief Ion diffusion coefficient
      */
      Real m_ionDiffCo;

      /*!
	@brief Ion impact efficiency
      */
      Real m_ionImpactEfficiency;

      /*!
	@brief Quantum efficiency
      */
      Real m_quantumEfficiency;

      /*!
	@brief Neutral number density
      */
      Real m_N;

      /*!
	@brief Gas pressure (SI)
      */
      Real m_p;

      /*!
	@brief Quenching pressure (SI)
      */
      Real m_pq;

      /*!
	@brief Gas temperature
      */
      Real m_T;

      /*!
	@brief O2 content
      */
      Real m_O2frac;

      /*!
	@brief N2 content
      */
      Real m_N2frac;

      /*!
	@brief Photo-ionization factor
      */
      Real m_photoIonizationFactor;

      /*!
	@brief Lookup tables holding mobilities, rate coefficients etc.
      */
      std::map<std::string, LookupTable1D<2>> m_tables;

      /*!
	@brief Excitation rate for photoionization term
      */
      virtual Real
      excitationRates(const Real a_E) const;

      /*!
	@brief Sergey factor for photoionization term
      */
      virtual Real
      sergeyFactor(const Real a_O2frac) const;

      /*!
	@brief Add a table to m_tables
	@param[in] a_tableName Table identifier for m_table
	@param[in] a_file      File. Must be in column x/y format
      */
      virtual void
      addTable(const std::string a_tableName, const std::string a_file) noexcept;

      /*!
	@brief Read various tabulated data into the lookup tables in this class
      */
      virtual void
      readTables() noexcept;

      /*!
	@brief Parse the m_dX variable used for time step restriction
      */
      virtual void
      parseDx() noexcept;

      /*!
	@brief Parse some auxiliary transport settings. 
      */
      virtual void
      parseTransport() noexcept;
    };

    /*!
      @brief Electron species
    */
    class ItoKMCAir3LFA::Electron : public ItoSpecies
    {
    public:
      /*!
	@brief Constructor. Sets to mobile and diffusive
      */
      Electron();

      /*!
	@brief Destructor. Does nothing
      */
      virtual ~Electron();
    };

    /*!
      @brief Positive ion species
    */
    class ItoKMCAir3LFA::Positive : public CdrSpecies
    {
    public:
      /*!
	@brief Constructor. Reads in parameters. 
      */
      Positive();

      /*!
	@brief Destructor. Does nothing
      */
      virtual ~Positive();

      /*!
	@brief Initial data
      */
      Real
      initialData(const RealVect a_pos, const Real a_time) const override;
    };

    class ItoKMCAir3LFA::Negative : public CdrSpecies
    {
    public:
      /*!
	@brief Constructor. Reads in parameters. 
      */
      Negative();

      /*!
	@brief Destructor. Does nothing
      */
      virtual ~Negative();

      /*!
	@brief Initial data
      */
      Real
      initialData(const RealVect a_pos, const Real a_time) const override;
    };

    class ItoKMCAir3LFA::PhotonZ : public RtSpecies
    {
    public:
      /*!
	@brief Constructor. Reads in parameters. 
      */
      PhotonZ();

      /*!
	@brief Destructor. Does nothing
      */
      virtual ~PhotonZ();

      /*!
	@brief Get a random absorption coefficient. 
	note This uses random sampling of the frequency between m_f1 and m_f2. See CdrPlasmaPhysicsJSON docs
      */
      Real
      getAbsorptionCoefficient(const RealVect a_pos) const override;

    protected:
      /*!
	@brief Lower frequency
      */
      Real m_f1;

      /*!
	@brief Upper frequency
      */
      Real m_f2;

      /*!
	@brief K1 factor
      */
      Real m_K1;

      /*!
	@brief K2 factor
      */
      Real m_K2;
    };
  } // namespace ItoKMC
} // namespace Physics

#include <CD_NamespaceFooter.H>

#endif
