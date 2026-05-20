# 13 attr-id-unordered-map — Refactor

## Summary

`TerminalHostBase::attr_id()` in `libs/draxul-host/src/terminal_host_base.cpp` currently searches an `attr_cache_` of type `std::vector<std::pair<HlAttr, uint16_t>>` linearly for each attribute lookup:

```cpp
for (auto& [attr, id] : attr_cache_) {
    if (attr == hlattr) return id;
}
// not found: assign new id, push_back
```

For buffers with N distinct highlight attributes (LSP diagnostics, rainbow parentheses, multiple diff colors), every cell redraw performs an O(N) scan. With 20 active highlight attributes and a 200×50 grid, that is 200,000 linear scans per flush.

Replacing `attr_cache_` with `std::unordered_map<HlAttr, uint16_t, HlAttrHash>` gives O(1) amortized lookup.

**Prerequisite:** Item 07 (attr-id-cache-correctness test) must be done first. Run those tests before and after this refactor to confirm no behavioral regression.

## Steps

- [ ] 1. Read `libs/draxul-host/src/terminal_host_base.cpp` in full. Find `attr_id()` and the `attr_cache_` field declaration (likely in the corresponding `.h` or in a `private:` section of the header).
- [ ] 2. Read `libs/draxul-types/include/draxul/types.h` (or wherever `HlAttr` is defined) to see all fields of `HlAttr`. Note their types — you need to hash all of them.
- [ ] 3. Check whether `HlAttr` already has an `operator==` defined. If not, add one (or use a custom comparator in the map).
- [ ] 4. Design a hash function for `HlAttr`. Options:
  - **Option A (simple):** Pack all fields into a `uint64_t` or `__int128` if they fit, then use `std::hash<uint64_t>`. Fast and readable.
  - **Option B (general):** Use hash-combine pattern over each field:
    ```cpp
    struct HlAttrHash {
        size_t operator()(const HlAttr& a) const noexcept {
            size_t h = std::hash<uint32_t>{}(a.fg);
            h ^= std::hash<uint32_t>{}(a.bg) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint16_t>{}(a.flags) + 0x9e3779b9 + (h << 6) + (h >> 2);
            // ... repeat for each field
            return h;
        }
    };
    ```
  - Choose whichever fits the actual `HlAttr` fields cleanly.
- [ ] 5. Declare `HlAttrHash` in the same header as `HlAttr` (or in a local anonymous namespace in `terminal_host_base.cpp` if it is implementation-internal).
- [ ] 6. Replace `std::vector<std::pair<HlAttr, uint16_t>> attr_cache_` with `std::unordered_map<HlAttr, uint16_t, HlAttrHash> attr_cache_` (or `HlAttrMap` if you create a type alias).
- [ ] 7. Rewrite `attr_id()` to use the map:
  ```cpp
  uint16_t attr_id(HlAttr hlattr) {
      auto it = attr_cache_.find(hlattr);
      if (it != attr_cache_.end()) return it->second;
      uint16_t id = next_attr_id_++;
      attr_cache_.emplace(hlattr, id);
      return id;
  }
  ```
  Adjust to match the actual existing return type and parameter names.
- [ ] 8. Add `#include <unordered_map>` at the top of the .cpp file if not already present.
- [ ] 9. Build: `cmake --build build --target draxul draxul-tests`. Confirm no compile errors.
- [ ] 10. Run: `ctest --test-dir build -R draxul-tests`. ALL tests must pass, especially the tests from item 07.
- [ ] 11. Optional: add a microbenchmark comment or a `// perf: O(1) via unordered_map` comment near `attr_id()` so future readers understand the intent.
- [ ] 12. Run clang-format on all touched files.
- [ ] 13. Mark this work item complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- `attr_cache_` is an `std::unordered_map` (or equivalent O(1) structure).
- A valid `HlAttrHash` functor covers all fields of `HlAttr`.
- `HlAttr::operator==` is defined.
- All tests from item 07 pass.
- All other existing tests pass.
- No linear scan remains in `attr_id()`.

*Authored by: claude-sonnet-4-6*
