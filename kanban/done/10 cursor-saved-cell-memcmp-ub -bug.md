# 10 Cursor Saved Cell Memcmp UB

## Why This Exists

`RendererState` saves the cell under the block cursor into `cursor_saved_cell_` and restores it when the cursor moves. To detect whether a restore is necessary, the code uses `std::memcmp` to compare two `GpuCell` structs.

`GpuCell` contains explicit `_pad` fields (`_pad0`, `_pad1`, `_pad2`) whose bytes are indeterminate at runtime. Comparing indeterminate bytes with `std::memcmp` is undefined behaviour in C++. A conforming compiler or sanitiser may produce incorrect results or trigger UB sanitiser warnings.

**Source:** `libs/draxul-renderer/src/renderer_state.cpp` — the `memcmp` comparison of `GpuCell`.
**Raised by:** Claude (primary), GPT (noted padding brittleness).

## Goal

Replace the raw `memcmp` comparison with a well-defined field-by-field `operator==` on `GpuCell`, or alternatively ensure the struct is always zero-initialised so padding bytes are deterministic and the `memcmp` is defined.

## Implementation Plan

- [x] Read `libs/draxul-renderer/src/renderer_state.cpp` and located the `memcmp` call.
- [x] Read `libs/draxul-renderer/src/renderer_state.h` to confirm the `GpuCell` layout, including the explicit `_pad` fields.
- [x] Chose **Option A**: add `bool operator==(const GpuCell&, const GpuCell&)` that compares only the meaningful fields and replace `memcmp` with that operator.
- [x] Replaced the `memcmp` call with the field-based equality operator.
- [x] Ran `clang-format` on touched files.
- [x] Ran `ctest --test-dir build --build-config Release --output-on-failure` to verify no regressions.

## Tests

- [x] The existing renderer state tests (`draxul-tests`) passed.
- [x] The existing `RendererState` block-cursor test still exercises the save/restore path.
- [x] Added a focused unit test proving `GpuCell` logical equality ignores `_pad` differences while still catching meaningful field changes.

## Sub-Agent Split

Single agent. This is a small, contained change in `renderer_state.cpp` and `types.h`.
