# SatView Bayer Star Labels Plan

## Status

Deferred. Implement when Bayer labels become the next SatView work item.

## Goal

Add an optional scene overlay that labels stars used by SatView's constellation
figures with their Bayer Greek letters. Labels must work in Globe and Ground
views, use Draxul's normal scene-font technology, avoid excessive clutter, and
remain consistent across Vulkan and Metal.

## Current State And Data Availability

- `assets/satview/catalog/stars.dxstar` contains direction, apparent magnitude,
  rendered color, and rendered size for up to 100,000 Hipparcos stars. Although
  the generator reads HIP identifiers, version 1 of the binary format discards
  them.
- `assets/satview/catalog/constellations.dxline` contains 656 direction pairs.
  Its generator reads Bright Star Catalogue HR identifiers and cross-matches
  them to Hipparcos astrometry, but the runtime format discards HR, HIP,
  constellation, and source-chain identity.
- ConstellationLines v1.3 uses 697 unique stars as figure endpoints. A planning
  cross-check against HYG v4.1 found Bayer designations for about 634 of them.
  The authoritative build should use the pinned Bright Star Catalogue input,
  not HYG.
- VizieR Bright Star Catalogue `V/50/catalog` has a `Name` field containing
  Bayer and/or Flamsteed designations. The existing constellation generator
  already consumes this catalogue but does not request that field.
- The shared text-atlas builder, SatView scene labels, rank-first collision
  removal, HDR composition, perspective projection, and stereographic
  projection are already implemented. No new text renderer is required.

Primary data sources:

- ConstellationLines v1.3:
  `https://github.com/MarcvdSluys/ConstellationLines/tree/v1.3`
- Bright Star Catalogue V/50:
  `https://cdsarc.cds.unistra.fr/viz-bin/cat/V/50`
- Hipparcos I/239:
  `https://cdsarc.cds.unistra.fr/viz-bin/cat/I/239`

## Scope Decisions

- Label only stars that are endpoints in the selected ConstellationLines figure
  catalogue. Do not label the complete Hipparcos starfield.
- Display Greek Bayer letters, including a component number where the source
  distinguishes entries such as beta-1 and beta-2.
- Do not fall back to Flamsteed numbers in the first implementation. A figure
  star without a Greek Bayer designation remains unlabeled.
- Keep labels independent of the `Constellation figures` toggle. A user may
  display figures, Bayer labels, both, or neither.
- Show labels in Globe and Ground views. Omit them from Earth, Moon, and Sun map
  projections.
- Default the feature off and persist its controls in `[satview]`.
- Keep the existing `DXSTAR1` GPU catalogue unchanged. Strings and identifiers
  do not belong in the 100,000-instance star upload.
- Do not change the selected constellation-figure interpretation as part of
  this work.

## Catalog Design

### New Runtime Asset

Add a small versioned binary asset:

`assets/satview/catalog/constellation_star_labels.dxbayer`

Use a normal 32-byte Draxul catalogue header with magic `DXBAYER1`. Store one
fixed-size record per unique figure star with:

- normalized J2000 render-space direction
- apparent visual magnitude
- HIP identifier when available
- HR identifier for source traceability
- three-character constellation abbreviation
- compact Bayer-letter enum
- optional component number
- reserved flags for format evolution

Keep label text out of the file. Format the compact enum into UTF-8 Greek text
at load time. This keeps the asset deterministic, compact, and independent of
font encoding.

### Generator Changes

Extend `scripts/build_satview_constellation_catalog.py` so one pinned input pass
produces both `constellations.dxline` and `constellation_star_labels.dxbayer`.

1. Request `Name` from Bright Star Catalogue V/50 in addition to HR, HD,
   RAJ2000, and DEJ2000.
2. Preserve the existing HR-to-HD-to-Hipparcos astrometry cross-match.
3. Gather the unique HR endpoints used by the ConstellationLines chains.
4. Parse Greek Bayer abbreviations from the V/50 `Name` field.
5. Map `Alp`, `Bet`, `Gam`, through `Ome` to a stable 24-value enum.
6. Preserve a Bayer component suffix where present.
7. Skip Flamsteed-only, Latin-letter, malformed, or missing designations and
   report their counts.
8. Reject duplicate `(constellation, Bayer letter, component)` assignments and
   invalid/non-finite directions.
9. Print total endpoint, Bayer-labelled, skipped, Hipparcos-cross-matched, and
   fallback-coordinate counts.

Do not infer Bayer letters from brightness order. The catalogue designation is
the authority.

## Runtime Architecture

### Loader And Neutral Records

Add `satview_constellation_star_label_catalog.*` with a neutral record containing
direction, magnitude, identifiers, constellation abbreviation, Bayer enum, and
component number. Validate magic, version, record size, enum range, finite unit
direction, magnitude, and abbreviation before accepting a record.

The loader should format display text as a Greek Unicode letter plus an optional
component suffix. Keep the constellation abbreviation in metadata for debugging
and future display modes, but initially draw only the letter and suffix.

### Text Atlas

Extend `SatViewHost::rebuild_scene_text_atlas()` with the unique Bayer label
strings. Deduplicate repeated labels before sending requests to the shared atlas
builder; the scene needs glyphs for a small alphabet, not one atlas entry per
star.

Use the existing `TextService` fallback-font path. Add explicit coverage tests
for the 24 lowercase Greek letters and the chosen component-number rendering.
If superscript digits are not reliably covered by configured fonts, render the
component number at normal baseline in the initial implementation.

### Placement And Collision Policy

Integrate Bayer stars into `SatViewHost::rebuild_scene_labels()` and the existing
screen-space collision pass:

- project the star direction with the same Globe/Ground camera contract used by
  constellation figures
- offset the label a few pixels from the star marker instead of centering text
  over it
- rank labels primarily by brighter apparent magnitude, then by HIP/HR for
  deterministic ties
- reserve Sun, Moon, cardinal, and constellation-name rectangles first
- rank Bayer labels below cardinal and constellation-name labels
- reject labels behind the Ground horizon when horizon occlusion is active
- reject labels hidden by the target globe rather than drawing through it
- rebuild only label instances when camera/projection changes; do not rebuild
  the text atlas unless font settings or the label-string set changes

Reuse the existing `SatViewLabelInstance` buffer and Vulkan/Metal label pipeline.
No backend-specific shader or material vocabulary should be introduced.

## Controls And Persistence

Add to the existing celestial-overlay section:

- `Bayer labels` checkbox, default off
- `Bayer label magnitude` slider, default `4.0`, range from the existing minimum
  star magnitude through `6.5`

Persist as:

- `bayer_labels`
- `bayer_label_max_magnitude`

The existing `Reset to defaults` action must reset both. The magnitude control
limits label eligibility only; it must not alter which stars are rendered by
the starfield.

Do not initially add font-size, opacity, abbreviation, proper-name, or Flamsteed
controls. Add those only in response to an observed usability need.

## Implementation Order

1. Add parser tests for V/50 Bayer names, all 24 Greek abbreviations, component
   suffixes, malformed names, and Flamsteed-only rows.
2. Define `DXBAYER1`, extend the generator, rebuild the asset, and update source
   attribution with transformation and omission details.
3. Add the C++ loader and catalogue-validation tests.
4. Add config fields, load/store clamping, reset behavior, and config tests.
5. Load the catalogue in `SatViewHost` and add its unique strings to the scene
   text atlas.
6. Add magnitude-ranked label candidates and collision handling.
7. Add the checkbox and magnitude slider to the celestial-overlay UI.
8. Verify Globe, perspective Ground, and stereographic Ground behavior on
   Vulkan; keep Metal on the same existing label-instance path.
9. Update `docs/features.md`, `assets/satview/README.md`, and catalogue
   attribution.

## Validation

### Automated

- Generator parser tests cover all Greek mappings and component suffixes.
- The bundled asset loads with the expected source/version and a stable record
  count.
- Every record has a finite unit direction, valid Bayer enum, valid magnitude,
  and source identifier.
- There are no duplicate star/designation records.
- Config defaults, round-trip persistence, clamping, and reset behavior pass.
- Collision tests prove brighter Bayer labels win deterministic overlaps and do
  not displace reserved body/cardinal labels.
- Existing constellation, text-atlas, projection, render, and smoke suites stay
  green.

### Visual

- Confirm familiar labels in Ursa Major, Cancer, Orion, Cassiopeia, and Crux.
- Confirm labels remain attached to stars while orbiting in Globe view.
- Confirm labels remain attached under perspective and stereographic Ground
  projection, including zenith and horizon transitions.
- Confirm Earth and other target bodies occlude labels correctly.
- Confirm the default magnitude cutoff and collision policy remain readable at
  common fields of view.
- Confirm Greek glyphs render with both the normal configured font and platform
  fallback fonts on Windows and macOS.

## Acceptance Criteria

- A persisted, default-off control displays Bayer Greek labels for available
  constellation-figure stars.
- Label positions use the same astrometry as the rendered starfield and figure
  lines.
- Missing Bayer data is handled by omission, not guessed designations.
- Labels are readable without overwhelming the sky at default settings.
- Globe and both Ground projections behave consistently on Vulkan and Metal.
- Map views remain unchanged.
- The 100,000-star render buffer and star shaders remain unchanged.
- Asset provenance and the distinction between interpretive figures, Bayer
  labels, and official IAU boundaries are documented.

## Follow-Ups

- Optional Flamsteed-number labels for figure stars without Bayer letters.
- Proper-star-name labels using IAU-approved names.
- A label-style selector for Greek-only versus Greek plus constellation
  abbreviation.
- Per-constellation visibility or selection after the figure catalogue retains
  constellation and chain identity at runtime.
- Multiple constellation-figure traditions once a second suitably licensed
  topology source is selected.
