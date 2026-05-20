# 02 Render Test Extraction And Parser Unification

## Why This Exists

The current render-test path works, but the ownership is wrong.

Current verified issues:

- `app/render_test.cpp` is compiled into `draxul.exe`
- the tests compile that same file directly
- `app_config.cpp` and `render_test.cpp` each carry their own partial TOML-like parser helpers
- render-test reporting still uses hand-built JSON text

## Goal

Keep render-test tooling available without making it a production-orchestration concern, and remove duplicated parsing/reporting logic.

## Implementation Plan

1. [x] Extract shared parsing helpers.
   - created `app/toml_util.h` with inline `trim`, `unquote`, `is_complete_array_literal`,
     `parse_string_array` (escape-aware), `parse_int`, `parse_double`, `parse_bool`,
     and `json_escape_string` — all in `namespace draxul::toml`
   - removed duplicated copies from `app_config.cpp` and `render_test.cpp`
2. [x] Move render-test logic behind a narrower library seam.
   - `render_test.h` no longer includes `app.h`; replaced with targeted includes:
     `app_config.h` (which also now owns `AppOptions`) + `<draxul/renderer.h>`
   - `AppOptions` moved from `app.h` to `app_config.h` (natural pairing with `AppConfig`)
3. [x] Keep the CLI mode thin.
   - `main.cpp` already only dispatches to the render-test service
   - `App` does not own report-writing policy
4. [x] Make report output structurally safe.
   - `write_report` and `write_failure_report` now call `json_escape_string` for `scenario.name`
     and `error_message` before embedding them in JSON output

## Tests

- [x] Existing multiline scenario test kept green
- [x] Added `run_test`-based tests for:
  - multiline commands, missing commands field, inline comments
  - `trim`, `unquote`, `parse_string_array` (basic + backslash escapes + non-array)
  - `is_complete_array_literal`, `parse_bool`, `parse_int`, `parse_double`
  - `json_escape_string` (plain, quotes, backslash, newline, tab)
- [x] All 5 ctest targets pass

## Suggested Slice Order

1. parser utility extraction
2. report/output cleanup
3. render-test service extraction
4. CLI/app wiring cleanup
