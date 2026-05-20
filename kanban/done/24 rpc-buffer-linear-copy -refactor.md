# RPC Buffer Linear Copy Refactor

**Type:** refactor
**Priority:** 24
**Raised by:** GPT, Gemini

## Summary

Replace the O(n) buffer-copy-on-consume pattern in `libs/draxul-nvim/src/rpc.cpp` with a `read_pos` offset approach that avoids moving data until the buffer is fully drained. This eliminates the quadratic-copy performance regression under sustained high-frequency RPC traffic.

## Background

At line 143 of `libs/draxul-nvim/src/rpc.cpp`, the accumulation buffer is modified with `accum.erase(accum.begin(), accum.begin() + consumed)` after every decoded message. `std::vector::erase` at the beginning of a vector copies all remaining bytes forward — O(n) work per message, O(n×m) total for a burst of m messages. Under a large neovim redraw (thousands of `grid_line` messages), this degrades noticeably. The fix is to track how far into the buffer the decoder has consumed using a `size_t read_pos` offset, and only reset the buffer (setting `read_pos = 0` and clearing/resizing the vector) when it is fully drained.

## Implementation Plan

### Files to modify
- `libs/draxul-nvim/src/rpc.cpp` — replace the `erase` call with offset-based tracking
- `libs/draxul-nvim/src/rpc.h` (or equivalent) — add `size_t read_pos_` member if state is held across read calls

### Steps
- [x] Add a `size_t read_pos_` member to the RPC reader class (or as a local variable if the buffer is drained within a single call)
- [x] Replace `accum.erase(accum.begin(), accum.begin() + consumed)` with `read_pos_ += consumed`
- [x] Pass a sub-span (pointer + remaining length) to the mpack decoder instead of a vector beginning
- [x] When the buffer is fully drained (`read_pos_ == accum.size()`), reset: `accum.clear(); read_pos_ = 0;`
- [x] When new bytes arrive and `read_pos_ > 0` but `read_pos_ < accum.size()` (partial message in buffer), either: (a) compact on each new read call, or (b) allow the buffer to grow and compact only when it exceeds a threshold (e.g., 64 KB)
- [x] Run `ctest` to verify message decoding is unchanged
- [x] Run the RPC burst test (work item `18`) to measure the performance improvement

## Depends On
- None

## Blocks
- `17 rpc-backpressure -test.md`
- `18 rpc-burst-transport -test.md`

## Notes
The compaction threshold approach (option b above) avoids pathological fragmentation in the degenerate case where one very large message is being assembled across many small reads. A 64 KB threshold means the buffer is compacted at most once every 64 KB of received data, which is negligible.

> Work item produced by: claude-sonnet-4-6
