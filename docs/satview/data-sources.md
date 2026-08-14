# SatView Data Sources

Last reviewed: 2026-07-08

SatView uses three categories of data:

1. live runtime feeds with local last-good caches;
2. generated offline assets committed to the repository;
3. static visual assets and computed models.

This document records where each source comes from, where Draxul stores it,
how it is updated, and its update cadence. Detailed asset licensing and image
credits remain in [`plugins/satview/assets/README.md`](../../plugins/satview/assets/README.md)
and the attribution files beside each generated asset.

## Runtime Cache Locations

The catalog and live-cloud services share the SatView cache directory:

| Platform | Cache directory |
|---|---|
| Windows | `%LOCALAPPDATA%\draxul\cache\satview` |
| Current Windows development machine | `C:\Users\cmaughan\AppData\Local\draxul\cache\satview` |
| macOS | `~/Library/Application Support/draxul/Cache/satview` |
| Linux with `XDG_CACHE_HOME` | `$XDG_CACHE_HOME/draxul/satview` |
| Linux fallback | `~/.cache/draxul/satview` |

The implementation is in
[`satview_catalog_service.cpp`](../../plugins/satview/src/services/satview_catalog_service.cpp)
and
[`satview_cloud_service.cpp`](../../plugins/satview/src/services/satview_cloud_service.cpp).

## Live Runtime Feeds

| Data | Upstream source | Local storage | Update behavior |
|---|---|---|---|
| Active satellite orbital elements | [CelesTrak GP JSON](https://celestrak.org/NORAD/elements/gp.php?GROUP=active&FORMAT=json) | `celestrak_active_gp.json` and `celestrak_active_gp.meta` in the SatView cache | Freshness threshold: **2 hours**. Checked at startup and when the user requests Refresh or presses `Ctrl+R`. It is not continuously polled merely because SatView remains open. |
| Full object catalogue | [CelesTrak SATCAT CSV](https://celestrak.org/pub/satcat.csv) | `celestrak_satcat.csv` and `celestrak_satcat.meta` in the SatView cache | Freshness threshold: **12 hours**. Checked at startup and on user refresh. Supplies payload/rocket-body/debris classification, owner, operational status, radar cross-section, and central-body metadata. |
| Current Earth cloud map | [Live Cloud Maps](https://clouds.matteason.co.uk/images/8192x4096/clouds.jpg), containing modified EUMETSAT data | `live_clouds_8192x4096.jpg` in the SatView cache | Automatically refreshed every **3 hours while SatView is running**. User refresh forces a request even when the cached image is younger. On failure, SatView keeps the cached image or bundled fallback texture. |

The catalog Refresh action is freshness-gated: it requests only CelesTrak
sources that have reached their two-hour or twelve-hour threshold. The live
cloud refresh action is a forced fetch.

The catalog service keeps GP and SATCAT caches independent. Failure of one
source does not discard the last-good data from the other.

## Generated Lunar Catalogue

SatView does not contact lunar trajectory providers at runtime. The offline
generator converts their data into a common Moon-relative sampled catalogue:

- generated asset:
  [`plugins/satview/assets/catalog/lunar_ephemeris.csv`](../../plugins/satview/assets/catalog/lunar_ephemeris.csv);
- provider/target manifest:
  [`lunar_ephemeris_targets.json`](../../plugins/satview/assets/catalog/lunar_ephemeris_targets.json);
- generator:
  [`plugins/satview/tools/build_satview_lunar_ephemeris.py`](../../plugins/satview/tools/build_satview_lunar_ephemeris.py);
- provenance:
  [`LUNAR_EPHEMERIS_ATTRIBUTION.md`](../../plugins/satview/assets/catalog/LUNAR_EPHEMERIS_ATTRIBUTION.md).

Regenerate it from the repository root with:

```powershell
py plugins\satview\tools\build_satview_lunar_ephemeris.py
```

The default generated interval begins four days before the generation date and
ends fourteen days after it. `--start` and `--stop` accept explicit ISO dates
for reproducible windows. Runtime interpolation is allowed only inside the
stored interval; SatView does not extrapolate stale mission data.

| Objects/data | Upstream source | Transformation | Update cadence |
|---|---|---|---|
| LRO, CAPSTONE, Danuri, Chandrayaan-2 Orbiter | [NASA/JPL Horizons](https://ssd.jpl.nasa.gov/horizons/) | Geometric Moon-centred ICRF position and velocity vectors are stored directly. | **Manual only:** run the generator. There is currently no scheduled regeneration. |
| ARTEMIS P1 and P2 | [NASA Satellite Situation Center](https://sscweb.gsfc.nasa.gov/WebServices/REST/) six-year predictive ephemerides plus JPL Horizons Moon vectors | SSC geocentric GEI J2000 positions are matched to JPL Moon positions, converted to Moon-relative ICRF, and assigned centred finite-difference velocities. | **Manual only:** regenerated with the same script. Rows are explicitly labelled `NASA SSC prediction`; they are not presented as reconstructed observations. |

As of the 2026-07-05 asset generation, the CSV contains 13,398 samples for six
missions, including 5,186 SSC-derived ARTEMIS samples, covering
2026-07-01 through 2026-07-19.

### Lunar Disposition Overlay

CelesTrak can retain a blank decay date and Moon-orbit classification after an
object has landed or impacted. SatView therefore applies a deliberately small,
primary-source-backed correction file before calculating current orbit counts:

- data:
  [`lunar_dispositions.csv`](../../plugins/satview/assets/catalog/lunar_dispositions.csv);
- provenance:
  [`LUNAR_DISPOSITIONS_ATTRIBUTION.md`](../../plugins/satview/assets/catalog/LUNAR_DISPOSITIONS_ATTRIBUTION.md).

Current corrections:

| NORAD | Object | Corrected disposition | Evidence source |
|---:|---|---|---|
| 43845 | Chang'e-4 | Lunar surface | China National Space Administration landing report |
| 62717 | HAKUTO-R M2 / RESILIENCE | Impacted | NASA LRO impact-site observation |

This file is updated **manually when a primary source confirms a disposition**.
It does not infer impact or escape from age, loss of contact, or a missing
ephemeris.

The broader source audit and unresolved lunar inventory are recorded in
[`plans/satview-lunar-data-source-audit.md`](../../plans/satview-lunar-data-source-audit.md).

## Mars Surface Catalogue

Mars surface markers are a curated offline catalogue, not a runtime feed:

- data:
  [`mars_surface_objects.csv`](../../plugins/satview/assets/catalog/mars_surface_objects.csv);
- provenance:
  [`MARS_SURFACE_OBJECTS_ATTRIBUTION.md`](../../plugins/satview/assets/catalog/MARS_SURFACE_OBJECTS_ATTRIBUTION.md).

The initial file contains nine historical landing sites: Viking 1, Viking 2,
Mars Pathfinder, Spirit, Opportunity, Phoenix, Curiosity, InSight, and
Perseverance. Coordinates are normalized to the same east-positive surface
catalogue schema used by the lunar surface loader, so the UI and renderer can
apply generic per-body surface filters. This catalogue is updated manually when
a primary mission or archive source supplies a new landed position; it does not
track current rover traverse positions.

## Solar-System Body Catalogue

The initial planet/major-moon view is an offline presentation catalogue in
[`satview_solar_system.cpp`](../../plugins/satview/src/core/satview_solar_system.cpp).
It contains the Sun, eight planets, and 20 major moons, including parent-body
hierarchy, equatorial/polar radii, rotation periods, and compact mean orbital
elements.

The physical and orbit values are based on JPL's
[planetary satellite mean elements](https://ssd.jpl.nasa.gov/sats/elem/sep.html),
[satellite physical parameters](https://ssd.jpl.nasa.gov/sats/phys_par/), and
[planetary orbit guidance](https://ssd.jpl.nasa.gov/planets/orbits.html), with
body-shape conventions matching NAIF
[Planetary Constants Kernels](https://naif.jpl.nasa.gov/pub/naif/toolkit_docs/C/req/pck.html).

These elements drive a small, deterministic Kepler presentation model. They
are suitable for recognizable local-system layout and closed orbit guides,
but they are not a sampled JPL ephemeris and do not include n-body
perturbations. SatView continues to use the existing sampled catalogue for
artificial lunar spacecraft. It does not invent artificial-object tracks at
other planets when no imported trajectory exists.

The same hierarchy supplies body-relative directions and apparent angular
sizes for contextual sky bodies. Planet and moon globe views render the Sun,
and major-moon views render their parent planet, on bounded proxy spheres. The
proxy distance is a depth-buffer implementation detail: its radius is derived
from the real target radius and observer distance, so the visible angular size
is preserved while interplanetary scene coordinates remain numerically stable.

The catalogue changes only with source edits and rebuilds; there is no runtime
download or scheduled cadence. A future Horizons/SPICE upgrade should replace
the approximate position provider without changing the renderer-facing body
records.

## Generated Sky Catalogues

These assets are committed to the repository, staged beside the executable,
and loaded without network access at runtime.

| Data | Upstream source | Repository storage | Update process and cadence |
|---|---|---|---|
| Stars | Hipparcos main catalogue `I/239/hip_main` through [CDS VizieR](https://cdsarc.cds.unistra.fr/viz-bin/cat/I/239) | [`stars.dxstar`](../../plugins/satview/assets/catalog/stars.dxstar) | Manual: `py plugins/satview/tools/build_satview_star_catalog.py --max-stars 100000`. No scheduled cadence. The bundled file contains the 100,000 brightest usable records. |
| Constellation figures | [ConstellationLines v1.3](https://github.com/MarcvdSluys/ConstellationLines/tree/v1.3), cross-matched through the VizieR Bright Star Catalogue and Hipparcos | [`constellations.dxline`](../../plugins/satview/assets/catalog/constellations.dxline) | Manual: `py plugins/satview/tools/build_satview_constellation_catalog.py`. The source release is pinned; no periodic update. |
| IAU constellation boundaries and names | [D3-Celestial](https://github.com/ofrohn/d3-celestial), pinned commit `7e720a3de062059d4c5400a379146a601d9010e0`; original boundary catalogue VizieR VI/49 | [`constellation_boundaries.dxbnd`](../../plugins/satview/assets/catalog/constellation_boundaries.dxbnd) | Manual: `py plugins/satview/tools/build_satview_constellation_boundary_catalog.py`. No periodic update. |

Constellation figures are an interpretive Western stick-figure set. The
separate boundary asset represents the official IAU-defined sky regions.

## Static And Generated Visual Textures

| Visual data | Upstream source | Repository storage | Update process and cadence |
|---|---|---|---|
| Earth day map | Solar System Scope, based on NASA elevation and imagery | `plugins/satview/assets/textures/earth_day_8k.jpg` | Manual replacement only. |
| Earth night map | Solar System Scope | `plugins/satview/assets/textures/earth_night_8k.jpg` | Manual replacement only. |
| Bundled fallback clouds | Solar System Scope | `plugins/satview/assets/textures/earth_clouds_8k.jpg` | Manual replacement only. This is distinct from the three-hour live-cloud cache. |
| Moon surface | [NASA SVS CGI Moon Kit](https://svs.gsfc.nasa.gov/4720/), derived from the LROC WAC mosaic | `plugins/satview/assets/textures/moon_lroc_8k.jpg` | Manual conversion/replacement only. |
| Sun surface | Solar System Scope, based on NASA imagery | `plugins/satview/assets/textures/sun_solar_system_scope_4k.jpg` | Manual replacement only. |
| Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune | [Solar System Scope](https://www.solarsystemscope.com/textures/), based on NASA elevation and imagery | `plugins/satview/assets/textures/*_solar_system_scope_2k.jpg` | Manual: `py plugins/satview/tools/build_satview_solar_system_textures.py`. URLs and SHA-256 digests are pinned. |
| Phobos, Galilean moons, Enceladus, Titan, Iapetus | [USGS Astrogeology/PDS global mosaics](https://astrogeology.usgs.gov/) | `plugins/satview/assets/textures/*_usgs_1k.jpg` | Manual: the same pinned downloader verifies the upstream browse products. |
| Incompletely mapped selected moons | Deterministic procedural presentation maps; not scientific geography | `plugins/satview/assets/textures/*_procedural_2k.jpg` | Manual: `py plugins/satview/tools/build_satview_procedural_moon_textures.py`. Stable seeds make output reproducible. |
| Milky Way background | [NASA SVS Deep Star Maps 2020](https://svs.gsfc.nasa.gov/4851/), including Gaia DR2 data | [`milky_way_nasa_4k.jpg`](../../plugins/satview/assets/textures/milky_way_nasa_4k.jpg) | Manual: `py plugins/satview/tools/build_satview_milky_way_texture.py`. The upstream OpenEXR checksum is pinned. |

Detailed licensing and transformations are in
[`plugins/satview/assets/README.md`](../../plugins/satview/assets/README.md) and the attribution
files under `plugins/satview/assets/catalog/` and `plugins/satview/assets/textures/`.

## Synthetic Offline Fixture

[`plugins/satview/assets/catalog/sample_gp.json`](../../plugins/satview/assets/catalog/sample_gp.json)
is synthetic data shaped like CelesTrak GP JSON. It is used to exercise the
catalog parser and provide a deterministic fallback only when neither live nor
cached catalogue data is usable. It is manually maintained with the parser and
tests; it is not downloaded or periodically updated.

## Computed Models

The following influence SatView output but are algorithms rather than
periodically downloaded datasets.

| Model | Source/storage | Update behavior |
|---|---|---|
| Earth satellite propagation | Vallado/CelesTrak AIAA-2006-6753 SGP4 reference implementation | SatView CMake downloads the upstream archive when the dependency is absent. The URL content is pinned by SHA-256 in `plugins/satview/cmake/Dependencies.cmake`, so it cannot silently change. It is not a runtime feed. |
| Moon position around Earth | Internal analytical lunar ephemeris in [`satview_moon_ephemeris.cpp`](../../plugins/satview/src/core/satview_moon_ephemeris.cpp) | Computed from simulation time; no network or daily data file. |
| Sun position, orientation, and Earth orbit | Internal analytical solar model in [`satview_sun_ephemeris.cpp`](../../plugins/satview/src/core/satview_sun_ephemeris.cpp) | Computed from simulation time. |
| Planet and major-moon local-system layout | Mean-element Kepler model in [`satview_solar_system.cpp`](../../plugins/satview/src/core/satview_solar_system.cpp), sourced from JPL/NAIF reference data | Computed from simulation time. Approximate presentation fidelity is explicit; no runtime update. |
| Atmosphere, ground grid, and observatory landscape | Procedural code and constants | No external source or update cadence. |

## Build-Time Staging

The source-of-truth bundled files live under `plugins/satview/assets`. When SatView is
enabled, the `stage_satview_assets` CMake target copies that directory beside
the executable:

```text
<plugin directory>/assets
```

For example, a Windows Release build reads bundled data from
`build/Release/plugins/dev.draxul.satview/assets`. Runtime live-feed caches remain in the platform
cache directory and are not written into the build tree.

## Operational Gaps And Follow-Ups

- Generated assets have no automatic regeneration job.
- Planet and major-moon tracks currently use mean-element Kepler approximations;
  a future Horizons/SPICE provider would add perturbation-aware positions and
  strict source validity metadata.
- The lunar ephemeris has strict start/end coverage, but SatView does not yet
  present a prominent asset-age or coverage-ending warning before it expires.
- The lunar disposition overlay covers only confirmed cases. Many legacy
  SATCAT Moon records still require impact/escape/orbit verification.
- Mars SATCAT `ORB` rows are retained as catalogue-only entries until a real
  Mars-relative ephemeris or trajectory source is imported.
- CelesTrak GP and SATCAT are freshness-checked at startup or user refresh;
  unlike clouds, they are not automatically polled again merely because a
  SatView pane remains open past the freshness threshold.

A useful future improvement is a Data Sources panel showing, for every source,
the last successful update, next eligible refresh, generated coverage interval,
provenance/fidelity, and whether the displayed data is live, cached,
reconstructed, predicted, or catalogue-only.
