# WI 62 — host-boundary-chunk-submission

**Type**: refactor  
**Priority**: 9 (live encoding still left all GPU work in one final submit)  
**Source**: direct architecture follow-up  
**Produced by**: codex

---

## Problem

After live frame encoding landed, hosts encoded their work immediately, but the renderer still submitted only once at `end_frame()`.

That meant:

- the GPU still waited until the CPU had finished the whole frame before starting
- the app could not pipeline "finished host work" against later host preparation

The next step was to submit completed work at host boundaries while still presenting only once at frame end.

---

## Tasks

- [x] Add a host-boundary chunk flush API to `IFrameContext`.
- [x] Update `App` so it flushes completed pane-host chunks as it advances through the frame.
- [x] Flush the diagnostics host as its own chunk and keep the command palette as the final overlay chunk.
- [x] Implement chunk commit on Metal so completed chunks are committed early and the last chunk presents.
- [x] Keep grid upload timing host-local.
- [x] Mirror the chunked model in the Vulkan backend structure, including load-capable later window passes.
- [x] Update test fakes for the new frame-context API.
- [x] Build and validate on the active platform.

---

## Acceptance Criteria

- Hosts no longer wait for `end_frame()` before their completed work can be submitted.
- `App` flushes submission chunks at host boundaries.
- The final composed frame is still presented once.
- `cmake --build build --target draxul draxul-tests` passes.
- `python3 do.py smoke` passes.
- `ctest --test-dir build --output-on-failure` passes.

---

## Status

Implemented.

On the validated macOS / Metal path:

- completed host chunks are committed as the app advances to the next host
- the last open chunk is submitted and presented in `end_frame()`
- render snapshots and tests remain green

The Vulkan source was updated to the same chunked model, including a load-capable later main render pass, but that backend was not compiled locally on this machine.
