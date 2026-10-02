# Keep configured font-image dimensions consistent
**Summary:** Use matching font-image dimensions so changing the size setting cannot break text or write outside drawing storage.

**Priority:** 82  
**Severity:** CRITICAL  
**Source:** `libs/draxul-font/src/glyph_atlas_manager.h`  
**Reported by:** Claude C1; consensus F01.

**Evidence and trigger:** The renderer receives the configured size at `app/app.cpp:483`, while cache initialization at source line 27 retains 2048. Configuring 1024 causes an oversized upload; larger sizes produce incorrect sampling coordinates.

- [x] **Investigate:** Trace dimensions through application initialization, font resets, configuration reload, and both renderer upload paths.
- [x] **Fix:** Propagate one supported dimension through font/cache and renderer setup; handle reload consistently.
- [x] **Fix:** Reject upload rectangles outside native texture bounds before recording.
- [ ] **Acceptance:** Supported non-default sizes render correct text on both backends without oversized copies; invalid uploads fail safely.
- [ ] **Validation:** Run core aggregate tests, affected text-render checks, and same-cache smoke; check Vulkan validation and Metal bounds handling.

## Implementation notes

- `TextServiceConfig::atlas_size` propagates the app's supported power-of-two dimension
  (1024, 2048, 4096, or 8192) through `TextService` and `GlyphAtlasManager` into
  `GlyphCache`. Font-size and overflow resets retain that allocation and UV scale;
  initialization after display-PPI changes also uses the configured size.
- GPU atlas resources live for the renderer's lifetime. A size-changing config reload
  now returns an explicit restart-required error before publishing any candidate
  settings/fonts; all previous settings and renderer/font dimensions remain active.
  Shutdown already rereads the on-disk config, preserving the requested atlas size for
  the next launch.
- Vulkan and Metal queue helpers validate null data, positive dimensions, source byte
  arithmetic, and texture bounds before reading/copying any data, including the path
  that patches a queued full upload. Subtraction-based bounds checks avoid signed
  overflow. Recording paths independently validate every rectangle and payload length
  before staging allocation/copy/GPU commands; Metal uses actual texture dimensions.
- App dirty-region packing checks source atlas bounds before scratch allocation and
  source pointer arithmetic.
- Added coverage for non-default dimensions and normalized glyph UVs, font-size and
  overflow resets, app startup and accepted/rejected reload transactions, unsafe
  rectangles with/without queued full uploads, and corrupt native upload payloads.

## Validation status

- Implementation reviewed locally; shared-cache aggregate, render snapshots, and smoke
  are coordinated by the parent task. No build/test cache was started by this agent.
- Metal source and shared validation paths inspected on Windows; native macOS execution
  remains unavailable here. Keep acceptance/validation gates pending until the parent
  records available evidence and any platform follow-up.
- Parent Windows `validate debug` (logs in
  `build-ninja-debug/validation-logs/20261002-114505-903646/`) compiled the slice and
  passed all five core render snapshots; the new atlas/font/app safety regressions
  passed. The aggregate failed other tests, so its gate remains open.
- Diagnosed the font/core shard failures from that run: the existing joined-family
  emoji test expects one system-font glyph but receives seven, before any atlas code
  runs; the existing remote-terminal host test times out receiving initial state.
  Each failure also reproduces alone with the existing executables (no rebuild).
  Neither failure exercises the configured-atlas changes. Both exact failures predate
  this slice and are tracked in `kanban/pending/63 joined-family-emoji-fallback -bug.md`
  and `kanban/pending/65 windows-validation-timing -test.md`; parent owns further triage.
