# SatView Solar-System Data Expansion

## Status

Phase 1 implemented on 2026-07-06. Later phases remain future work. This plan
continues to prohibit rendering catalogue metadata as precise trajectories.

The implemented first slice includes:

- a grouped Sun/planet/major-moon POV dropdown;
- normalized body-centric globe and map views for the Sun, eight planets, and
  20 major moons;
- a shared Vulkan/Metal textured ellipsoid path with real size ratios and
  approximate oblateness;
- sourced planet maps and suitable USGS/PDS moon mosaics, with clearly named
  deterministic procedural stand-ins where a usable global map is absent;
- local mean-element orbit guides and moving markers for each selected body's
  major natural satellites, plus presentation ring bands for Saturn and
  Uranus;
- observer-aware contextual Suns in planet/moon skies and textured parent
  planets in major-moon skies, with angular size preserved on bounded depth
  proxies;
- preserved Earth SGP4 and Moon sampled-ephemeris behaviour.

Current Phase 1 limitations are intentional: non-Earth natural-body positions
are compact Kepler presentation approximations rather than Horizons/SPICE
ephemerides, and no artificial spacecraft/debris tracks are added outside the
existing Earth/Moon sources.

## Summary

Public coverage is strong for natural Solar-System objects and many mission
spacecraft, but weak for artificial debris outside Earth orbit.

The recommended architecture is therefore source- and fidelity-aware:

- use high-precision JPL or SPICE ephemerides for planets, moons, and selected
  spacecraft;
- use JPL Small-Body Database orbital solutions for broad asteroid and comet
  populations;
- upgrade selected small bodies with sampled Horizons vectors and uncertainty;
- use mission archives for historical spacecraft, landings, and impacts;
- keep untracked artificial hardware and debris catalogue-only unless a real
  trajectory is available.

## Available Data By Object Class

| Object class | Best public sources | Available information | SatView suitability |
|---|---|---|---|
| Planets and dwarf planets | JPL Horizons, JPL Development Ephemerides, NAIF generic SPICE kernels | High-precision position, velocity, orientation, radii, illumination, and body-fixed frames over long intervals | Excellent; suitable as the backbone of a Solar-System view. |
| Natural satellites | Horizons and NAIF generic kernels | Orbits for hundreds of major and irregular moons, plus sizes and rotational models where known | Excellent; small moons require marker scaling and level-of-detail controls. |
| Asteroids | JPL SBDB, SBDB Query API, Minor Planet Center, Horizons | Orbital elements, covariance, physical properties, aliases, observations, close approaches, and radar measurements | Excellent, but the roughly 1.5-million-object population requires filtering and bulk propagation. |
| Comets and fragments | JPL SBDB, Minor Planet Center, Horizons | Osculating elements, non-gravitational parameters, magnitude models, close approaches, and fragment identity | Very good; simple long-term Kepler propagation is less accurate for active comets. |
| Near-Earth objects | JPL CNEOS/SBDB and ESA NEOCC | Orbit solutions, uncertainty, close approaches, impact monitoring, physical properties, and follow-up priorities | Excellent for a dedicated NEO view with uncertainty envelopes. |
| Trojans, Centaurs, TNOs, and interstellar objects | SBDB, MPC, Horizons | Small-body elements and physical metadata classified by dynamical population | Good; distances require logarithmic scaling or system-level camera presets. |
| Current planetary spacecraft | Horizons plus NASA and ESA operational SPICE repositories | Mission trajectories, planned manoeuvres, reconstructed history, attitude, and instrument geometry | Excellent where kernels are public; coverage is mission-specific. |
| Historical planetary spacecraft | NASA PDS/NAIF and ESA PSA SPICE archives | Reconstructed trajectories, flybys, landings, impacts, attitude, and instrument pointing | Excellent for historical-time visualization. |
| Registered artificial objects | UNOOSA Register of Objects Launched into Outer Space | Identity, launching state, designation, launch date, destination, and general status | Useful for metadata, usually insufficient for accurate rendering. |
| Artificial debris outside Earth orbit | SATCAT metadata, mission reports, occasional observations, and research papers | Fragment identity or last-known disposition in a few cases; rarely continuous state vectors | Poor; no comprehensive public Moon-, Mars-, or Solar-System-debris trajectory catalogue exists. |
| Meteoroids and dust | Meteor networks, NASA CNEOS fireballs, PDS dust-instrument archives, and environment models | Flux, radiant, speed, population, and impact measurements | Suitable as statistical populations or events, not individually tracked objects. |
| Planetary rings | PDS mission archives, SPICE geometry, occultation and imaging products | Ring boundaries, ringlets, optical depth, and wave or particle distributions | Suitable as structured ring models, not individual particles. |
| Surface landers and impact sites | PDS/PSA mission archives, mission reports, and body-fixed SPICE coordinates | Landing or impact time, latitude, longitude, and sometimes an uncertainty ellipse | Excellent as a separate surface-object layer. |

## Primary Sources

### JPL Horizons

Horizons is the most convenient common vector interface for planets, natural
satellites, asteroids, comets, dynamical points, and selected spacecraft.

- Manual: https://ssd.jpl.nasa.gov/horizons/manual.html
- API: https://ssd-api.jpl.nasa.gov/doc/horizons.html

The service reports roughly 1.48 million asteroids, more than 4,000 comets,
hundreds of natural satellites, and 239 spacecraft. Natural-body solutions are
actively maintained. Spacecraft trajectories depend on mission-team delivery
and may update weekly or monthly, stop updating, or cover only historical or
planning intervals.

### JPL Small-Body Database

SBDB is the best machine-readable catalogue for known asteroids and comets.

- Single-object API: https://ssd-api.jpl.nasa.gov/doc/sbdb.html
- Bulk query API: https://ssd-api.jpl.nasa.gov/doc/sbdb_query.html

Useful fields include:

- object identity, aliases, SPK id, and dynamical class;
- epoch and osculating orbital elements;
- solution condition and covariance/uncertainty;
- absolute magnitude and selected physical properties;
- close approaches, radar observations, and impact-monitoring information.

SBDB solutions use observations published by the Minor Planet Center,
including radar astrometry where available.

### Minor Planet Center

The MPC is the international clearinghouse for observations, designations, and
orbits of minor planets and comets. Its bulk catalogues are valuable for raw
observational provenance and independent catalogue ingestion. For an initial
SatView implementation, JPL SBDB provides the simpler normalized API while MPC
remains the underlying observation authority and a later direct source.

### NASA NAIF/PDS SPICE

- Generic kernels: https://naif.jpl.nasa.gov/naif/data_generic.html
- Mission archives: https://naif.jpl.nasa.gov/naif/data_archived.html
- Operational kernels: https://naif.jpl.nasa.gov/naif/data_operational.html

SPICE provides:

- SPK spacecraft and natural-body trajectories;
- PCK body size and rotation models;
- CK spacecraft/instrument attitude;
- FK reference-frame definitions;
- DSK shape models;
- mission clocks, leapseconds, and meta-kernels.

Archived mission collections commonly receive increments every three or six
months and may lag operations. Operational collections can be much more current
but vary by mission.

### ESA Planetary Science Archive And SPICE Service

- SPICE data: https://www.cosmos.esa.int/web/spice/data

ESA publishes operational and peer-reviewed archival kernels for missions such
as Mars Express, ExoMars TGO, Rosetta, BepiColombo, Solar Orbiter, JUICE, and
others. Some operational kernels are produced by automatic pipelines. The
archive also includes mission science data, shape models, observation geometry,
and ancillary navigation products.

### ESA NEO Coordination Centre

- https://neo.ssa.esa.int/en/about-neocc

NEOCC provides independent orbit computation, follow-up priorities, physical
properties, risk assessment, orbit visualization, and impact monitoring for
near-Earth objects.

### UNOOSA Space Object Register

- https://www.unoosa.org/oosa/en/spaceobjectregister/resources/index.html

The UN register is useful for identity, ownership, launch, destination, and
status metadata for objects launched into Earth orbit or beyond. Registration
records are not a precision trajectory service.

## Planetary Spacecraft Coverage

| Planet or system | Example public trajectories | Likely source |
|---|---|---|
| Mercury | MESSENGER history; BepiColombo cruise and operations | NASA and ESA/JAXA SPICE |
| Venus | Magellan, Venus Express, Akatsuki, and historical probes | NAIF, ESA PSA, and JAXA archives |
| Mars | Mars Odyssey, Mars Express, MRO, MAVEN, ExoMars TGO, and historical orbiters and landers | NASA/ESA SPICE plus mission-specific archives |
| Jupiter | Galileo history, Juno, Europa Clipper, and JUICE | NASA/ESA SPICE and Horizons |
| Saturn | Cassini/Huygens reconstructed mission | NASA NAIF/PDS |
| Asteroids | Dawn, NEAR, Hayabusa missions, OSIRIS-REx, DART, and Lucy | Mission SPICE archives and Horizons |
| Comets | Rosetta/Philae, Deep Impact/EPOXI, Stardust, and historical missions | NASA/ESA archives |
| Outer Solar System | Voyager 1/2, New Horizons, and historical Pioneer trajectories | NAIF and Horizons |

Coverage is not uniform. Some foreign or commercial mission trajectories may
be unavailable, delayed, or published only by the operator.

## Artificial Debris Limitation

Earth is the only body with anything approaching a comprehensive maintained
artificial-object and debris catalogue. Beyond Earth orbit:

- launch and mission records may identify hardware;
- a mission trajectory may cover a stage until separation;
- an impact or landing location may be known;
- an object may occasionally be rediscovered by telescope or radar;
- continuous public orbit solutions are unusual.

SatView must represent these distinctions explicitly:

- `Ephemeris-backed`
- `Predicted ephemeris`
- `Historical ephemeris`
- `Last known position`
- `Catalogue only`
- `Surface`
- `Impacted`
- `Escaped`
- `Disposition unknown`

Do not derive precise-looking Mars, Moon, asteroid, or heliocentric debris
orbits from registration metadata alone.

## Recommended Architecture

### Common Object Model

Extend the current central-body and solution-fidelity model rather than adding
planet-specific object types. Each object should carry:

- stable source and object identifiers;
- natural/artificial classification;
- payload, rocket body, debris, asteroid, comet, natural satellite, surface
  object, or dynamical-point type;
- central body or barycentric frame;
- source frame and timescale;
- source revision and validity interval;
- measured, reconstructed, predicted, integrated, or approximate fidelity;
- optional orbital-element solution and covariance;
- optional sampled ephemeris;
- known disposition and evidence source.

### Frame Handling

Keep source data in its documented frame and transform only for presentation.
Likely common frames include:

- ICRF/J2000 and Solar-System barycentric;
- heliocentric ecliptic;
- planet-centred inertial;
- planet-fixed body frames;
- mission-defined rotating or local frames.

Do not mix objects from different frames without an explicit epoch-dependent
transform.

### Scale And Level Of Detail

Solar-System distances and object counts require:

- planet/system camera presets;
- logarithmic or hybrid distance scaling where appropriate;
- magnitude-, distance-, and screen-size culling;
- marker clustering and GPU instancing;
- separate body, orbit, label, and surface-object level-of-detail policies;
- bounded track horizons based on object class and orbital period.

## Phased Implementation

### Phase 1: Planets And Major Natural Satellites (implemented baseline)

- Import JPL/NAIF positions for the Sun, planets, dwarf planets, and selected
  major moons.
- Add stable body identifiers, radii, orientation, and central-body hierarchy.
- Add Solar-System and planetary-system camera presets.
- Validate body/frame transforms on both Vulkan and Metal.

This phase offers the highest confidence and smallest catalogue size.

### Phase 2: Selected Active Spacecraft

- Curate public Horizons or operational SPICE trajectories for active missions.
- Reuse the existing sampled-ephemeris pipeline where possible.
- Preserve reconstructed versus predicted provenance and strict validity bounds.
- Add mission and central-body filters.

### Phase 3: Broad Asteroid And Comet Catalogue

- Ingest selected SBDB fields and orbital elements.
- Begin with constrained populations such as numbered asteroids, NEOs, largest
  bodies, potentially hazardous asteroids, or user-selected dynamical classes.
- Add a scalable heliocentric propagation path.
- Surface epoch, solution condition, uncertainty, and physical-size metadata.

Do not initially attempt to display all roughly 1.5 million small bodies at
full track fidelity.

### Phase 4: Selected-Object Precision And Uncertainty

- Fetch or pre-generate Horizons vectors for a selected asteroid or comet.
- Render the high-fidelity path separately from the bulk approximate orbit.
- Add covariance-derived uncertainty envelopes where available.
- Include close-approach events and encounter geometry.

### Phase 5: Historical Missions And Surface Sites

- Import time-bounded SPICE trajectories for major historical missions.
- Add lander, rover, impact, and sample-return surface records.
- Allow simulation-time browsing of flybys, orbital phases, landings, and
  impacts.

### Phase 6: Rings, Meteoroid Populations, And Events

- Render named ring systems from structured boundaries/optical-depth models.
- Represent dust and meteoroids statistically rather than as falsely tracked
  particles.
- Add fireball, impact, and meteor-stream event layers where appropriate.

### Phase 7: Artificial Debris Metadata

- Ingest registration and mission metadata without inventing trajectories.
- Upgrade individual objects only when a public observation-derived ephemeris
  exists.
- Extend the lunar disposition-overlay pattern to other planetary systems.

## Small-Body Hybrid Strategy

A practical asteroid/comet implementation should use two fidelity tiers:

1. **Bulk overview**
   - SBDB osculating elements;
   - GPU-friendly approximate propagation;
   - filtering by size, magnitude, class, distance, hazard status, or selection;
   - explicit epoch and approximation labels.
2. **Selected-object precision**
   - sampled Horizons vectors;
   - strict coverage bounds;
   - covariance or uncertainty visualization;
   - close-approach and body-relative views.

This provides broad visual scale without claiming navigation-grade precision
for every object.

## Validation Requirements

- Compare imported positions against their source service at known epochs.
- Test all frame transformations and central-body changes independently.
- Verify sampled trajectories never extrapolate beyond source coverage.
- Distinguish reconstructed, predicted, integrated, approximate, and
  catalogue-only data in UI and rendering.
- Verify large catalogues remain responsive under filtering, simulation-speed
  changes, and camera transitions.
- Keep generated assets reproducible with pinned manifests and provenance.
- Run unit, render, and smoke tests on Windows/Vulkan and macOS/Metal.

## Risks

- Millions of small bodies can overwhelm CPU propagation, GPU buffers, labels,
  and interaction picking without aggressive level of detail.
- Osculating elements become stale and are not equivalent to continuously
  integrated trajectories.
- Comet non-gravitational forces and close planetary encounters reduce simple
  propagation accuracy.
- Mission SPICE sets vary widely in availability, cadence, frame definitions,
  and reconstructed/predicted coverage.
- Solar-System scale can make ordinary linear camera controls unusable.
- Artificial-object metadata can outlive the object's actual orbit or omit its
  final disposition.

## Acceptance Criteria For An Initial Release

- Planets and selected major moons render from sourced ephemerides.
- Active spacecraft retain source, frame, validity, and prediction/reconstruction
  fidelity.
- A bounded small-body subset can be filtered and propagated interactively.
- Selected asteroids/comets can use high-fidelity Horizons paths.
- Surface, impacted, escaped, and catalogue-only objects never appear as active
  orbiting markers without a valid trajectory.
- Vulkan and Metal consume the same backend-neutral Solar-System scene records.
