# SatView lunar orbit catalog plan

## Goal

Extend SatView so it can show cataloged spacecraft, rocket bodies, and debris orbiting the Moon in the same product area as the existing Earth object view, while being explicit about data fidelity.

The important constraint is that there is no public Earth-style GP/TLE feed for most lunar-orbit objects. The implementation should therefore separate:

- catalog membership: “this object is/was cataloged as Moon-centered and orbiting”
- coarse orbital summaries: “SATCAT has enough period/inclination/apogee/perigee data for an approximate Moon-centered orbit”
- ephemeris-backed positions: “we have time-sampled vectors from Horizons/SPICE or an equivalent mission ephemeris”

Do not run SGP4 on lunar-orbit objects.

> Implementation correction (2026-07-05): the populated SATCAT summaries for
> DRO-A/DRO-B must not be interpreted as Moon-relative Keplerian elements. That
> produced enormous false paths. All Moon SATCAT rows therefore remain
> catalogue-only until a sourced Horizons/SPICE ephemeris is applied.

## Current source survey

### CelesTrak / Space-Track SATCAT

Use SATCAT as the primary broad inventory source.

Relevant fields:

- `OBJECT_TYPE`: `PAY`, `R/B`, `DEB`, `UNK`
- `DECAY_DATE`: blank means no listed decay/removal date
- `PERIOD`, `INCLINATION`, `APOGEE`, `PERIGEE`: sometimes available, but sparse for lunar objects
- `ORBIT_CENTER`: `MO` means Moon
- `ORBIT_TYPE`: `ORB` means orbit; `IMP`, `LAN`, `DOC`, etc. are not currently orbiting rows

Reference: [CelesTrak SATCAT format](https://celestrak.org/satcat/satcat-format.php).

Live check on 2026-07-05 against `https://celestrak.org/pub/satcat.csv`:

- `ORBIT_CENTER=MO`: 102 rows
- object types: 84 payloads, 15 rocket bodies, 3 debris
- `ORBIT_CENTER=MO`, `ORBIT_TYPE=ORB`, blank `DECAY_DATE`: 38 rows
- only 2 of those 38 rows currently have complete summary orbit fields: `DRO-A` and `DRO-B`

SATCAT is therefore good for the Moon object list and classifications, but not enough by itself to render most current lunar objects accurately.

### JPL Horizons

Use Horizons for ephemeris-backed positions for selected spacecraft where available. Horizons can generate vectors, osculating elements, and SPK files for solar-system bodies and spacecraft.

Reference: [JPL Horizons manual](https://ssd.jpl.nasa.gov/horizons/manual.html).

Horizons also accepts SGP4/SDP4 TLE input for Earth-orbiting artificial satellites, but that is not the model to use for lunar-orbit objects. For lunar missions, treat Horizons output as an external ephemeris source.

### NAIF SPICE kernels

Use SPICE as the higher-fidelity source for missions with public kernels, especially if Horizons coverage is insufficient or if we want reproducible offline assets.

Reference: [NAIF lunar mission data](https://naif.jpl.nasa.gov/naif/data_lunar.html).

Do not add a SPICE runtime dependency in the first implementation unless necessary. Prefer an offline asset-generation step that converts mission kernels or Horizons output into sampled state vectors consumed by SatView.

### Aerospace Cislunar Database

Use only as optional enrichment for cislunar mission status/planning metadata. It is useful for discovering currently on-orbit or planned XGEO, cislunar, and heliocentric missions from public sources, but it is not a direct object-by-object render ephemeris feed.

Reference: [Aerospace Cislunar Database](https://aerospace.org/cislunar-database).

### UNOOSA Register

Use only for legal/registration metadata or manual cross-checks. The register is not an orbital-state source, and the online index/export was marked unavailable during this survey.

Reference: [UNOOSA Register of Objects Launched into Outer Space](https://www.unoosa.org/oosa/en/spaceobjectregister/index.html).

### ESA DISCOS and NASA ODPO models

Useful for environment statistics and debris-model context, but not the primary implementation path for renderable Moon objects.

References:

- [ESA DISCOS statistics](https://sdup.esoc.esa.int/discosweb/statistics/)
- [NASA ODPO modeling](https://orbitaldebris.jsc.nasa.gov/modeling/)

## Data model changes

Generalize the current Earth-focused satellite records into central-body-aware space objects.

Add or confirm these concepts:

- central body: Earth, Moon, Earth-Moon Lagrange/cislunar, heliocentric, other
- object type: payload, rocket body, debris, unknown
- orbit status: orbiting, landed, impacted, docked, reentered/decayed, transferred, unknown
- source identity: SATCAT catalog number, international designator, optional Horizons/SPICE identifiers
- renderability:
  - `CatalogOnly`: visible in lists/details, not propagated
  - `SatcatSummaryEstimate`: propagated from summary orbit fields around the declared central body
  - `SampledEphemeris`: propagated by interpolating sampled vectors

Keep SSO/LEO/MEO/GEO-style Earth classification as Earth-specific derived metadata. Lunar objects need different labels such as low lunar orbit, distant retrograde orbit, near-rectilinear halo orbit, frozen lunar orbit, or “unclassified lunar orbit” where source data is insufficient.

## Ingestion plan

1. Reuse the existing SATCAT download/cache.
   - Do not download a second SATCAT file for lunar objects.
   - Parse all rows needed for Earth and Moon views.
   - Keep the source timestamp visible in diagnostics/details.

2. Change the SATCAT retention filter.
   - Earth view continues to use `ORBIT_CENTER=EA`, `ORBIT_TYPE=ORB`, blank `DECAY_DATE` for orbiting Earth objects.
   - Moon view uses `ORBIT_CENTER=MO`, `ORBIT_TYPE=ORB`, blank `DECAY_DATE`.
   - Rows with `IMP`, `LAN`, `DOC`, `R`, or `T` should be available only in historical/detail modes if added later, not in the default orbiting-object view.

3. Add a lunar ephemeris manifest.
   - Map SATCAT objects to optional Horizons/SPICE identifiers.
   - Store source, valid time range, sample cadence, frame, and central body.
   - Keep this manifest small and curated at first.

4. Add an offline ephemeris asset path.
   - Generate sampled state vectors outside the render loop.
   - Store them as compact JSON/CSV/binary assets under the SatView data/cache area.
   - Load and interpolate them in SatView.
   - Start with a curated set such as LRO, CAPSTONE, ARTEMIS P1/P2, Danuri, Chandrayaan-2, Queqiao-2, and DRO-A/DRO-B if source coverage is available.

## Propagation/rendering plan

1. Earth GP objects keep the existing SGP4 path.

2. Earth SATCAT summary objects keep the existing summary-estimate path.

3. Moon SATCAT rows remain catalogue-only regardless of summary-field completeness.
   - Do not reconstruct Moon-relative orbits from SATCAT apogee/perigee fields.
   - Upgrade a row to renderable only through a sourced sampled ephemeris.

4. Sampled ephemeris objects use interpolation.
   - Prefer source-provided vectors over conic reconstruction for NRHO, DRO, halo, and other cislunar trajectories.
   - Show stale/out-of-range ephemerides as unavailable instead of extrapolating indefinitely.

5. Catalog-only objects appear in the object tree, filters, counts, and details panel but do not produce fake scene markers.

## UI plan

Add a central-body filter near the existing population/orbit filters:

- Earth
- Moon
- All

For Moon rows, show:

- object type: payload, rocket body, debris, unknown
- SATCAT status fields
- renderability/fidelity: catalog-only, SATCAT estimate, ephemeris-backed
- central body and orbit status
- source age and source name

Scene behavior:

- Earth perspective can optionally show Moon-orbit objects near the Moon when the Moon filter is enabled.
- Moon perspective should show lunar-orbit objects and tracks around the Moon.
- Moon map/projection view can show sub-satellite tracks only for renderable objects.
- Counts should include catalog-only objects, but rendered counts should be separate so the user understands why some listed objects are not drawn.

## Tests

Add fixtures based on a small SATCAT sample containing:

- Moon orbiting payload with no summary orbit fields
- Moon orbiting payload with complete summary fields
- Moon rocket body
- Moon debris
- Moon impacted or landed row that should not appear in the default orbiting view
- Earth row proving the Earth path still works

Test coverage:

- SATCAT parser preserves `ORBIT_CENTER`, `ORBIT_TYPE`, `OBJECT_TYPE`, and summary fields.
- Moon orbit filter returns `MO + ORB + no DECAY_DATE`.
- Impacted/landed/docked Moon rows do not appear in the default orbiting list.
- Catalog-only rows are counted and selectable but not sent to propagation.
- Moon summary-estimate rows produce bounded Moon-relative positions.
- Moon-relative positions follow the Moon in the scene transform.
- Central-body and object-type filters compose correctly.
- Existing Earth SGP4/summary tests still pass.

## Phased rollout

### Phase 1: lunar catalog inventory

- Parse Moon SATCAT rows.
- Add central-body filters and Moon counts.
- Show catalog-only Moon objects in tree/details.
- No fake rendering for rows without usable orbital state.

### Phase 2: Moon summary safety

- Keep Moon SATCAT summaries catalogue-only.
- Reject false Moon-relative reconstructions in tests.
- Add clear fidelity labels.

### Phase 3: curated ephemeris-backed missions

- [x] Add the lunar ephemeris manifest.
- [x] Add an offline asset-generation path from Horizons output or SPICE-derived vectors.
- [x] Render selected active lunar missions with sampled vectors and time-bounded tracks.

The generated window covers 2026-07-01 through 2026-07-19. LRO, CAPSTONE,
Danuri, and Chandrayaan-2 Orbiter use NASA/JPL Horizons geometric Moon-centred
ICRF vectors. ARTEMIS P1/P2 use explicitly labelled NASA SSC six-year
predictions converted from geocentric GEI J2000 with matching JPL Moon vectors.
A curated disposition overlay removes Chang'e-4 and HAKUTO-R M2 from current
orbit counts using primary-source landing/impact evidence.

### Phase 4: enrichment and historical modes

- Optionally enrich details from Aerospace/UNOOSA metadata.
- Optionally add historical landed/impacted/docked lunar objects as a separate non-orbiting mode.
- Optionally add SPICE-based asset generation if the curated Horizons path is insufficient.

## Risks and decisions

- Public orbital elements for lunar objects are sparse. The UI must not imply precision when only catalog status is known.
- Horizons/SPICE identifier mapping is manual and mission-specific.
- SPICE runtime integration is cross-platform build risk; defer it unless offline conversion is not enough.
- Cislunar trajectories are often not well represented by simple Moon-centered Keplerian elements.
- Current SATCAT counts will drift. Tests should use fixtures, while diagnostics can display live-source counts.

## Acceptance criteria

- SatView has a Moon central-body filter.
- The default Moon object list includes SATCAT `MO + ORB + no DECAY_DATE` rows.
- Payloads, rocket bodies, debris, and unknowns are classified consistently with Earth objects.
- Catalog-only lunar rows are visible in lists/details but not rendered as fake positions.
- Moon SATCAT summaries remain non-renderable unless upgraded by a sourced ephemeris.
- Ephemeris-backed objects can be added through a curated manifest without changing renderer code.
- SGP4 remains Earth-only.
- Vulkan and Metal consume the same SatView scene records; no renderer-specific lunar catalog logic is added.
