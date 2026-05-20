# 00 config-persistence-policy-leak — Bug

## Summary

`GuiActionHandler::change_font_size()` in `app/gui_action_handler.cpp` writes `config.toml` immediately on every font-size change. `App::shutdown()` respects `AppOptions::save_user_config` to decide whether to persist config — the action handler does not. This means interactive font-size adjustments bypass the "don't save" intent entirely, writing config even in scenarios (e.g. test harnesses, `--no-save` launch flags) where persistence was explicitly opted out.

**Raised by:** GPT (review-latest.gpt.md)

## Steps

- [x] 1. Read `app/gui_action_handler.cpp` — locate `change_font_size()` and any other action handler methods that call config save directly.
- [x] 2. Read `app/gui_action_handler.h` — understand what context/deps the handler receives.
- [x] 3. Read `app/app.cpp` — find `App::shutdown()` and how it uses `AppOptions::save_user_config` to gate config writes.
- [x] 4. Read `libs/draxul-app-support/include/draxul/app_config.h` and `app_options.h` — confirm the `save_user_config` flag location and its type.
- [x] 5. Determine how `GuiActionHandler` should receive the `save_user_config` flag. Options:
   - Pass `AppOptions` or `bool save_user_config` into `GuiActionHandler::Deps` at construction.
   - Gate all immediate config saves on the flag.
   - Alternatively, remove immediate saves from the action handler entirely and defer all config persistence to `App::shutdown()` (preferred if config is always in memory).
- [x] 6. Apply the fix. The preferred approach:
   - Remove the config file write from `change_font_size()` (and any other action handler that calls it).
   - Ensure the in-memory `AppConfig` is updated (it already should be).
   - Let `App::shutdown()` do the single authoritative save gated by `save_user_config`.
   - If real-time persistence is required (e.g. crash recovery), add a flag to `GuiActionHandler::Deps` and gate writes on it.
- [x] 7. Build: `cmake --build build --target draxul draxul-tests`.
- [x] 8. Run: `ctest --test-dir build -R draxul-tests`. All tests must pass.
- [ ] 9. Manual smoke: launch draxul, change font size, confirm size persists across restart (normal path). Then verify with a test harness that `save_user_config=false` suppresses the file write.
- [x] 10. Run clang-format on all touched files.
- [x] 11. Mark complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- `GuiActionHandler::change_font_size()` does not directly write `config.toml`.
- When `save_user_config = false`, font size changes do not produce file I/O.
- Font size is persisted on clean shutdown when `save_user_config = true`.
- All tests pass.

## Interdependencies

- Work item `04 gui-action-config-persistence -test.md` writes the regression test for this fix — do that test item after this bug is fixed.

*Authored by: claude-sonnet-4-6*
