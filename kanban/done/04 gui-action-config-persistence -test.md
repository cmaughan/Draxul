# 04 gui-action-config-persistence — Test

## Summary

`GuiActionHandler::change_font_size()` should respect `AppOptions::save_user_config = false` and not write `config.toml` when this flag is set. After bug fix `00 config-persistence-policy-leak` lands, there is no automated test that verifies this contract. This test item locks in the correct behavior so future changes to the action handler cannot silently reintroduce the persistence leak.

**Raised by:** GPT (review-latest.gpt.md)

## Steps

- [x] 0. **PREREQUISITE**: `00 config-persistence-policy-leak -bug.md` must be complete. The test will fail against unfixed code, which is informative but not the goal.
- [x] 1. Read `app/gui_action_handler.h` and `app/gui_action_handler.cpp` — understand the `Deps` structure, how config is accessed, and how `change_font_size()` is called.
- [x] 2. Read `libs/draxul-app-support/include/draxul/app_config.h` — find `AppConfig` and `AppOptions` fields relevant to persistence.
- [x] 3. Check `tests/` for existing `gui_action_handler_tests.cpp` or equivalent — understand what's already covered.
- [x] 4. Write tests in `tests/gui_action_handler_tests.cpp` (or create it if it doesn't exist):
   - **Test A** (`on_config_changed` installed): `font_increase` → in-memory `font_size` updated AND `on_config_changed` called once.
   - **Test B** (`on_config_changed` null): `font_increase` → in-memory `font_size` updated AND no save occurs.
   - **Test C**: Rapid `font_increase` calls → `font_size` accumulates correctly and hook fires per change.
   - **Test D** (boundary): `font_decrease` at `MIN_POINT_SIZE` → size stays at minimum, hook not called.
   - **Test E**: `font_reset` from non-default size → resets to `DEFAULT_POINT_SIZE`, hook fires once.
- [x] 5. Use a fake/mock for the config save mechanism (lambda spy in `Deps.on_config_changed`) to avoid real filesystem I/O in tests.
- [x] 6. Build: `cmake --build build --target draxul-tests`.
- [x] 7. Run: `ctest --test-dir build -R draxul-tests`. All tests pass.
- [x] 8. Run clang-format on all touched files.
- [x] 9. Mark complete and move to `plans/work-items-complete/`.

## Implementation Notes

- Added `std::function<void()> on_config_changed` field to `GuiActionHandler::Deps` (alongside the existing `on_font_changed` callback).
- `change_font_size()` now calls `on_config_changed()` after updating `deps_.config->font_size` (if the callback is set).
- Tests compile `app/gui_action_handler.cpp` directly into the test binary (added to `tests/CMakeLists.txt` sources) and add `${CMAKE_SOURCE_DIR}/app` to the test include paths.
- 5 tests written covering: save-when-hooked, no-save-when-not-hooked, accumulation, min-boundary, reset.

## Acceptance Criteria

- At least 3 tests cover `GuiActionHandler` config persistence behavior.
- `save_user_config = false` prevents any config file write when font size changes.
- `save_user_config = true` results in a config write.
- In-memory config is always updated regardless of the persistence flag.
- All tests pass.

## Interdependencies

- **PREREQUISITE**: `00 config-persistence-policy-leak -bug.md` must be resolved first.
- Independent of all other test items.

*Authored by: claude-sonnet-4-6*
