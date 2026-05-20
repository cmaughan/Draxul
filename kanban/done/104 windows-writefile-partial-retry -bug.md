# WI 104 — windows-writefile-partial-retry

**Type:** bug  
**Priority:** 2 (RPC stream corruption on partial write; Windows-only)  
**Source:** review-bugs-consensus.md §M1 [Claude]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`NvimProcess::write()` on Windows (`libs/draxul-nvim/src/nvim_process.cpp:184–188`):

```cpp
bool NvimProcess::write(const uint8_t* data, size_t len) const
{
    DWORD written;
    return WriteFile(impl_->child_stdin_write_, data, (DWORD)len, &written, nullptr) && written == len;
}
```

`WriteFile` to an anonymous pipe can return `TRUE` with `written < len` (a partial write) when the
pipe's kernel buffer is nearly full. The current code returns `false` in that case, signalling
failure rather than retrying. This incorrectly kills the RPC stream when it should complete the
write.

This is distinct from WI 97 (which addresses callers ignoring the return value). Even after WI 97 is
fixed, a partial write causes the caller to trigger a disconnect rather than retrying.

The POSIX implementation (lines 376–386) correctly loops until all bytes are delivered.

**Trigger:** Sending a large RPC request (e.g. pasting many lines) when the pipe's kernel buffer is
nearly full under Windows.

---

## Investigation

- [ ] Read `libs/draxul-nvim/src/nvim_process.cpp:184–188` (Windows path) — confirm `WriteFile` is
  not retried.
- [ ] Read `libs/draxul-nvim/src/nvim_process.cpp:376–386` (POSIX path) — use as reference for the
  loop pattern.
- [ ] Check whether `DWORD len` cast is safe for very large buffers (msgpack frames are typically
  small, but paste events can be large).

---

## Fix Strategy

Replace the Windows `write()` implementation with a retry loop mirroring the POSIX path:

```cpp
bool NvimProcess::write(const uint8_t* data, size_t len) const
{
    size_t total_written = 0;
    while (total_written < len)
    {
        DWORD written = 0;
        DWORD to_write = static_cast<DWORD>(
            std::min<size_t>(len - total_written, MAXDWORD));
        if (!WriteFile(impl_->child_stdin_write_,
                       data + total_written, to_write, &written, nullptr)
            || written == 0)
            return false;
        total_written += written;
    }
    return true;
}
```

- [ ] Apply the fix.
- [ ] Build (Windows): `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] `NvimProcess::write()` on Windows loops until all bytes are written or a genuine error occurs.
- [ ] A partial `WriteFile` result no longer causes a false-failure return.
- [ ] Smoke test passes.

---

## Interdependencies

- **WI 97** (nvim-write-return-unchecked) — both touch `NvimProcess::write` and its callers; fix in
  the same pass.
