---
# WI 76 — Separate CLI Argument Parsing from Process Termination in `main.cpp`

**Type:** refactor  
**Priority:** low-medium (improves testability of argument parsing)  
**Raised by:** [P] GPT  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

## Status

**Completed** — `app/main.cpp` now calls `draxul::parse_args()` (line 135) which returns a `ParseArgsResult` struct (`ParsedArgs args` + error/help fields). No `std::exit()` calls remain inside the parser; `main()` inspects the result and decides whether to exit. CLI parsing is now testable in isolation.

---

## Problem

`app/main.cpp` mixes argument parsing with `std::exit()` calls. When `--help` or an invalid argument is encountered, the parser calls `std::exit()` directly inside parsing logic. This makes argument parsing:
- Impossible to unit test (would terminate the test process)
- Non-reusable from other entry points (e.g. a render-test harness or a future CLI tool)
- Hard to extend with custom error messaging

Reference: `app/main.cpp` line ~93.

---

## Investigation Steps

- [ ] Read `app/main.cpp` to identify all `std::exit()` call sites within argument parsing
- [ ] Identify what return type / error model the parsing function currently uses
- [ ] Check `app/app_options.h` to understand what the parser returns

---

## Proposed Design

Refactor argument parsing into a function that returns a result type instead of exiting:

```cpp
struct ParseResult {
    AppOptions options;
    bool help_requested{false};
    std::string error_message; // empty = success
};

ParseResult parse_args(int argc, char** argv);
```

`main()` then inspects `ParseResult` and calls `std::exit()` itself — keeping exit decisions at the top level where they belong.

---

## Implementation Steps

- [ ] Extract the argument parsing body into `ParseResult parse_cli_args(int argc, char** argv)` (can remain in `main.cpp` as a static function, or move to a new `app/cli_args.h/.cpp`)
- [ ] Replace all internal `std::exit()` calls in the parser with setting `error_message` and returning
- [ ] Update `main()` to inspect `ParseResult` and call `std::exit()` or print help as appropriate
- [ ] Write a unit test (`tests/cli_args_test.cpp`) covering: help flag, unknown flag, missing value, valid combinations

---

## Acceptance Criteria

- [ ] No `std::exit()` inside the argument parser function
- [ ] Unit tests for `parse_cli_args()` pass without process termination
- [ ] `--help` still prints help and exits (from `main()`, not from the parser)
- [ ] Invalid arguments still exit with code 1 and an error message

---

## Notes

Small, self-contained change. No interdependencies. Good candidate for a focused single-agent pass.
