# Abort Metal grid draw after upload failure

**Type:** bug
**Priority:** 05
**Raised by:** Claude, GPT/Codex

## Problem

`MetalGridHandle::upload_state()` retries failed growth correctly, but returns `void`. `draw_grid_handle_now()` continues and uses instance counts from the new state with the prior smaller buffer, creating a potential GPU out-of-bounds read for that frame.

## Implementation plan

- [ ] Change `upload_state()` to return a status that distinguishes empty-success from allocation/map/copy failure.
- [ ] Validate the selected buffer exists and its committed byte size covers `state_.buffer_size_bytes()`.
- [ ] In `draw_grid_handle_now()`, return `false` before encoder creation or draw submission when upload fails.
- [ ] Keep the old buffer available for retirement but never pair it with new instance counts.
- [ ] Log a rate-limited error including requested/current sizes and frame slot.
- [ ] Add an injectable Metal-buffer allocation seam or a narrow `MetalGridHandle` test hook.
- [x] Share and test pane-scissor clamping across Metal/Vulkan (`pane_scissor::clamp`,
      delivered by `7318f41b`).

## Tests

- [ ] Force a growth allocation failure and prove no draw is encoded.
- [ ] Prove the next frame retries and renders after allocation recovers.
- [ ] Cover map failure and zero-sized state.
- [x] Cover negative/off-window pane origins and zero-area intersections for Metal/Vulkan parity.

## Acceptance criteria

- [ ] A failed upload cannot submit a draw with stale capacity.
- [ ] Normal upload and retry behavior is unchanged.
- [x] Grid scissor rectangles never exceed the drawable and match the shared viewport contract.
- [ ] Build and run renderer tests/render smoke on macOS; inspect Vulkan parity.

## Dependencies and parallelism

This is a residual distinct from completed `02 metal-grid-handle-buffer-silent-fail -bug.md`, which fixed permanently committing a failed allocation size.

<model>GPT-5 Codex</model>
