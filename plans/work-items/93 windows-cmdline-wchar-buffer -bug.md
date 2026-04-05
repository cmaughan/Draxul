# WI 93 — windows-cmdline-wchar-buffer

**Type:** bug  
**Priority:** 1 (all CLI arguments silently empty — Windows only)  
**Source:** review-bugs-consensus.md §H7 [GPT]  
**Produced by:** claude-sonnet-4-6

---

## Problem

In `app/main.cpp:45–53`, the Windows command-line argument conversion loop:

1. Calls `WideCharToMultiByte(..., argv[i], -1, nullptr, 0, ...)` to get `size` — which *includes* the NUL terminator per MSDN.
2. Allocates `size - 1` bytes.
3. Calls `WideCharToMultiByte(..., argv[i], -1, converted.data(), size - 1, ...)` — one byte short of what is needed for the NUL.

Per MSDN, when `cbMultiByte` is less than the required size, the function fails with `ERROR_INSUFFICIENT_BUFFER` and returns 0 — leaving `converted` as all NUL bytes. The return value is not checked. The result is every CLI argument (including `--host`, `--log-file`, `--command`) is silently converted to an empty string, making all CLI flags inoperative on Windows.

---

## Investigation

- [ ] Read `app/main.cpp:38–57` (Windows branch of `command_line_args()`) — confirm the buffer size, the unchecked second call, and the resulting `converted` value.
- [ ] Verify the MSDN behaviour: does `WideCharToMultiByte` with `cbMultiByte = size - 1` and `cchWideChar = -1` actually fail? Check with a debug build or add a temporary assert on the return value.

---

## Fix Strategy

- [ ] Allocate `size` bytes (not `size - 1`) and pop the trailing NUL afterward:
  ```cpp
  std::string converted(size, '\0');
  int written = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1,
                                     converted.data(), size, nullptr, nullptr);
  if (written <= 0)
  {
      DRAXUL_LOG_ERROR(LogCategory::App, "WideCharToMultiByte failed for arg %d", i);
      args.emplace_back();
      continue;
  }
  converted.pop_back(); // remove embedded NUL
  args.push_back(std::move(converted));
  ```
- [ ] Build (Windows): `cmake --build build --target draxul`
- [ ] Verify `--log-level debug` and `--host` arguments are received correctly.

---

## Acceptance Criteria

- [ ] All CLI flags (`--host`, `--log-file`, `--command`, `--log-level`) are correctly parsed on Windows.
- [ ] `WideCharToMultiByte` return value is checked; failures are logged.
- [ ] Build succeeds on Windows.
