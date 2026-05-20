# 26 AppConfig / AppOptions Field Deduplication

## Why This Exists

`AppOptions` (parsed from command-line arguments) and `AppConfig` (parsed from `config.toml`) share many of the same fields. Currently many `AppOptions` fields are copies of `AppConfig` fields, maintained separately and merged manually in `App`. A change to a field name or type in `AppConfig` requires a matching manual update in `AppOptions`, with no compile-time enforcement.

Additionally, `std::cout` is used directly in some test files instead of the project's categorized logging system, making test logs inconsistent with app logs.

**Source:** `app/app_config.h`, `app/app.h`/`app.cpp` (where options and config are merged).
**Raised by:** GPT.

## Goal

Collapse the duplicated fields between `AppOptions` and `AppConfig`. The cleanest approach is to
have `AppOptions` contain only the CLI-specific overrides (paths, flags, modes) and have the merge
into `AppConfig` happen at one well-defined point. Fields that appear in both should live in one place.

As a secondary goal: replace `std::cout` in test files with `draxul_log` or the project logger.

## Implementation Plan

- [x] Read `app/app_config.h`, `app/app.h`, and any `AppOptions` definition to enumerate duplicated fields.
- [x] Design the merge strategy:
  - `AppConfig` is the canonical runtime config (loaded from file).
  - `AppOptions` holds CLI overrides that shadow `AppConfig` fields.
  - A single `apply_overrides(AppConfig&, const AppOptions&)` function merges them — only fields
    that were explicitly set on the CLI override the config-file value.
- [x] Implement the merge function and remove duplicated storage.
- [x] Search for `std::cout` in `tests/` and replace with the project logging macro.
- [x] Run `clang-format` on touched files.
- [x] Run `ctest --test-dir build`.

## Completion Notes

- `AppOptions` now carries CLI override fields as `std::optional` values instead of storing a second embedded `AppConfig`.
- Added `apply_overrides(AppConfig&, const AppOptions&)` as the single merge point for config-file values plus explicit CLI overrides.
- `App::initialize()` now loads config or defaults first, then applies overrides in one place.
- Added tests covering selective overrides as well as intentional clearing of string and list values.
- Replaced direct `std::cout` / `std::cerr` usage in the test harness with categorized logging macros.

## Sub-Agent Split

Single agent. Changes are in `app/app_config.h/cpp`, `app/app.h/cpp`, and `tests/*.cpp`.
