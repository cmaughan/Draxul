# mpack_codec: silent size_t → uint32_t truncation on encode

**Severity:** MEDIUM  
**File:** `libs/draxul-nvim/src/mpack_codec.cpp:79,82,88`  
**Source:** review-bugs-consensus BUG-09 (claude)

## Bug Description

Three narrowing casts in `write_value()` silently truncate `size_t` to `uint32_t`:

```cpp
mpack_write_str(writer, val.as_str().c_str(), (uint32_t)val.as_str().size());
mpack_start_array(writer, (uint32_t)val.as_array().size());
mpack_start_map(writer, (uint32_t)val.as_map().size());
```

If a string, array, or map ever exceeds `UINT32_MAX` elements/bytes, the length written to the wire would be `real_size % 2^32`, producing a frame-length mismatch that corrupts all subsequent RPC messages for the session.

In current practice this is unreachable: inbound strings are bounded to 64 MB by the decoder, and outbound strings (Lua snippets, key names) are tiny. But the casts are unguarded and will silently corrupt if the invariant ever changes.

**Trigger:** Outbound RPC message with a string > 4 GB or an array/map with > 4 billion elements.

## Investigation

- [ ] Confirm the maximum outbound string size constructed by `dispatch_action`, `nvim_api_call`, and other RPC callers
- [ ] Check whether `mpack_write_str`/`mpack_start_array` have any internal range check that would catch oversized inputs

## Fix Strategy

- [ ] Add `assert` guards before each cast:
  ```cpp
  assert(val.as_str().size() <= UINT32_MAX);
  mpack_write_str(writer, val.as_str().c_str(), static_cast<uint32_t>(val.as_str().size()));
  
  assert(val.as_array().size() <= UINT32_MAX);
  mpack_start_array(writer, static_cast<uint32_t>(val.as_array().size()));
  
  assert(val.as_map().size() <= UINT32_MAX);
  mpack_start_map(writer, static_cast<uint32_t>(val.as_map().size()));
  ```
- [ ] Replace the C-style `(uint32_t)` casts with `static_cast<uint32_t>` to make the narrowing explicit and searchable
- [ ] Do NOT add error-return paths here; `assert` is sufficient since this is an internal encoding path, not user input

## Acceptance Criteria

- [ ] All three C-style casts replaced with `static_cast<uint32_t>` with preceding `assert`
- [ ] `draxul-tests` RPC encode/decode round-trip tests pass
- [ ] No new compiler warnings introduced
