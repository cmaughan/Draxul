# WI 23 — Renderer shutdown with pending GPU frames

**Type:** test  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 5

---

## Goal

Verify that calling `shutdown()` on the renderer before `end_frame()` completes (e.g. during rapid quit or host crash) correctly waits for all GPU fences and frees all resources without use-after-free or resource leak.

---

## What to test

- [ ] Construct the renderer (headless Metal or a fake GPU backend), start a frame with `begin_frame()`, then call `shutdown()` before `end_frame()`.
- [ ] Assert: no crash, no `std::terminate` from un-joined threads.
- [ ] Assert: all GPU resources are freed — run under ASan (memory leak) and validate no leaked metal/vulkan objects in the fake backend.
- [ ] Assert: subsequent `shutdown()` calls are idempotent (double-shutdown safety).
- [ ] Verify that the 2-frames-in-flight synchronisation primitives are correctly drained — the test should not hang (add a timeout).

---

## Implementation notes

- On macOS, use the headless Metal init path (see WI 14 icebox `metal-headless-init`) if available, or use a stub `IGridRenderer` that tracks resource allocation/deallocation counts.
- The stub approach (counting `begin_frame`/`end_frame`/`shutdown` calls and asserting balanced resource alloc/free) is more portable and does not require GPU hardware in CI.
- Run under ASan: `cmake --preset mac-asan && cmake --build build --target draxul-tests && ctest -R renderer-shutdown`.
- Place in `tests/renderer_shutdown_test.cpp`.

---

## Interdependencies

- WI 85 (complete: metal-capture-semaphore-race) fixed a related semaphore race; this test would have caught that bug.
- WI 14 icebox (metal-headless-init) would enable a higher-fidelity version of this test.
- Independent of Phase 1 bugs; can be written and run at any point.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
