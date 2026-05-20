# WI 10 — mpackvalue-uint64-cast

**Type:** bug  
**Priority:** MEDIUM (implementation-defined in C++17; sanitizers flag; returns negative for large uint64 handle IDs)  
**Platform:** all  
**Source:** review-bugs-consensus.md — BUG-11 (Gemini)

---

## Problem

`MpackValue::as_int()` in `libs/draxul-nvim/include/draxul/nvim_rpc.h` (lines 93–100) casts the `uint64_t` storage variant to `int64_t` without a range check:
```cpp
if (auto value = std::get_if<uint64_t>(&storage))
    return (int64_t)*value;
```
For values > `INT64_MAX` (e.g. large Neovim buffer/window/tabpage handles or bitmasks), the cast is implementation-defined in C++17 (defined in C++20). In practice on all current targets this wraps to negative, silently returning a negative integer to callers expecting a positive ID.

---

## Investigation

- [x] Read `libs/draxul-nvim/include/draxul/nvim_rpc.h` lines 70–115 to understand the full `MpackValue` variant and all `as_*` accessors.
- [x] Search for callers of `as_int()` in `libs/draxul-nvim/src/` and `libs/draxul-host/src/` to understand whether any caller would mishandle a negative value.
- [x] Check whether Neovim's ext types (Buffer, Window, Tabpage) use uint64 IDs that could exceed INT64_MAX in practice.

---

## Fix Strategy

Option A (safer): Add a range check in `as_int()` and throw on out-of-range:
```cpp
if (auto* v = std::get_if<uint64_t>(&storage)) {
    if (*v > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        throw std::range_error("uint64 value exceeds int64 range in as_int()");
    return static_cast<int64_t>(*v);
}
```

Option B (broader): Add an `as_uint()` accessor for callers that legitimately handle unsigned values, and migrate callers that expect unsigned IDs.

---

## Acceptance Criteria

- [x] `as_int()` does not silently return a negative value for `uint64_t` inputs > `INT64_MAX`.
- [x] UBSan/sanitizer build does not flag the cast.
- [x] All existing callers continue to work correctly.
- [x] Build and smoke test pass: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
