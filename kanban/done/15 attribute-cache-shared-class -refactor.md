# Refactor: Deduplicate Attribute Cache Logic into Shared Class

**Type:** refactor
**Priority:** 15
**Source:** Claude review

## Problem

The "get or create highlight attr ID, compact on threshold" pattern appears independently in two places:

1. `libs/draxul-host/src/terminal_host_base.cpp` — `attr_id()` method
2. `libs/draxul-nvim/src/ui_events.cpp` — `handle_grid_line()` inline logic

Both implementations:
- Walk a map from `HlAttr` → `uint16_t` ID.
- Allocate the next available ID when an attr is new.
- Trigger a full grid scan and compact the map when the count exceeds `kAttrCompactionThreshold`.

Any bug in the compaction logic (e.g. evicting a live ID) must be fixed in two places. Any future change to the compaction strategy must be applied twice.

**Note:** Do alongside `08 attr-cache-compaction -test` — single agent, refactor first then add tests.

## Proposed design

Create an `AttributeCache` class in `libs/draxul-grid/include/draxul/attribute_cache.h` (or `libs/draxul-app-support/`):

```cpp
class AttributeCache {
public:
    // Returns an existing ID or allocates a new one.
    uint16_t get_or_insert(const HlAttr& attr);

    // Called when the grid has been fully redrawn; removes IDs not in the
    // provided live set.
    void compact(const std::unordered_set<uint16_t>& live_ids);

    // Threshold after which compact() is triggered automatically.
    static constexpr size_t kCompactionThreshold = /* same value as current */;
};
```

## Implementation steps

- [x] Read both usages in `terminal_host_base.cpp` and `ui_events.cpp` — extract the exact logic.
- [x] Create `AttributeCache` class with the interface above.
- [x] Replace both existing usages with calls to the shared class.
- [x] Add the new class to the appropriate CMake library target.
- [x] Run `cmake --build build --target draxul draxul-tests` to verify the refactor compiles and tests pass.

## Implementation notes

Investigation revealed that `ui_events.cpp` does **not** contain a duplicate attr cache. The Neovim path uses pre-assigned `hl_attr_define` IDs from Neovim's RPC protocol (stored directly into `HighlightTable`), which is fundamentally different from the terminal emulator's "SGR attr -> ID" mapping. The duplication described in the original filing does not exist.

The refactor was still performed for `terminal_host_base.cpp`: the inline `attr_cache_` map, `next_attr_id_` counter, and compaction logic were extracted into `AttributeCache` in `libs/draxul-grid/include/draxul/attribute_cache.h` (header-only, no CMake changes needed). `TerminalHostBase` now delegates to `AttributeCache::get_or_insert()` and `AttributeCache::compact()`.

## Acceptance criteria

- [x] No duplication of the attr-map + compaction logic — single implementation in `AttributeCache`.
- [x] `terminal_host_base.cpp` uses the shared class. (`ui_events.cpp` uses Neovim-assigned IDs — no cache logic to deduplicate.)
- [x] All existing attr-cache tests pass against the new class (verified via `ctest`).
- [x] No functional change: visual output should be identical before and after.

## Interdependencies

- **`08 attr-cache-compaction -test`**: write tests in the same agent pass, after the refactor.
- **`03 ui-events-attr-id-truncation -bug`**: the bounds-check fix for large attr IDs belongs in the new shared class.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
