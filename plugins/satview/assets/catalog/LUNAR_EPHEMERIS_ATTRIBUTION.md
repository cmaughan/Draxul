# Lunar ephemeris attribution

The generated `lunar_ephemeris.csv` vectors come from two NASA services:

- NASA/JPL Horizons, operated by the Solar System Dynamics Group at JPL, for
  LRO, CAPSTONE, Danuri, and Chandrayaan-2 Orbiter.
- NASA Goddard's Satellite Situation Center (SSC) six-year predictive
  ephemerides for ARTEMIS P1 and P2. SSC supplies geocentric GEI J2000
  positions; the generator subtracts matching JPL Horizons Moon vectors and
  derives velocities with centred finite differences.

- API: <https://ssd.jpl.nasa.gov/api/horizons.api>
- Documentation: <https://ssd-api.jpl.nasa.gov/doc/horizons.html>
- SSC API: <https://sscweb.gsfc.nasa.gov/WebServices/REST/>
- Coordinate center: Moon body center (`500@301`)
- Reference frame: ICRF (`REF_PLANE=FRAME`)
- Corrections: geometric states (`VEC_CORR=NONE`)
- Units: kilometres and kilometres per second
- Time scale: UTC/UT output (`TIME_TYPE=UT`)

ARTEMIS rows are explicitly labelled `NASA SSC prediction` because they are
mission predictions rather than reconstructed observations. SatView never
extrapolates them beyond the generated window.

The exact spacecraft IDs, NORAD mappings, sample cadences, and any known
coverage bounds are pinned in `lunar_ephemeris_targets.json`. Regenerate the
CSV using `scripts/build_satview_lunar_ephemeris.py`; do not edit generated
vectors by hand.
