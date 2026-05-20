# Bug: Signed-to-int Truncation of Attr ID in ui_events.cpp

**Type:** bug
**Priority:** 3
**Source:** Claude review

## Problem

In the Neovim UI event handler (`libs/draxul-nvim/src/ui_events.cpp` or nearby), attribute IDs received from Neovim are cast like:

```cpp
int attr_id = (int)value.as_int();
```

`value.as_int()` returns a 64-bit integer (from the msgpack value). If Neovim sends a very large attribute ID (which does not happen in normal use but can happen from a malformed or extended session), the silent truncation produces a wrong attr ID that maps to an incorrect highlight. No warning is emitted.

While this is unlikely in normal use, it:
1. Produces silent data corruption when it does occur.
2. Will fire under UBSan (implementation-defined narrowing conversion for large values).

Key files:
- `libs/draxul-nvim/src/ui_events.cpp` — find `as_int()` casts for attr IDs
- `libs/draxul-grid/include/draxul/grid.h` — check the type used for attr IDs (`uint16_t`? `int`?)

## Investigation steps

- [x] Read `ui_events.cpp` and find every `as_int()` call used for attr/highlight IDs.
- [x] Check the declared type for attr IDs in `grid.h` / `highlight_table.h`.
- [x] Determine the realistic maximum attr ID Neovim will send (typically < 65536; the Neovim protocol doc gives guidance).

## Fix strategy

- [x] Replace the cast with a bounds-checked conversion:
  ```cpp
  auto raw = value.as_int();
  if (raw < 0 || raw > kMaxAttrId) {
      DRAXUL_LOG_WARN(LogCategory::NvimUi, "attr_id {} out of range; clamping", raw);
      raw = 0; // default highlight
  }
  auto attr_id = static_cast<uint16_t>(raw);
  ```
- [x] Define `kMaxAttrId` as a constant that matches the storage type (`UINT16_MAX` if attr IDs are `uint16_t`).
- [x] Apply the same pattern to any other integer narrowing casts in the same file.

## Acceptance criteria

- [x] No implicit narrowing conversion from 64-bit msgpack int to the internal attr ID type.
- [x] A `WARN` is logged when an out-of-range value is received.
- [x] UBSan does not flag this path under the `mac-asan` preset.

## Interdependencies

- `08 attr-cache-compaction -test`: that test exercises the highlight table; add a large-attr-ID edge case.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
