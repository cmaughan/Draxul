# SatView lunar surface artifacts plan

## Goal

Add optional markers for landers, rovers, deployed instruments, impact sites,
and other human-made objects on the Moon. The markers must work in Moon globe
and Moon map views, remain geographically correct under map panning and Moon
orientation changes, participate in filtering, picking, and selection, and be
backed by public sources with explicit coordinate quality.

Keep this feature separate from the orbiting-object catalogue. A static object
on the lunar surface has no propagation state or orbit track, and multiple
artifacts may belong to one landing site without having individually known
coordinates.

## Implementation status

Implemented on 2026-07-06:

- Phase 1: deterministic LROC/GCAT generator, 70 reviewed LROC features,
  46 clustered sites, bundled source manifest, conflict report, attribution,
  runtime parser, and data tests.
- Phase 2: shared Moon-frame conversion, Vulkan/Metal procedural markers and
  independent surface-marker buffers, Moon globe/map rendering, map wrapping,
  filtering, clustering, picking, tree selection, details, and persisted UI.
- The runtime model accepts non-renderable `site_only` inventory records, but
  the broader NASA/GCAT inventory is not bundled until it has been reviewed.
- Phase 4 traverses remain a separate follow-up because the available Apollo
  and rover path products have uneven coverage and require source-specific
  licensing and quality review.

## Product shape

The first version should answer three questions cleanly:

- Where are the known human-made sites on the Moon?
- What mission and object are represented at each site?
- How precisely is the position known, and where did it come from?

At global scale, display one clustered marker per mission site. When the user
zooms in or selects the site, expose independently located objects and the
site's non-geolocated inventory as children. Do not render several coincident
markers merely because a source has separate records for a lander, attached
camera, ascent stage, and undeployed rover.

## Source survey

Survey date: 2026-07-06.

### LROC Anthropogenic Features on the Moon

Use the Lunar Reconnaissance Orbiter Camera team's point shapefile as the
coordinate authority for features it contains.

Source:

- [Product page](https://data.lroc.im-ldi.com/lroc/view_rdr/SHAPEFILE_ANTHROPOGENIC_OBJECTS)
- [PDS directory](https://pds.lroc.im-ldi.com/data/LRO-L-LROC-5-RDR-V1.0/LROLRC_2001/EXTRAS/SHAPEFILE/ANTHROPOGENIC_OBJECTS/)

The 2025-09-04 product provides both `-180..180` and `0..360` longitude
versions in the IAU Moon 2000 Geographic Coordinate System. Its fields include:

- abbreviated and full object names
- mission and object type
- latitude and east-positive longitude
- radial distance from the Moon center and its source
- coordinate source and uncertainty in meters
- arrival date
- supporting references

A local inspection of the `-180..180` archive found 70 features across 41
missions. It includes landers, rovers, deployed experiments, retroreflectors,
rocket-stage and spacecraft impact sites, and recent sites through HAKUTO-R
Mission 2. LROC deliberately excludes objects that have not been located in
LROC NAC imagery or by another accepted high-precision method.

### GCAT lander catalogue

Use Jonathan McDowell's GCAT lander catalogue for mission identity, components,
status, dates, and coverage beyond the visually confirmed LROC set.

Sources:

- [GCAT documentation](https://planet4589.org/space/gcat/root.html)
- [Lander catalogue columns](https://planet4589.org/space/gcat/web/cat/landercols.html)
- [Lander catalogue TSV](https://planet4589.org/space/gcat/tsv/cat/landercat.tsv)

GCAT is CC-BY-4.0 and is actively updated. The 2026-07-03 TSV contained 257
Moon-related component records, 191 with coordinates. It provides JCAT and
COSPAR identities, owner/country, target body, object type, landing status,
launch/landing/off dates, landing-site name, and a mission comment.

GCAT is a completeness and identity source, not the preferred coordinate
source. It expands missions into attached components and may retain approximate
or conflicting coordinates. For example, the surveyed GCAT file placed IM-2 at
29.1957 degrees west while the imagery-derived LROC product placed it at
29.19567 degrees east. LROC must win for that coordinate.

### NASA Catalogue of Manmade Material on the Moon

Use NASA's 2012 catalogue as an inventory source for Apollo and older robotic
sites, especially small artifacts that do not have independent coordinates.

Source:

- [Catalogue of Manmade Material on the Moon](https://www.nasa.gov/wp-content/uploads/2024/02/final-catalogue-of-manmade-material-on-the-moon.pdf)

The catalogue lists individual Apollo experiments, tools, cameras, memorials,
life-support equipment, descent stages, and other material. Most small items
have only a landing-site association. Store these as inventory children of the
site, not as geographic points. Do not use this old catalogue as the coordinate
authority; it predates recent missions and contains known low-precision and
erroneous coordinate entries.

### Rover and astronaut traverses

Treat paths as a later, separate data product. Relevant public sources include:

- [LROC PDS shapefile root](https://pds.lroc.im-ldi.com/data/LRO-L-LROC-5-RDR-V1.0/LROLRC_2001/EXTRAS/SHAPEFILE/)
- [USGS Apollo traverse maps](https://astrogeology.usgs.gov/search/map/moon-apollo-traverse-maps)
- [Apollo 17 controlled orthomosaic and GIS package](https://astrogeology.usgs.gov/search/map/moon_apollo_17_lroc_nac_landing_site_orthomosaic_50cm)

The PDS archive exposes machine-readable Apollo 11, 12, and 14 path data. The
Apollo 17 package includes reconstructed rover traverses and station
shapefiles. Lunokhod and newer rover routes are less consistently available and
will require mission-specific sources and quality labels.

### Secondary validation sources

Use these for manual checks and gap discovery, not as the bundled coordinate
authority:

- [NASA NSSDCA Master Catalog](https://data.nasa.gov/dataset/national-space-science-data-center-master-catalog)
- [NASA Moon Trek](https://trek.nasa.gov/)
- [NASA Moon Trek tile API](https://trek.nasa.gov/tiles/apidoc/trekAPI.html?body=moon)
- [USGS Gazetteer of Planetary Nomenclature](https://planetarynames.wr.usgs.gov/)

Avoid Wikipedia or Wikidata as source-of-record inputs. They can help discover
missing missions but should not override mission, PDS, LROC, or GCAT data.

## Source precedence and confidence

Use deterministic field-level precedence rather than choosing one source for
the whole record:

1. LROC or laser-ranging position, uncertainty, and feature radius.
2. Mission-published or PDS position when LROC has no confirmed location.
3. GCAT position as an explicitly approximate fallback.
4. Site-only association when no independent point is known.

Assign a normalized location quality:

- `confirmed`: located in imagery, laser ranging, or a controlled mission GIS
  product
- `reported`: mission/operator coordinate without independent image control
- `approximate`: catalogue or reconstructed coordinate with broad uncertainty
- `site_only`: belongs to a known site but has no independent point
- `unknown`: retained as inventory but not geographically renderable

Retain every contributing reference and preserve source values in the generated
manifest so conflicts remain auditable.

## Normalized data model

Do not add surface artifacts to `SatelliteRecord`. Add a static,
backend-neutral catalogue owned by SatView.

```text
LunarSurfaceObject
  id
  mission_id
  parent_site_id
  display_name
  vehicle_name
  kind
  status
  latitude_degrees
  longitude_east_degrees
  coordinate_uncertainty_m
  radial_distance_m
  arrival_date
  location_quality
  coordinate_source
  radius_source
  cospar_id
  gcat_id
  owner
  country
  references[]
  display_rank
```

Use normalized kinds rather than exposing source-specific strings directly:

- landing site
- lander
- rover
- deployed instrument
- retroreflector
- impact site
- rocket stage
- crewed artifact
- memorial
- attached component
- unknown

Represent traverses separately:

```text
LunarSurfacePath
  id
  mission_id
  object_id
  kind
  location_quality
  source
  references[]
  points[] { latitude_degrees, longitude_east_degrees }
```

## Offline generation pipeline

Add an explicit, reproducible generator rather than parsing shapefiles or
downloading data at application startup.

Proposed outputs:

- `assets/satview/catalog/lunar_surface_objects.csv`
- `assets/satview/catalog/lunar_surface_sources.json`
- `assets/satview/catalog/LUNAR_SURFACE_OBJECTS_ATTRIBUTION.md`
- later: `assets/satview/catalog/lunar_surface_paths.csv`

Proposed generator:

- `scripts/build_satview_lunar_surface_catalog.py`

The generator should:

1. Download pinned LROC and GCAT source versions into a temporary directory.
2. Parse structured source formats rather than scraping rendered pages.
3. Normalize all longitudes to east-positive `-180..180` degrees.
4. Group GCAT components into mission sites.
5. Match LROC features to GCAT by mission, name, date, and proximity.
6. Apply field-level source precedence.
7. Add manually reviewed aliases and conflict resolutions from a small checked-in
   override file.
8. Validate coordinate ranges, identifiers, references, and duplicate points.
9. Emit deterministic, sorted output and a source/version manifest.

Runtime startup must remain offline and deterministic. The generated catalogue
is small enough that CSV is adequate; no database or custom binary format is
needed initially.

## Coordinate frame

LROC uses the IAU Moon 2000 Geographic Coordinate System with east-positive
longitude. SatView's Moon map already treats longitude zero as Earth-facing and
contains an explicit mesh-axis sign adjustment in `render_to_lunar_body()`.

Add one shared conversion helper used by globe rendering, map rendering,
picking, label placement, and tests:

```text
lunar_surface_position(latitude, east_longitude, radius)
```

Do not independently reproduce the transform in CPU picking and Vulkan/Metal
shader code. Generate or upload Moon-body unit vectors once and transform them
through the same Moon body-to-render basis used by the Moon surface.

Initially anchor markers to the spherical Moon radius plus a small depth bias.
The current Moon surface has no terrain displacement, so applying source
elevation would make markers disagree visually with the rendered surface.

Required coordinate regression sites:

- Apollo 11: approximately 0.674 degrees north, 23.473 degrees east
- Chang'e 4: far side, approximately 177.589 degrees east
- Chandrayaan-3: approximately 69.374 degrees south, 32.319 degrees east
- Blue Ghost 1: approximately 18.562 degrees north, 61.810 degrees east

These checks should catch longitude mirroring, north/south inversion, and map
wrap errors.

## Runtime architecture

Add a small surface-catalogue component with no dependency on propagation or
the simulation worker:

- parse the generated asset during SatView initialization
- retain immutable normalized records
- build mission/site group indices once
- build a static GPU instance buffer when the catalogue or filters change
- keep current selection as a stable surface-object id

Surface markers do not belong in the lock-free propagated-satellite snapshot.
They are static scene data and should follow the same ownership pattern as the
star and constellation catalogues.

## Rendering and interaction

### Globe view

- Render constant-screen-size marker quads slightly above the Moon sphere.
- Depth-test against the Moon so far-side markers remain hidden.
- Use the existing Moon body basis rather than simulation-time propagation.
- Keep marker size stable under zoom and field-of-view changes.
- Include markers in the existing picking path.

### Map view

- Project the same Moon-body unit vector through the existing rotated map
  projection.
- Respect map-center panning and longitude wrapping.
- Draw at most one copy near either map edge, using the existing wrapped-map
  rules.

### Visibility and clustering

- Show one site marker at global scale.
- Expand independently located children past a configurable zoom threshold or
  when the site is selected.
- Keep `site_only` inventory in the tree and Selection panel without inventing
  separate positions.
- Give approximate locations a visually distinct hollow or lower-alpha marker.
- Disable minor impacts and small Apollo inventory by default.

### Colors and symbols

Use shape for object kind and color for mission family or owner. Do not encode
both concepts with color. Suggested symbols:

- lander/site: downward point or flag
- rover: small vehicle/circle-with-track indicator
- instrument/retroreflector: diamond
- impact: ring or crosshair
- crewed site: star

Implement symbols as small procedural or atlas-backed marker geometry shared by
Vulkan and Metal. Do not add one texture per object type.

## UI

### View panel

Add a persisted `Lunar surface objects` master checkbox. Add a separate
`Surface traverses` checkbox only when path data exists.

### Filter panel

Add surface-specific filters when Moon objects are relevant:

- landing sites and landers
- rovers
- instruments and retroreflectors
- impacts and rocket stages
- crewed artifacts
- approximate locations

The tree should contain a `Lunar Surface` root, mission/site nodes beneath it,
and individual artifacts beneath each site. Filtering must affect both markers
and tree contents while preserving a selected object as visible context.

### Selection panel

Show:

- mission and object name
- normalized kind and status
- latitude and east/west longitude
- arrival date
- coordinate uncertainty and quality
- coordinate source
- owner/country when available
- source references

Add a command to center the Moon map or camera on the selected site. Do not put
raw source URLs into the rendered scene; keep them in the ImGui Selection/About
UI.

## Delivery phases

### Phase 1: reviewed static catalogue

- Implement the source generator and normalized records.
- Import the 70 LROC features.
- Enrich identity from GCAT without adding lower-confidence GCAT-only points.
- Add attribution, conflict reporting, and catalogue tests.

### Phase 2: markers and selection

- Add the shared lunar surface coordinate helper.
- Render and pick markers in Moon globe and Moon map views on Vulkan and Metal.
- Add the master visibility control, basic type filters, tree nodes, and
  Selection details.
- Cluster by mission site.

### Phase 3: broader inventory

- Add reviewed GCAT-only reported/approximate sites.
- Add Apollo site inventories from the NASA catalogue as non-geolocated child
  records.
- Add source-quality visualization and selection navigation.

### Phase 4: traverses

- Import the available Apollo path shapefiles.
- Add rover/astronaut path rendering and controls.
- Research and add Lunokhod, Yutu, Yutu-2, Pragyan, and other rover paths only
  where source geometry and licensing are clear.

## Validation

### Data tests

- Generator output is byte-for-byte deterministic for pinned inputs.
- Every renderable record has finite latitude/longitude in valid ranges.
- Every record has a stable id, mission id, quality, and at least one source.
- Duplicate co-located components resolve to one global-scale site marker.
- LROC coordinates override conflicting GCAT coordinates.
- `site_only` records never enter the GPU marker buffer.

### Geometry and projection tests

- Latitude/longitude to Moon-body vector round-trips within tolerance.
- The four regression sites appear in the expected hemispheres.
- Map panning and wrapping preserve marker/texture alignment.
- Globe and map picking return the same object for the same site.
- Far-side markers fail globe visibility/depth tests.

### Rendering tests

- Vulkan and Metal consume the same marker records and symbol vocabulary.
- Marker size remains stable across viewport aspect ratios and camera FOV.
- Selection, filtering, clustering, and map wrapping do not regenerate unrelated
  orbit or star buffers.
- Add a Moon-map render scenario with known markers and a Moon-globe scenario
  with one near-side and one hidden far-side marker.

### Required repository validation

- Build `draxul` and `draxul-tests`.
- Run focused SatView catalogue/projection tests.
- Run `py do.py smoke`.
- Run Release `ctest --output-on-failure` and the relevant render scenarios.

## Documentation and licensing

- Cite Wagner et al. (2017) and the LROC/PDS product in the attribution file.
- Preserve GCAT's CC-BY-4.0 attribution and release date.
- Cite the NASA History Program catalogue for Apollo inventory records.
- Record source URLs and retrieval dates in the generated manifest.
- Confirm redistribution terms for every path dataset before bundling it.
- Update `docs/features.md` when the first user-visible marker phase lands.

## Decisions

- Static lunar surface data is a separate catalogue, not a satellite state.
- LROC is the coordinate authority where it has a confirmed feature.
- GCAT is the broad identity and completeness source.
- Site clustering is part of the data model, not a renderer-only heuristic.
- Unknown or site-only positions remain useful searchable inventory but are not
  rendered as fabricated points.
- Rover and astronaut traverses are a later independent layer.
- All runtime data is generated and bundled offline; SatView does not fetch
  shapefiles or catalogues during startup.
