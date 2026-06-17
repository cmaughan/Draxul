# WI 40 -- Scrollback Config and Test Completion

**Type:** feature/test
**Priority:** medium
**Source:** cleanup of completed `scrollback-buffer-completion` work item
**Created:** 2026-04-09
**Cleaned up:** 2026-06-15

---

## Current State

The original scrollback completion work is mostly implemented:

- IND, CSI SU, and DL capture rows into scrollback.
- Resize shrink/grow reflow preserves visible content via `ScrollbackBuffer::pop_newest_rows`.
- Shift+PageUp/PageDown/Home/End keyboard scrollback navigation is wired.
- `erase_display(2)` preserves non-blank visible rows in scrollback.
- Scrollback capacity is a constructor parameter with a higher default.

The remaining work is narrower: expose capacity through config/launch options and add targeted regression tests for the capture/reflow/clear behavior that was implemented.

---

## Remaining Work

### Configurable Capacity

- [x] Add `scrollback_lines` to config parsing and serialization.
- [x] Thread the parsed value into shell `HostLaunchOptions`.
- [x] Pass the configured capacity into `LocalTerminalHost` / `ScrollbackBuffer`.
- [x] Clamp invalid values and log a warning, matching the existing config style.
- [x] Document `scrollback_lines` in `docs/features.md`.

### Targeted Tests

- [x] IND (`ESC D`) captures the row that scrolls off the top.
- [x] CSI SU (`CSI n S`) captures all rows scrolled off the top.
- [x] DL at row 0 captures deleted full-screen rows.
- [x] Shrink/grow round trip preserves visible content and cursor position.
- [x] `erase_display(2)` / `clear` preserves previous non-blank output in scrollback.

---

## Acceptance Criteria

- [x] `scrollback_lines` controls shell-host scrollback capacity from `config.toml`.
- [x] Invalid `scrollback_lines` values fall back safely with a warning.
- [x] Targeted scrollback regression tests cover the implemented capture and reflow paths.
- [x] `cmake --build build --target draxul draxul-tests` passes.
- [x] `python do.py smoke` passes.

---

## Relevant Files

| File | Role |
|---|---|
| `libs/draxul-config/src/app_config_io.cpp` | Parse/serialize config |
| `libs/draxul-config/include/draxul/app_config*.h` | Config storage |
| `libs/draxul-runtime-support/include/draxul/app_options.h` | Launch/runtime option plumbing |
| `libs/draxul-host/include/draxul/scrollback_buffer.h` | Ring buffer capacity |
| `libs/draxul-host/src/local_terminal_host.cpp` | Shell scrollback owner |
| `tests/scrollback_overflow_tests.cpp` | Existing scrollback tests |
| `tests/terminal_vt_tests.cpp` | Terminal escape behavior tests |

---

## Notes

The old card mixed completed implementation notes with a few unchecked residuals. This card intentionally tracks only what remains.
