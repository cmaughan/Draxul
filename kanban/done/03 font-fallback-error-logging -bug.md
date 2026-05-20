# 03 Font Fallback Error Logging (bug)

## Problem

When a configured font path doesn't exist, the error log is generic ("Failed to load
configured font") with no path or FreeType error code, making it hard to diagnose
configuration problems.

## Changes

- [x] `libs/draxul-font/src/font_manager.cpp`: Capture `FT_Error` from `FT_New_Face`;
  log the attempted path plus the numeric error code and `FT_Error_String()` description.
- [x] `libs/draxul-font/src/font_resolver.h`: Log a `warn` when the user-configured primary
  font path does not exist on disk; log `debug` for each fallback candidate skipped because
  it is not found or is the same as the primary; log `warn` when a fallback that exists on
  disk still fails to load.
- [x] `app/app.cpp`: Expand `last_init_error_` in `initialize_text_service()` to include the
  attempted font path and a hint to check `config.toml`.

## Outcome

Font loading failures now surface the exact path attempted and the FreeType error code,
making misconfiguration straightforward to diagnose from the log.
