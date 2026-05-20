# WI 11 — windows-readfile-narrowing

**Type:** bug  
**Priority:** MEDIUM (latent; harmless at current buffer sizes but formally incorrect narrowing cast)  
**Platform:** Windows only  
**Source:** review-bugs-consensus.md — BUG-12 (Claude)

---

## Problem

`NvimProcess::read()` on Windows in `libs/draxul-nvim/src/nvim_process.cpp` (lines 216–224):
```cpp
DWORD bytes_read;
if (!ReadFile(impl_->child_stdout_read_, buffer, (DWORD)max_len, &bytes_read, nullptr))
    return -1;
return (int)bytes_read;
```
`DWORD` is unsigned 32-bit; `int` is signed 32-bit. If `bytes_read > INT_MAX` the cast produces a negative value, which the RPC reader interprets as a read error. This cannot happen with the current 256 KB read buffer (`max_len` is always ≤ 256 KB), but the cast is formally incorrect and a future change to the buffer size could activate the bug silently.

---

## Investigation

- [x] Read `libs/draxul-nvim/src/nvim_process.cpp` lines 195–224 (Windows `write` and `read` functions) to confirm the cast pattern.
- [x] Check `libs/draxul-nvim/src/rpc.cpp` to confirm the read buffer size constant (expected ~256 KB).

---

## Fix Strategy

- [x] Guard the return cast:
  ```cpp
  return (bytes_read > static_cast<DWORD>(INT_MAX)) ? -1 : static_cast<int>(bytes_read);
  ```
- [x] Optionally add a `static_assert` that the read buffer size is ≤ INT_MAX to make the constraint explicit.
  - Skipped: buffer size is a runtime value (256 * 1024), not a compile-time constant. The return guard is sufficient.

---

## Acceptance Criteria

- [x] `read()` never returns a negative value due to `DWORD` → `int` narrowing for any valid read size.
- [ ] Windows build passes: `cmake --preset release && cmake --build build --config Release --target draxul`.
- [x] macOS build and smoke test also pass (no regressions): `py do.py smoke`.
