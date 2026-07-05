# SatView Observatory Horizon And Constellation Boundaries Plan

## Goal

Make SatView's Ground view easier to orient without obscuring the sky:

- add a sparse observatory silhouette attached to the true local horizon
- optionally label the cardinal directions
- draw the official constellation-area boundaries in the same celestial frame as
  the existing stars and constellation figures
- optionally label the constellation areas with density-aware placement

All labels are scene content rendered through Draxul's font technology and the
SatView HDR pass. They are not ImGui draw-list text or a debug overlay.

## Scope Decisions

- The observatory is a deliberately generic orientation aid, not a claimed model
  of the observer's real terrain or nearby buildings.
- The silhouette appears only in Ground view. Constellation boundaries and labels
  appear in Globe and Ground sky views, but not on the Earth/Moon/Sun map views.
- The existing `Constellation lines` feature becomes `Constellation figures` in
  the UI. Keep reading the existing `constellation_lines` config key so current
  configurations continue to work.
- Boundaries, labels, cardinal labels, and the observatory silhouette are separate
  persisted controls.
- Boundary filling, hover selection, and cultural constellation systems are
  follow-up work. This slice draws the official IAU partition as lines and names.

## Existing Foundations

Reuse the code that already establishes SatView's coordinate and projection
contracts:

- `satview_constellation_catalog.*` loads J2000 direction pairs for the existing
  figure lines.
- `satview_ground_view.*` owns observer position and local-frame camera helpers.
- `satview_sky_projection.*` is the tested CPU implementation of perspective and
  stereographic Ground projection.
- `satview_constellation.vert`, `satview_sky_projection.glsl`, and
  `satview_scene.metal` mirror that projection on the GPU.
- `SatViewScenePass` already owns the private multisampled RGBA16F scene, depth,
  tone mapping, and final presentation.
- MegaCity's `sign_label_atlas.*` already proves that `TextService` glyphs can be
  rasterized into a scene-owned texture atlas and rendered on geometry.

SatView must not depend on `modules/megacity/`. The reusable text-atlas mechanism
should move downward; the city-specific sign sizing and placement policy should
remain in MegaCity.

## Architecture

### Shared Text Atlas

Extract the generic parts of MegaCity's private sign atlas into the normal font
and type layers:

- Add backend-neutral `TextAtlasImage`, `TextAtlasEntry`, and alignment records
  under `libs/draxul-types/include/draxul/`.
- Add `TextAtlasRequest` and `build_text_atlas(TextService&, ...)` under
  `libs/draxul-font`.
- Keep the builder responsible for glyph resolution, bitmap copying, padding,
  alignment, optional elision, deterministic packing, and atlas revisioning.
- Keep target dimensions, text colors, outline policy, and world placement in the
  caller. The generic builder must not know about buildings or constellations.
- Convert MegaCity's `SignLabelRequest` into a thin city-policy adapter over the
  shared builder and preserve its current atlas output and renderer behavior.
- Replace CodeViz's private `LabelAtlasData` image record with the shared neutral
  image type so `draxul-codeviz-scene` remains independent of `draxul-font`.

SatView will own a private `TextService` initialized from the active Draxul font
path, point size, and display PPI. This mirrors MegaCity's font path while avoiding
mutation or dirty-state coupling with the terminal glyph atlas. Store the app
`TextService*` from `HostContext` only as the source of current font settings.

Override `SatViewHost::on_font_metrics_changed()` and also handle font-path changes
received through `set_imgui_font()`. Reinitialize the private service, rebuild the
small label atlas, increment its revision, and request a frame. This makes global
Draxul font-size and font-family changes apply to the sky labels.

The first catalog uses official Latin/IAU names and ASCII cardinal letters. The
shared builder should still iterate valid UTF-8 codepoints/clusters rather than
bytes so a later localized catalog does not require another atlas rewrite.

### Constellation Boundary Catalog

Add `scripts/build_satview_constellation_boundary_catalog.py` and commit its
generated binary asset under `assets/satview/catalog/`.

Use the BSD-3-Clause D3-Celestial data pinned to commit
`7e720a3de062059d4c5400a379146a601d9010e0`:

- `data/constellations.borders.json` for 257 shared boundary features
- `data/constellations.json` for area label anchors, names, designations, and ranks

The source is already converted to J2000 GeoJSON. Convert each longitude/latitude
pair into the same render-space unit direction used by the Hipparcos and current
constellation-figure builders:

```text
equatorial = (cos(dec) * cos(ra), cos(dec) * sin(ra), sin(dec))
render      = (-equatorial.y, equatorial.z, -equatorial.x)
```

D3-Celestial represents right ascension as GeoJSON longitude in `[-180, 180]`;
normalize negative longitude by adding 360 degrees before the conversion.

Create a versioned binary format with explicit offsets and record sizes:

```text
header
boundary segment records: start direction, end direction, source feature id
label records: anchor direction, rank, designation, name string offset/length
UTF-8 string table
```

The generator must:

- validate the pinned GeoJSON structure and reject missing/unexpected fields
- preserve each shared border once rather than re-emitting polygon edges
- subdivide long spherical segments to a maximum angular span of one degree so
  curved projection remains stable in wide stereographic views
- emit 89 named area anchors: one for each area, including separate Serpens Caput
  and Serpens Cauda anchors, while retaining 88 unique IAU designations
- produce deterministic byte-for-byte output from local `--borders-input` and
  `--names-input` fixtures as well as the pinned network sources
- write source, commit, transformation, and BSD-3-Clause attribution beside the
  asset and in `assets/satview/README.md`

Add `SatViewConstellationBoundaryCatalog` as a neutral loaded model containing
line segments and label metadata. Keep it separate from GPU vertex records so
catalog parsing can be unit-tested without a renderer.

### Celestial Lines

Generalize the existing constellation line path into two independently controlled
streams:

- figure lines: current 656 segments, solid and slightly brighter
- official boundaries: new catalog, thinner, dimmer, and dashed

Prefer screen-space expanded segment quads over backend line primitives. A small
shared line-instance record should carry paired directions, linear color, width,
dash length, and gap length. The Vulkan and Metal vertex shaders project both
endpoints through the existing sky projection, expand the segment to a stable
pixel width, and pass along local segment distance for the fragment shader's dash
mask. This keeps line width and dashes consistent across Vulkan, Metal, DPI, MSAA,
and both Ground projections.

Tessellation in the generated catalog keeps projection curvature smooth. Shader
visibility must reject segments crossing the perspective camera plane or the
stereographic singularity and must obey `Horizon occlusion` in Ground view.

The line draw order is:

1. Milky Way/atmosphere background
2. official boundaries
3. constellation figures
4. stars
5. constellation labels

Earth, Moon, Sun, orbit tracks, satellite markers, the physical ground, and the
foreground observatory are rendered afterward as appropriate. Existing depth
behavior continues to occlude the sky behind the Earth in Globe view.

### Constellation Labels

Build one atlas containing the 89 area names plus `N`, `E`, `S`, and `W`. Store
white glyph coverage and tint in the shader so the atlas remains reusable across
day/night backgrounds. Render labels as instanced, camera-facing quads in the HDR
scene with premultiplied alpha blending and no depth writes.

Each label instance contains:

- J2000 render direction or observer-local world direction
- atlas UV rectangle
- pixel dimensions and anchor offset
- linear text color and priority

The GPU projects the anchor through the exact same function used by stars and
constellation lines, then expands the quad in clip space using viewport pixel
dimensions. A small alpha-derived dark halo keeps light text readable over the
day sky and Milky Way without putting names in opaque boxes.

Build the visible constellation-label list on the main thread each frame. There
are only 89 candidates, so a deterministic greedy layout is inexpensive:

1. Reject labels outside the viewport, behind the camera, or below an enabled
   Ground horizon.
2. Sort by catalog rank, then designation/name for stable results.
3. Reserve a padded screen-space rectangle for each accepted label.
4. Reject lower-priority rectangles that overlap an accepted label, cardinal
   label, Sun, or Moon exclusion rectangle.

Do not hard-code a single label count per field of view. Collision removal makes
rank-1 labels dominate wide views and naturally reveals ranks 2 and 3 as the user
zooms or turns toward a smaller region.

### Observatory Horizon

Add a procedural `SatViewLandscape` model in observer-local azimuth/elevation
coordinates. It contains a low, irregular horizon profile plus a restrained set
of landmarks:

- a small observatory dome
- a telescope on a pier
- two radio dishes at different azimuths
- a thin antenna mast
- one low equipment building

Keep most of the profile below 3 degrees altitude, major equipment below 5
degrees, and only the mast near 7 degrees. Leave most azimuths visually quiet.

Generate a filled triangle mesh and a separate rim line at startup. Vertices store
local east/north/up directions, not screen positions. The shader derives the
observer basis from the Ground observer direction, converts each local direction
to render space, and uses the same perspective/stereographic projection helpers
as every other sky primitive. This keeps the horizon attached to altitude zero as
the camera crosses zenith or nadir.

Render the filled silhouette after the physical ground, tracks, and markers but
before its cardinal labels. Use a near-black neutral linear color with a faint
cool rim so it remains legible against both atmosphere and night sky. It should
not receive scene lighting, cast shadows, or write depth. A short skirt below
altitude zero overlaps the ground edge and prevents projection precision gaps.

The observatory control is independent of `Show ground` and `Horizon occlusion`:
it remains a useful reference when the physical surface is hidden or below-horizon
objects are intentionally shown.

### Cardinal Labels

Place `N`, `E`, `S`, and `W` at fixed local azimuths a few degrees above the
horizon. Convert those anchors through a tested observer-local ENU helper in
`satview_ground_view.*`, then render them with the shared scene-label pipeline.

Cardinal labels are foreground labels and are drawn after the silhouette rim.
They reserve layout rectangles before constellation names so the two systems do
not overlap. They remain upright in screen space while their anchors move with
the projected horizon.

## Render-Pass And Backend Work

Extend `SatViewScenePass` with backend-neutral setters for:

- figure and boundary line instances
- label atlas image and revision
- constellation and cardinal label instances
- observatory fill/rim geometry
- the four visibility controls

### Vulkan

- Add static/dynamic buffers for celestial lines, landscape geometry, and label
  instances.
- Add an RGBA8 UNORM label-atlas texture and descriptor binding; sample alpha as
  linear data.
- Add premultiplied-alpha label and observatory pipelines targeting SatView's
  existing RGBA16F/MSAA scene pass.
- Add the screen-space celestial-line pipeline and fragment dash mask.
- Recreate/upload only when the corresponding revision changes; only the tiny
  visible-label instance buffer is expected to update per frame.
- Include all new resources in device/render-pass teardown and resize paths.

### Metal

- Add matching MTL buffers, label texture, pipeline states, blend/depth state,
  shader records, and draw order in `satview_render.mm` and
  `shaders/satview_scene.metal`.
- Keep buffer revision and atlas revision behavior aligned with Vulkan.
- Use the active HDR target sample count for every new scene pipeline.

Do not route scene labels through the grid renderer, ImGui, NanoVG, or a backend
private renderer header. `SatViewScenePass` owns the GPU resources on both APIs.

## Controls And Defaults

Extend `SatViewConfig`, persistence, reset defaults, and the Visuals/Ground panel:

| Control | Config key | Default | Applies to |
| --- | --- | --- | --- |
| Constellation figures | `constellation_lines` | Off | Globe/Ground |
| Constellation boundaries | `constellation_boundaries` | Off | Globe/Ground |
| Constellation labels | `constellation_labels` | Off | Globe/Ground |
| Observatory horizon | `observatory_horizon` | On | Ground |
| Cardinal labels | `cardinal_labels` | Off | Ground |

Keep the existing key for figure-line compatibility. `Reset Defaults` must restore
all five values. The UI controls can remain in the existing SatView ImGui control
panel; only the user-facing scene text itself must use Draxul's font pipeline.

## Implementation Sequence

1. **Extract the shared text atlas**
   - Add neutral atlas records and the `draxul-font` builder.
   - Migrate MegaCity sign atlas generation without changing its rendering.
   - Add packing, elision, UTF-8, alignment, overflow, and deterministic-output
     tests.

2. **Add the boundary catalog tool and asset**
   - Implement pinned D3-Celestial download/local-input paths.
   - Generate the versioned binary and attribution.
   - Add parser tests for counts, unit directions, rank range, string bounds,
     Serpens split areas, malformed headers, and truncated files.

3. **Add configuration and host state**
   - Add controls, persistence, defaults, reset behavior, catalog ownership, and
     host-side dirty revisions.
   - Rename only the visible figure control; preserve its config key.

4. **Implement the celestial line path**
   - Add screen-space segment instances and matching Vulkan/Metal pipelines.
   - Move existing figure lines to the path, then add independently styled
     boundary lines.
   - Verify perspective/stereographic clipping and Ground horizon filtering.

5. **Implement shared scene labels**
   - Initialize SatView's private `TextService` from Draxul settings.
   - Build/upload the label atlas and add Vulkan/Metal instanced-quad shaders.
   - Add CPU projection/collision layout and font-change rebuild behavior.

6. **Implement the observatory landscape**
   - Generate and test the local angular profile and recognizable landmarks.
   - Add fill/rim rendering and independent visibility control.
   - Add the cardinal anchors through the shared label path.

7. **Document and validate**
   - Update `docs/features.md` and `assets/satview/README.md`.
   - Run the complete build, unit, smoke, and render validation matrix.

Keep each step buildable. The font extraction can land separately; the final
user-facing commit should include both Vulkan and Metal source paths.

## Automated Tests

- Shared text atlas packs stable UVs, preserves glyph coverage, handles UTF-8
  input, and rejects over-limit dimensions without corrupting output.
- MegaCity's migrated sign atlas retains representative dimensions and entries.
- Boundary generator/parser validates all expected records and source metadata.
- Every boundary direction and label anchor is finite and unit length.
- Local ENU cardinal anchors map to the expected north/east/south/west directions
  at the equator, mid-latitudes, and near both supported latitude limits.
- Label collision layout is deterministic, rank-prioritized, viewport-clipped,
  and reserves cardinal/body exclusion rectangles.
- Config defaults and round trips include all new controls.
- Existing Ground forward/inverse projection and constellation figure tests remain
  unchanged or are strengthened for the generalized line records.

## Visual And Runtime Validation

- Build `draxul` and `draxul-tests` in Release.
- Run focused font, SatView config/catalog, Ground, and sky-projection tests.
- Run `ctest --test-dir build --build-config Release --output-on-failure`.
- Run `py do.py smoke`.
- Run the render smoke/snapshot suite and inspect changes before blessing.
- Launch `--host satview` with Vulkan validation and exercise both Ground lenses,
  wide stereographic FOV, zenith/nadir crossing, resize, and split panes.
- Confirm the observatory stays on the true horizon and never stretches like the
  previously fixed satellite markers.
- Confirm boundary dashes/width and label pixel sizes match on Vulkan and Metal.
- Confirm Earth occludes celestial overlays in Globe view and Ground horizon
  filtering agrees for stars, figures, boundaries, and names.
- Change Draxul's font family and size at runtime and confirm scene labels rebuild
  cleanly without changing ImGui or terminal atlas ownership.
- Check day, twilight, and night backgrounds for label readability and ensure the
  halo does not become an opaque plate.
- Configure/build with `DRAXUL_ENABLE_SATVIEW=OFF` and
  `DRAXUL_ENABLE_MEGACITY=OFF` independently to catch accidental optional-module
  coupling.
- On macOS, launch and inspect the Metal path. If local Metal hardware is not
  available, require CI compilation and record runtime parity as outstanding.

## Acceptance Criteria

- Ground view has a persisted, default-on observatory silhouette that remains
  fixed to the local horizon in perspective and stereographic projection.
- Optional cardinal labels use Draxul's selected font and stay attached to their
  correct local azimuths.
- Optional official constellation boundaries cover the complete sky without
  duplicate shared edges, obvious long-segment projection chords, or seam gaps.
- Optional area names are rank-prioritized, collision-filtered, font-responsive,
  and rendered inside the HDR scene rather than through ImGui.
- Existing constellation figures retain their current catalog and appearance
  while using the generalized celestial line infrastructure.
- Vulkan and Metal implementations have matching resources, shaders, controls,
  and draw ordering.
- SatView remains independent of Megacity, and MegaCity continues to use the same
  shared Draxul font-atlas implementation for its world labels.
- Full build, smoke, unit, and render validation passes.

## Follow-Up Opportunities

- Add constellation hover/picking and a very faint selected-area fill using the
  already loaded polygon data.
- Add localized constellation names once a locale source and fallback-font policy
  are selected.
- Allow importing a real horizon obstruction profile for a known observing site;
  keep it separate from the generic observatory silhouette so the UI never
  confuses decoration with measured visibility.
- Add equatorial, ecliptic, and galactic reference grids through the same generic
  celestial-line pipeline.
