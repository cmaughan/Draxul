# SatView named view bookmarks

**Type:** feature
**Priority:** 39
**Raised by:** Claude

## User need

SatView now has globe, map, and ground viewpoints plus arbitrary time/selection state. Users need named bookmarks for interesting observations without rebuilding the view manually.

## Bookmark schema

- View mode and central body.
- Camera quaternion/distance or map center/ground observer plus look quaternion and projection settings.
- Simulation instant, pause/speed choice, and optional selected object id.
- Schema version, display name, and creation/update time.

## Implementation plan

- [ ] Land current observatory work and SatView host smoke/item 18; prefer item 26 boundaries if already underway.
- [ ] Add a versioned SatView-owned bookmark store separate from general defaults; use atomic writing from item 02's helper.
- [ ] Add Save/Apply/Rename/Delete controls to the SatView panel with duplicate-name and invalid-object handling.
- [ ] Apply a bookmark as one transaction, recomputing camera/projection/paths and issuing one frame request.
- [ ] If a selected object is absent, restore the view/time and report the missing selection without failing the bookmark.
- [ ] Keep bookmarks local and data-only; no downloaded catalog payload is embedded.
- [ ] Document storage location and behavior in `docs/features.md`.

## Tests and acceptance

- [ ] Round-trip every view mode/projection and schema migration/unknown fields.
- [ ] Test missing/corrupt store, duplicate names, missing selected object, and atomic failure.
- [ ] Verify Vulkan and Metal consume the same restored scene state.
- [ ] SatView tests, startup/render check, `ctest`, and smoke pass.

## Dependencies and parallelism

Depends on active SatView work and item 18; easier after item 26 separates host/config from renderer.

<model>GPT-5 Codex</model>
