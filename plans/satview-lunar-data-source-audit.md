# SatView Lunar Object Data-Source Audit

## Status

Discovery pass completed 2026-07-05. ARTEMIS P1/P2 ingestion and the first two
primary-source disposition corrections were implemented the same day.

The audit covered the 38 undecayed CelesTrak SATCAT
records tagged `ORBIT_CENTER=MO` and `ORBIT_TYPE=ORB` in the local SatView
cache refreshed 2026-07-05.

This is a source audit, not proof that every SATCAT record is still in lunar
orbit. SATCAT's blank decay field is especially weak evidence for historical
lunar objects.

## Result

The 38 records divide into:

| Category | Count | Meaning |
|---|---:|---|
| Current public trajectory already ingested | 4 | Horizons covers LRO, Chandrayaan-2 Orbiter, CAPSTONE, and Danuri. |
| Current public trajectory newly found | 2 | NASA SSC publishes ARTEMIS P1/P2 definitive and predictive ephemerides. |
| Historical public trajectory | 1 | Horizons covers Chandrayaan-1 only through 2022; it is not a current solution. |
| Confirmed not orbiting | 2 | Chang'e-4 is on the surface; HAKUTO-R M2 impacted during its 2025 landing attempt. |
| Modern record without public vectors found | 6 | DRO-A/B, Queqiao-2, Tiandu-1/2, and ICUBE-Q require operator or tracking data. |
| Legacy object without a current public solution | 23 | Old payloads, stages, and Explorer 49 debris require disposition research or new observations. |

The immediate honest ceiling therefore rises from four to **six current
positionable objects**, not 38. CAPSTONE and the two ARTEMIS probes can be far
outside a close low-lunar-orbit camera view.

## Current Public Trajectories

| NORAD | Object | Source | Verified public coverage | SatView action |
|---:|---|---|---|---|
| 30581 | ARTEMIS P1 / THEMIS B | NASA SSC `artemisp1pred` | 2008-07-18 through 2032-06-20; one-minute resolution | Add SSC ingestion and convert GEI J2000 geocentric vectors to Moon-relative ICRF. |
| 30582 | ARTEMIS P2 / THEMIS C | NASA SSC `artemisp2pred` | 2008-07-18 through 2032-06-20; one-minute resolution | Add with P1. |
| 35315 | LRO | Horizons `-85` | Reconstructed/predicted coverage through 2028-01-03 | Already ingested. |
| 44441 | Chandrayaan-2 Orbiter | Horizons `-152` | Coverage through 2026-08-12 | Already ingested. |
| 52914 | CAPSTONE | Horizons `-1176` | Coverage through 2026-07-23 | Already ingested. |
| 53365 | Danuri | Horizons `-155` | Coverage through 2027-01-03 | Already ingested. |

NASA SSC also exposes definitive `artemisp1` and `artemisp2` records through
2026-06-17. Prefer definitive data for elapsed times and the `*pred` records
only for later samples. Do not present the six-year prediction as observed
tracking data.

## Historical Public Trajectory

| NORAD | Object | Source | Coverage/use |
|---:|---|---|---|
| 33405 | Chandrayaan-1 | Horizons `-86` | Public trajectory ends 2022-10-01 and includes extrapolation after the 2016 radar recovery. Useful for historical-time rendering, not a 2026 marker. |

NASA/JPL radar observations confirmed Chandrayaan-1 remained in lunar orbit in
2016, but this does not provide a current 2026 solution.

## Confirmed SATCAT Misclassification/Staleness

| NORAD | Object | Evidence | Required correction |
|---:|---|---|---|
| 43845 | Chang'e-4 | CNSA reports the spacecraft landed on the lunar far side on 2019-01-03. | Treat as a lunar surface object, not an orbiter. |
| 62717 | HAKUTO-R M2 / RESILIENCE | NASA LRO imaged its hard-landing impact site from 2025-06-05. | Treat as impacted; exclude from current orbit counts. |

## Modern Records With No Public State Vectors Found

| NORAD | Object | Notes |
|---:|---|---|
| 59228 | DRO-A | No Horizons or NASA SSC match. SATCAT's summary values are not safe lunar elements. |
| 60058 | DRO-B | Same limitation as DRO-A. |
| 59276 | Queqiao-2 | No public machine-readable trajectory found in Horizons, SSC, or NAIF. |
| 59278 | Tiandu-1 | No public machine-readable trajectory found. |
| 59279 | Tiandu-2 | No public machine-readable trajectory found. |
| 59629 | ICUBE-Q / ICUBE-Qamar | Mission information is public, but no current state-vector service was found. |

## Legacy Inventory Requiring Disposition Research

No current public trajectory was found for these 23 records:

| NORAD | Object | Type |
|---:|---|---|
| 2126 | Luna 10 | Payload |
| 2395 | Atlas Agena D | Rocket body |
| 2406 | Luna 11 | Payload |
| 2508 | Luna 12 | Payload |
| 2884 | Explorer 35 / AIMP-E | Payload |
| 2908 | Atlas Agena D | Rocket body |
| 3178 | Luna 14 | Payload |
| 3948 | Apollo 10 LM descent stage | Payload |
| 4041 | Apollo 11 LM ascent stage | Payload |
| 5377 | Apollo 15 subsatellite | Payload |
| 5449 | SL-12 stage | Rocket body |
| 5488 | Luna 19 | Payload |
| 5490 | SL-12 stage | Rocket body |
| 5836 | SL-12 stage | Rocket body |
| 6005 | Apollo 16 LM Orion | Payload |
| 6686 | Explorer 49 / RAE-2 | Payload |
| 6725 | Explorer 49 debris | Debris |
| 6726 | Explorer 49 debris | Debris |
| 7315 | Luna 22 | Payload |
| 20618 | Hagoromo | Payload |
| 32056 | Ouna / VRAD | Payload |
| 32057 | H-2A stage | Rocket body |
| 37175 | CZ-3C stage | Rocket body |

Some have historical science or reconstructed trajectory data, but none of the
sources checked supplies a trustworthy current state. Their next audit should
establish `impacted`, `escaped`, `surface`, or `orbit unconfirmed` rather than
assuming that a blank SATCAT decay date means `orbiting`.

## Reproducible Checks

- Horizons alias lookup:
  `https://ssd.jpl.nasa.gov/api/horizons_lookup.api?sstr=<name>&group=sct`
- Horizons target metadata:
  `https://ssd.jpl.nasa.gov/api/horizons.api?format=json&COMMAND='<id>'&MAKE_EPHEM='NO'`
- NASA SSC object inventory:
  `https://sscweb.gsfc.nasa.gov/WS/sscr/2/observatories`
- NASA SSC ARTEMIS proof query:
  `https://sscweb.gsfc.nasa.gov/WS/sscr/2/locations/artemisp1pred,artemisp2pred/20260705T000000Z,20260705T010000Z/geij2000/?resolutionFactor=10`
- NAIF ARTEMIS archive:
  `https://naif.jpl.nasa.gov/pub/naif/THEMIS/kernels/spk/`

## Recommended Implementation Order

1. [x] Add ARTEMIS P1/P2 through NASA SSC with prediction source/fidelity
   labels, bounded generated coverage, and no runtime extrapolation. A later
   generator refinement can splice definitive SSC samples where available.
2. [x] Add a disposition overlay that corrects known SATCAT stale rows before
   calculating Moon-orbit counts.
3. Support historical-time trajectories separately from current markers,
   beginning with Chandrayaan-1.
4. Research the 23 legacy dispositions; do not synthesize lunar orbits from
   blank SATCAT summary fields.
5. Recheck Chinese and other agency portals periodically for machine-readable
   operator ephemerides.

## Primary Sources

- JPL Horizons spacecraft-data limitations and provenance:
  https://ssd.jpl.nasa.gov/horizons/manual.html
- NASA SSC Web Services:
  https://sscweb.gsfc.nasa.gov/WebServices/REST/
- NASA NAIF SPICE availability:
  https://naif.jpl.nasa.gov/naif/data_operational.html
- NASA THEMIS/ARTEMIS mission status:
  https://science.nasa.gov/mission/themis-artemis/
- NASA Chandrayaan-1 radar recovery:
  https://www.jpl.nasa.gov/news/new-nasa-radar-technique-finds-lost-lunar-spacecraft/
- CNSA Chang'e-4 landing:
  https://www.cnsa.gov.cn/english/n6465652/n6465653/c6805049/content.html
- NASA HAKUTO-R M2 impact-site observation:
  https://www.nasa.gov/missions/lro/nasas-lro-views-ispace-hakuto-r-mission-2-moon-lander-impact-site/
