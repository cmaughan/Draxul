# ConPtyProcess::write: silent short write on Windows

**Severity:** HIGH (Windows only)  
**File:** `libs/draxul-host/src/conpty_process.cpp:259–267`  
**Source:** review-bugs-consensus BUG-06 (gpt)

## Bug Description

`ConPtyProcess::write()` performs a single `WriteFile` call and treats `written < text.size()` as a hard failure:

```cpp
return WriteFile(input_write_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr)
    && written == static_cast<DWORD>(text.size());
```

On Windows named pipes and ConPTY pseudo-console handles, `WriteFile` may succeed (`return TRUE`) with `written < text.size()` when the pipe buffer is near capacity. The current code returns `false` in that case, but the partially written bytes are already consumed. Callers do not retry, so the trailing portion of a large paste or startup command burst is silently discarded.

`NvimProcess::write()` (`nvim_process.cpp:187`) already uses the correct retry-loop pattern.

**Trigger:** Paste or programmatic write larger than the ConPTY pipe buffer (typically 4–64 KB) into a Windows or WSL shell pane.

## Investigation

- [ ] Confirm `WriteFile` on a ConPTY pipe can return `TRUE` with `written < size` (MSDN documents this for pipes)
- [ ] Read `NvimProcess::write()` retry loop pattern to understand the exact retry contract
- [ ] Check all call sites of `ConPtyProcess::write()` — confirm none handle partial write specially

## Fix Strategy

- [ ] Replace the single `WriteFile` call with a retry loop matching `NvimProcess::write()`:
  ```cpp
  bool ConPtyProcess::write(std::string_view text)
  {
      if (input_write_ == INVALID_HANDLE_VALUE) return false;
      const char* ptr = text.data();
      DWORD remaining = static_cast<DWORD>(text.size());
      while (remaining > 0) {
          DWORD written = 0;
          if (!WriteFile(input_write_, ptr, remaining, &written, nullptr))
              return false;
          if (written == 0)
              return false;  // unexpected zero-byte write
          ptr += written;
          remaining -= written;
      }
      return true;
  }
  ```
- [ ] Keep the `INVALID_HANDLE_VALUE` guard unchanged

## Acceptance Criteria

- [ ] A write of 128 KB into a ConPTY handle that only accepts 4 KB per `WriteFile` call delivers all bytes
- [ ] Build succeeds on Windows with the retry loop
- [ ] Smoke test passes on Windows: large paste into a WSL/CMD pane produces complete output
- [ ] `written == 0` on a valid handle is treated as a hard failure (not an infinite loop)
