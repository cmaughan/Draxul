# Bug: Unbounded reserve() on msgpack array/map count can exhaust memory

**Severity**: HIGH
**File**: `libs/draxul-nvim/src/mpack_codec.cpp:160,175`
**Source**: review-bugs-consensus.md (H9)

## Description

In `read_value`, the array and map branches call:
```cpp
items.reserve(count);  // count comes directly from the msgpack stream
```
`count` is a `uint32_t` from the wire format. If Neovim sends a corrupt, truncated, or abnormally large value (e.g., due to a bug in nvim, OOM, or crafted input), `reserve` attempts to allocate `count * sizeof(element)` bytes. For `count == UINT32_MAX` (~4 billion) this requests gigabytes and throws `std::bad_alloc`, crashing the process.

## Trigger Scenario

Neovim sends a corrupt msgpack message with a very large array or map count. This can happen on:
- Neovim OOM condition producing a garbled RPC write
- A bug in the msgpack serialisation path
- A corrupted pipe buffer

## Fix Strategy

- [x] Cap the reserve to a practical maximum:
  ```cpp
  constexpr uint32_t kMaxReserveCount = 65536;
  items.reserve(std::min(count, kMaxReserveCount));
  ```
  The container will grow as items are actually decoded; the cap only prevents the upfront allocation explosion.
- [x] Define `kMaxReserveCount` as a named constant near the top of the file for visibility
- [x] Verify the existing per-item `mpack_reader_error` check (lines 164, 180) will correctly truncate if the actual element count is less than `count`

## Acceptance Criteria

- [x] Feeding a msgpack message with `count == UINT32_MAX` does not throw `std::bad_alloc` or OOM
- [x] Normal Neovim messages with small counts are unaffected (no extra allocations)
- [x] Unit test: construct a synthetic msgpack array with a very large declared count and verify graceful handling
