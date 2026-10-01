# Skip grids whose larger drawing allocation failed

**Summary:** Skip drawing when larger drawing memory cannot be created so a resize cannot read beyond the available storage.

**Priority:** 72  
**Severity:** CRITICAL  
**Source:** `libs/draxul-renderer/src/metal/metal_renderer.mm`

**Evidence and trigger:** B32; failed growth retains a smaller buffer, while drawing continues with the resized grid’s instance counts.

- [ ] **Investigate:** Trace slot capacity, upload success, mapping, and draw counts after resize failure.
- [ ] **Fix:** Return upload success and skip drawing until sufficient storage contains current state.
- [ ] **Acceptance:** Injected growth and mapping failures emit no draw that addresses insufficient or stale storage; later frames recover.
- [ ] **Validation:** Run core aggregate tests, same-cache smoke, and relevant macOS rendering checks.
