# 00 duplicated-private-public-headers — Bug

## Summary

Two library pairs in the Draxul codebase maintain both a public header (under `include/`) and a private header (under `src/`) that have diverged from each other. Consumers who `#include` the public header compile against a different API than the implementation uses internally, causing subtle API gaps and potential linker surprises.

**Divergence 1 — VtParser callbacks:**
- `libs/draxul-host/include/draxul/vt_parser.h` — public `VtParser::Callbacks` struct is MISSING the `on_esc` callback.
- `libs/draxul-host/src/vt_parser.h` — private version HAS `on_esc` added.
- External consumers of `VtParser` (e.g., tests, other libraries) cannot set `on_esc` even though the implementation supports it, because the public struct doesn't declare the field.

**Divergence 2 — AppConfig default keybinding:**
- `libs/draxul-app-support/include/draxul/app_config.h` — default keybinding uses the action name `toggle_diagnostics`.
- `libs/draxul-app-support/src/app_config.h` — default keybinding uses `toggle_debug_panel`.
- Both copies define default values that affect runtime behavior. Users and tests may observe different defaults depending on which header is in scope.

## Steps

- [x] 1. Read `libs/draxul-host/include/draxul/vt_parser.h` in full to see the public `Callbacks` struct definition.
- [x] 2. Read `libs/draxul-host/src/vt_parser.h` in full to see the private `Callbacks` struct definition and the `on_esc` field.
- [x] 3. Determine whether the private `src/vt_parser.h` is included only by `src/vt_parser.cpp` (or nearby translation units) and serves a genuine purpose, or whether it is a duplicate that should simply be removed.
- [x] 4. If the private header exists only to add `on_esc`: merge `on_esc` into the public `include/draxul/vt_parser.h` `Callbacks` struct and delete (or stop including) the private copy. The implementation file should include the public header directly.
  - Added `on_esc` to the public header. Private header now matches public — TODO: remove private copy and update includes to use `<draxul/vt_parser.h>` directly.
- [x] 5. If the private header serves a genuine purpose (e.g., contains implementation-internal types beyond just `on_esc`): at minimum add `on_esc` to the public `Callbacks` struct so external consumers can use it, and add a comment in the private header warning that it must stay in sync with the public one.
- [x] 6. Read `libs/draxul-app-support/include/draxul/app_config.h` in full to find the `toggle_diagnostics` default.
- [x] 7. Read `libs/draxul-app-support/src/app_config.h` in full to find the `toggle_debug_panel` default.
- [x] 8. Determine the correct action name. Check `app/gui_action_handler.cpp` (or equivalent) to see which name is actually dispatched at runtime.
  - Runtime dispatches `toggle_diagnostics`; `src/app_config.h` had the wrong default `toggle_debug_panel`. Fixed to `toggle_diagnostics`.
- [x] 9. Align both header files to use the correct, consistent action name. Remove whichever copy of the default is redundant, or at minimum ensure both use the same string literal.
- [x] 10. Search the entire codebase for any other `include/` vs `src/` header pairs that follow this same pattern and note them in a code comment or in this work item for future cleanup.
  - Found many pairs in `draxul-app-support`, `draxul-nvim`, `draxul-renderer`, and `draxul-host`. These are a known pattern in the codebase. The private `vt_parser.h` was the only one diverged; it has now been deleted and all includes updated to use `<draxul/vt_parser.h>` directly.
- [x] 11. Build on macOS: `cmake --build build --target draxul draxul-tests`. Confirm no compile errors.
- [x] 12. Run the full test suite: `ctest --test-dir build -R draxul-tests`. Confirm all tests pass.
- [x] 13. Run clang-format on all touched files: `clang-format -i <files>`.
- [x] 14. Mark this work item complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- `libs/draxul-host/include/draxul/vt_parser.h` public `Callbacks` struct contains `on_esc` (or the divergence is fully resolved by removing the private copy).
- Both `app_config.h` copies agree on the default keybinding action name.
- All existing tests pass.
- No new `include/` vs `src/` dual-header divergence is introduced.

*Authored by: claude-sonnet-4-6*
