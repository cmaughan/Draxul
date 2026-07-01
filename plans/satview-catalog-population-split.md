# SatView Catalog Population Split Plan

## Goal

Expand SatView from its current CelesTrak `active` GP catalog into a visibly and
filterably split view of cataloged Earth-orbiting objects:

- active payloads;
- inactive payloads;
- rocket bodies;
- debris;
- unknown cataloged objects.

Existing active objects must retain their real GP elements and SGP4 propagation.
Objects available only through SATCAT must be shown as orbit-summary estimates
and must never be presented as precise current positions.

## Data-Fidelity Decision

CelesTrak's public data divides the required information across two products:

- The `active` GP JSON feed contains complete mean elements for SGP4, but only
  for active objects with publicly available elements.
- SATCAT CSV contains the broader catalog classification, operational status,
  ownership, and summary orbit fields (`PERIOD`, `INCLINATION`, `APOGEE`, and
  `PERIGEE`), but not RAAN, argument of perigee, anomaly, epoch, or drag terms.

The implementation will merge both sources by NORAD catalog id. It will use
SGP4 wherever GP elements exist and a clearly labeled deterministic
summary-orbit visualization everywhere else. It will not synthesize OMM records
and feed them into SGP4, because that would imply accuracy the source does not
provide.

This covers individually cataloged objects only. It cannot show small untracked
fragments, dust, or debris below the catalog threshold. The UI must state that
boundary.

## Scope

### Included

- Parse current RFC 4180 SATCAT CSV by header name.
- Merge SATCAT metadata with the current `active` GP records by NORAD id.
- Cache and refresh GP and SATCAT independently.
- Classify every retained object into a user-facing population.
- Keep GP-backed records on the existing SGP4 path.
- Generate deterministic animated summary-orbit positions for renderable
  SATCAT-only records.
- Add population counts, filters, colors, tree grouping, source quality, and
  selected-object metadata to the SatView panel.
- Preserve offline startup from last-good caches.
- Keep one shared CPU scene stream for Vulkan and Metal.

### Not Included

- Authenticated Space-Track access or credential storage.
- Precise current positions for objects without public GP elements.
- Historical catalogs, conjunction analysis, collision prediction, or alerts.
- Uncataloged debris or a model for sub-catalog fragments.
- Renderer-backend-specific implementations.

## 1. Catalog Model

Add two explicit module-local enums:

```text
SatellitePopulation
  ActivePayload
  InactivePayload
  RocketBody
  Debris
  Unknown

OrbitSolutionKind
  GeneralPerturbations
  SatcatSummaryEstimate
  Sample
```

Thread both through `SatelliteRecord`, `SatellitePropagationEntry`,
`SatellitePropagatedState`, `SatelliteOrbitTrack`, filter candidates, and
selected-object details.

Retain `SatelliteObjectKind` for source-level PAY/R/B/DEB/UNK semantics.
`SatellitePopulation` is the user-facing split and distinguishes active from
inactive payloads.

Extend immutable record metadata with useful SATCAT fields:

- owner;
- operational status code;
- data status code;
- radar cross section when present;
- orbit center and orbit type;
- per-object provenance/solution kind.

Avoid copying source-label strings into every dynamic state. Store compact enums
per object and derive display labels from them. Replace the assumption that one
catalog-wide source label describes every object.

## 2. SATCAT Parsing

Add `parse_celestrak_satcat_csv()` to the catalog library. It must:

- follow RFC 4180 quoting, including commas and escaped quotes;
- resolve columns from the header rather than fixed positions;
- accept catalog ids beyond five digits;
- treat blank optional numeric fields as absent rather than zero;
- map `PAY`, `R/B`, `DEB`, and `UNK` explicitly;
- retain only non-decayed Earth-orbit records;
- report malformed, excluded, and non-renderable counts separately.

Population rules:

- A payload is active when it is present in the active GP set or its SATCAT
  status is operational, partially operational, backup/standby, spare, or
  extended mission (`+`, `P`, `B`, `S`, or `X`).
- Every other on-orbit payload is inactive.
- `R/B`, `DEB`, and `UNK` map directly to rocket body, debris, and unknown.
- Active-GP membership wins over stale or missing SATCAT status.

Keep valid catalog entries even when their summary orbit is incomplete. Count
them in catalog totals, but mark them non-renderable unless GP supplies the
missing orbit.

## 3. Fetch, Cache, and Merge

Extend `SatViewCatalogService` to own two independently cached inputs:

```text
GP:     https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=json
SATCAT: https://celestrak.org/pub/satcat.csv
```

Use separate payload and metadata files, such as
`celestrak_active_gp.json` and `celestrak_satcat.csv`. Preserve last-good files
when a fetch or parse fails and replace successful downloads atomically.

Refresh policy:

- GP: no more than once every two hours.
- SATCAT: no more than once every twelve hours, matching its manual once- or
  twice-daily update cadence.
- `Ctrl+R` requests refresh but still honors both freshness guards.
- Failure of one source must not discard valid data from the other.
- All network I/O and parsing remains off the render thread.

Merge order:

1. Parse the active GP set.
2. Parse the on-orbit SATCAT set.
3. Overlay SATCAT classification and metadata onto matching GP records while
   preserving GP orbital elements.
4. Retain GP objects not yet present in SATCAT as active payloads.
5. Add SATCAT-only records with a valid summary orbit as estimated objects.
6. Count but do not render records lacking enough orbit information.
7. De-duplicate by NORAD id.

Publish one catalog generation only after a complete merge. Status must report
each source's live/cache/sample state, age, error, object count, and merged
population/renderability counts.

## 4. Summary-Orbit Visualization

Keep Vallado SGP4 unchanged for `GeneralPerturbations` records. Add a separate
solver for `SatcatSummaryEstimate` records.

For records with perigee and apogee:

```text
rp = Earth radius + perigee
ra = Earth radius + apogee
a  = (rp + ra) / 2
e  = (ra - rp) / (ra + rp)
```

Use SATCAT inclination directly. Derive a missing period from semi-major axis
when possible, or derive a circular radius from period when altitude fields are
missing. Reject impossible values instead of clamping them into plausible-looking
orbits.

SATCAT does not supply orientation or phase. Generate RAAN, argument of perigee,
and reference mean anomaly from independent stable hashes of the NORAD id.
Advance mean anomaly from a fixed reference epoch, solve Kepler's equation,
rotate into the shared Earth-centered inertial axes, and use the existing
Earth-fixed/render-coordinate conversion.

Required behavior:

- The same catalog id produces the same geometry across runs.
- Estimated objects move continuously and repeat after their period.
- Estimated tracks use the existing selected-track path.
- States and tracks retain their solution kind through filtering and rendering.
- Estimated markers use lower alpha and are labeled `SATCAT summary estimate`.

Keep the existing general track cap. Order GP-backed records first for normal
track sampling, while selection still forces an estimated object's track into
the track set.

## 5. Filtering, Configuration, and UI

Add persistent population visibility flags, all enabled by default, plus a
persistent `show_summary_estimates` flag enabled by default. Older `[satview]`
configs that omit these keys must receive those defaults.

Add `Population` to `SatViewColorMode` and make it the default for fresh config.
Keep Name Prefix, Orbit Class, and Object Type modes. Use distinct colors for
active payloads, inactive payloads, rocket bodies, debris, and unknowns, with
estimated-object alpha as a separate fidelity cue.

Update the panel with:

- a population legend and live counts;
- population and summary-estimate checkboxes near orbit filters;
- object grouping as Population -> Orbit Class -> Name Prefix;
- GP and SATCAT cache/fetch status;
- merged, renderable, and skipped totals;
- selected population, owner/status, radar cross section, and solution label;
- a note that SATCAT-only positions are estimates and uncataloged debris is
  absent.

Change source filtering to use per-object solution/provenance rather than the
current catalog-wide source string.

## 6. Threading and Performance

The merged catalog roughly doubles the marker population, so static metadata
must not become per-tick allocation work.

- Parse, merge, and cache on the catalog worker.
- Keep metadata immutable across simulation ticks and share it by catalog
  generation where practical.
- Keep dynamic snapshots focused on numeric state and compact enums.
- Rebuild the object-tree index only when catalog generation or filter revision
  changes.
- Continue using one marker instance buffer and the capped track line stream.
- Do not add Vulkan- or Metal-specific scene paths.

Measure merge time, propagation tick time, marker upload size, and panel/tree
responsiveness against a full live SATCAT cache. If repeated strings in dynamic
states are material at this scale, move them behind immutable indexed metadata
before completion.

## 7. Tests

### Catalog Parser

- Quoted names, embedded commas, escaped quotes, blank fields, and 9-digit ids.
- PAY/R/B/DEB/UNK and active-status mapping.
- Decayed, non-Earth, landed, impacted, and docked exclusion.
- Invalid summary values counted without becoming zero-altitude objects.

### Merge and Service

- Matching GP and SATCAT rows produce one enriched GP-backed object.
- GP-only recent objects remain present.
- SATCAT-only objects get the correct population and estimate solution kind.
- Independent cache success/failure and stale/fresh combinations.
- Failed refresh preserves the prior merged catalog and generation.
- GP and SATCAT freshness intervals are independent.
- Population, renderable, and skipped counts are correct.

### Propagation

- Existing Vallado SGP4 reference tests remain unchanged and pass.
- Estimated geometry is deterministic and finite.
- Estimated radius reaches expected perigee/apogee within tolerance.
- Position repeats after one period and changes continuously between samples.
- Population and solution kind survive model, state, and track propagation.

### Filter and Config

- Each population toggle hides only that population.
- Summary estimates can be hidden independently.
- Source text matches per-object GP/SATCAT provenance.
- Population color mode and new booleans round-trip through TOML.
- Older config gets safe defaults.

### Integration Validation

- Build `draxul` and `draxul-tests` in Release.
- Run SatView catalog, propagation, filter, and config test slices.
- Run full `ctest` when shared wiring changes.
- Run `python do.py smoke`.
- Run the relevant SatView/render smoke and confirm precise and estimated
  populations render without snapshot regressions.

## 8. Documentation

Update `docs/features.md` after implementation with:

- the merged active-GP plus SATCAT catalog;
- the five categories and controls;
- independent cache cadences and offline behavior;
- precise SGP4 versus SATCAT summary-estimate fidelity;
- the exclusion of uncataloged debris.

## Implementation Order

1. Add population/solution enums, SATCAT metadata, and parser tests.
2. Implement SATCAT parsing and catalog merge as pure functions.
3. Extend the service with independent caches, refresh policy, and status.
4. Add the summary-orbit solver and propagate metadata into states/tracks.
5. Add filters, config persistence, colors, counts, grouping, and details.
6. Address measured metadata-copy or tree-index performance issues.
7. Update documentation and complete build, test, smoke, and render validation.

## Acceptance Criteria

- Online SatView presents all five population counts without duplicate ids.
- All populations are independently toggleable and Population coloring makes
  the split immediately legible.
- Existing active GP objects continue to use SGP4.
- Every SATCAT-only object is visibly and textually identified as an estimate.
- Decayed and non-Earth objects are not rendered around Earth.
- A selected estimated object shows an estimated path and SATCAT metadata.
- Offline startup uses both caches; one-source failure does not erase the scene.
- Refresh respects two-hour GP and twelve-hour SATCAT guards.
- The full catalog remains responsive with existing marker/path limits.
- Windows/Vulkan and macOS/Metal consume the same shared scene data.
- Targeted tests, Release build, app smoke, and render smoke pass.

## Risks and Mitigations

- **Estimates mistaken for tracked positions:** use a solution enum, lower alpha,
  explicit labels, and a visibility toggle.
- **Sources update at different times:** merge by id, let GP membership override
  stale status, show both ages, and publish atomically.
- **CelesTrak rate limiting:** cache independently, honor source cadence, retain
  last-good data, and never retry from the render loop.
- **Full-catalog CPU/UI cost:** cap paths, keep metadata immutable, make tree
  rebuilds revision-driven, and test with a real full cache.
- **CSV schema evolution:** resolve named headers, tolerate unknown columns, and
  report missing required columns clearly.

## Source References

- CelesTrak current GP sets: https://celestrak.org/NORAD/elements/
- CelesTrak GP format and access guidance:
  https://celestrak.org/NORAD/documentation/gp-data-formats.php
- CelesTrak SATCAT and population summary: https://celestrak.org/satcat/
- CelesTrak SATCAT format: https://celestrak.org/satcat/satcat-format.php
- CelesTrak usage policy: https://celestrak.org/usage-policy.php
