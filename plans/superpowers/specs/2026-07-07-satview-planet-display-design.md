# SatView Planet Display Design

Date: 2026-07-07

## Goal

Improve planetary views in SatView without starting the larger physically based ring-rendering project.

The accepted direction is:

- Render Saturn's rings as several tilted, flattened annulus disks with alpha banding, rather than the current simple line circles.
- Show major moon bodies as actual geometry when zoomed out from planets, not only as marker crosses.
- Add Sun-view checkboxes for choosing which planetary orbit tracks are visible.
- Add basic selection behavior for natural bodies: single-click updates the selection data shown in the tab, and double-click switches POV to that moon.

## Saturn Ring Rendering

Use a lightweight procedural disk-ring model for Saturn. The renderer should draw a small stack of flat annulus bands in Saturn's equatorial plane, using transparent colors, gaps, and a darker Cassini-like division to create a plausible ring disk.

This should be implemented in both Metal and Vulkan scene paths. The design intentionally avoids an external art asset for now, so there is no licensing or attribution dependency. A future ray-casted ring pass can replace this representation without changing the body catalog or UI.

Uranus can keep its existing understated line-band approximation unless the implementation naturally generalizes the annulus renderer to both bodies.

## Moon Geometry Around Planets

When the active POV is a generic solar-system body such as Mars, Jupiter, Saturn, Uranus, or Neptune, the scene should render its major natural satellites as small body geometry at their current local orbit positions.

The geometry should reuse the existing solar-system body catalog and normalized body-view scale. The important user-facing behavior is that zooming out from a planet resembles the Earth view: nearby moons are visible as bodies, not only as labels or crosses.

Existing marker labels may remain as overlays if they help readability, but they should not be the only representation.

## Selection

Single-clicking a rendered natural body or its marker should set the selected object data in the SatView tab to that body.

Double-clicking the same body should change the active POV to that body, matching the user's expectation that a selected moon can be entered directly.

This pass does not need a full selection-management redesign. It should use the existing SatView tab/selection data path where practical and add a narrow mapping from rendered natural bodies to `SatViewCameraPov`.

## Planet Track Controls

In Sun POV, the View panel should expose checkboxes for the eight planet tracks:

- Mercury
- Venus
- Earth
- Mars
- Jupiter
- Saturn
- Uranus
- Neptune

The controls should persist through `config.toml`. Defaults should preserve the current visible behavior as closely as possible. If the current Sun view shows all planetary tracks, all checkboxes should default to enabled; if it only shows Earth, Earth should remain enabled and the new tracks should default off.

Include quick `All` and `None` controls only if they fit naturally into the existing ImGui panel.

## Testing

Add unit coverage for:

- Planet-track config defaults and round-trip persistence.
- Mapping enabled Sun-view planet-track checkboxes to generated orbit tracks.
- Natural-satellite selectable target mapping for generic body views.
- Saturn ring metadata or draw-record generation, depending on where the implementation boundary lands.

Run the normal Draxul build/test smoke before committing implementation changes.

## Out Of Scope

- Ray-casted physically based rings.
- Real external Saturn ring texture art.
- Full selection UX redesign.
- Changing Windows-only Vulkan infrastructure or macOS-only Metal infrastructure independently; renderer-facing behavior should remain cross-platform.
