# Keep configured font-image dimensions consistent
**Summary:** Use matching font-image dimensions so changing the size setting cannot break text or write outside drawing storage.

**Priority:** 82  
**Severity:** CRITICAL  
**Source:** `libs/draxul-font/src/glyph_atlas_manager.h`  
**Reported by:** Claude C1; consensus F01.

**Evidence and trigger:** The renderer receives the configured size at `app/app.cpp:483`, while cache initialization at source line 27 retains 2048. Configuring 1024 causes an oversized upload; larger sizes produce incorrect sampling coordinates.

- [ ] **Investigate:** Trace dimensions through application initialization, font resets, configuration reload, and both renderer upload paths.
- [ ] **Fix:** Propagate one supported dimension through font/cache and renderer setup; handle reload consistently.
- [ ] **Fix:** Reject upload rectangles outside native texture bounds before recording.
- [ ] **Acceptance:** Supported non-default sizes render correct text on both backends without oversized copies; invalid uploads fail safely.
- [ ] **Validation:** Run core aggregate tests, affected text-render checks, and same-cache smoke; check Vulkan validation and Metal bounds handling.
