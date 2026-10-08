# Retain failed font-image uploads for retry

**Summary:** Retry failed font-image transfers so a temporary memory shortage does not leave text missing.

**Priority:** P2  
**Source:** `libs/draxul-renderer/src/metal/metal_renderer.mm`

**Evidence and trigger:** B33; allocation, mapping, or encoder failure loses queued glyph pixels after their original dirty state was cleared.

- [ ] **Investigate:** Trace pixel ownership and dirty-state transitions through queuing, recording, and dependent draws.
- [ ] **Fix:** Preserve uploads on failure, check encoder creation, and schedule recovery before dependent glyph drawing.
- [ ] **Acceptance:** Injected failures retain pixels and recover on a later frame without requiring font reset.
- [ ] **Validation:** Run core aggregate tests, relevant macOS text-render checks, and same-cache smoke.
