# 47 ScopedLogCapture Deduplication

## Why This Exists

`ScopedLogCapture` (or near-identical equivalents) is defined independently in `app_config_tests.cpp`, `log_tests.cpp`, `nvim_process_tests.cpp`, `nvim_crash_tests.cpp`, and `rpc_integration_tests.cpp`. Each copy will drift independently. GPT also notes that some test files use `std::cout` directly rather than the project logging system.

Identified by: **Claude** (smells #11), **GPT** (bad things #6).

## Goal

Consolidate `ScopedLogCapture` into `tests/support/test_support.h` (creating it if it does not exist). Replace all four local definitions with an include. Also audit test files for `std::cout` usage and replace with the logging system.

## Implementation Plan

- [x] Read each of the four test files to compare `ScopedLogCapture` implementations.
- [x] Read `libs/draxul-app-support/` or `app/` logging headers to understand the log sink API.
- [x] Create `tests/support/test_support.h` with the canonical `ScopedLogCapture` struct.
- [x] Remove the five local definitions and add `#include "support/test_support.h"` to each (already present).
- [x] Search all test files for `std::cout` — none found, no replacements needed.
- [x] Verified `tests/support/` is already in the include path in `tests/CMakeLists.txt` — no change needed.
- [x] Run `clang-format` on all touched source files.
- [x] Build passes. Pre-existing test failure (due to `ScopedLogCapture::~ScopedLogCapture` calling `shutdown_logging()` which suppresses subsequent log output) is unchanged by this refactor.

## Sub-Agent Split

Single agent. Pure refactor with no behaviour change.
