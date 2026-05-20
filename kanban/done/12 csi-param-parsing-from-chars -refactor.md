# 12 csi-param-parsing-from-chars — Refactor

## Summary

`TerminalHostBase::handle_csi()` in `libs/draxul-host/src/terminal_host_base.cpp` parses semicolon-delimited numeric parameters from CSI sequences. The current parsing uses:

```cpp
atoi(std::string(part).c_str())
```

This allocates a `std::string` heap object for every token (every numeric parameter in every CSI sequence). For a terminal emitting thousands of SGR color sequences per second (e.g., syntax-highlighted source code scrolling, `cat`-ing a large file with ANSI colors), this is unnecessary per-call allocation pressure.

`std::from_chars` (C++17, `<charconv>`) is the zero-allocation, locale-independent, NaN-safe replacement:

```cpp
int value = 0;
std::from_chars(part.data(), part.data() + part.size(), value);
// value == 0 on parse error, same as atoi("") == 0
```

**This refactor must be done BEFORE item 15 (handle-csi-dispatch-table)** because both modify the same function. Doing the mechanical `atoi`→`from_chars` substitution first keeps the diff minimal and the structural refactor in item 15 can proceed on clean code.

## Steps

- [x] 1. Read `libs/draxul-host/src/terminal_host_base.cpp` in full. Found the single `atoi(std::string(part).c_str())` in `handle_csi()`.
- [x] 2. Searched rest of file for other `atoi(` calls — none found.
- [x] 3. Verified C++17 is used (`set(CMAKE_CXX_STANDARD 20)` in CMakeLists.txt).
- [x] 4. Added `#include <charconv>` to `terminal_host_base.cpp`.
- [x] 5. Replaced `atoi(std::string(part).c_str())` with `std::from_chars` + comment explaining equivalence.
- [x] 6. Verified error handling equivalence: both return 0 on non-numeric/empty input.
- [x] 7. `#include <string>` was not added for this alone — kept as-is since other code uses std::string.
- [x] 8. Build: `cmake --build build --target draxul draxul-tests`. No compile errors.
- [x] 9. Run: `ctest --test-dir build -R draxul-tests`. All terminal VT tests pass.
- [x] 10. Run clang-format on `libs/draxul-host/src/terminal_host_base.cpp`.
- [x] 11. Mark this work item complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- No `atoi(std::string(` calls remain in `handle_csi()` or `terminal_host_base.cpp`.
- All replaced calls use `std::from_chars` with a zero-init value for the error/empty case.
- `<charconv>` is included.
- All existing terminal VT tests pass.
- No observable behavior change (confirmed by test suite).

*Authored by: claude-sonnet-4-6*
