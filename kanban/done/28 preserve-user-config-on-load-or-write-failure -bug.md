# Preserve user configuration on load or write failure

**Type:** bug
**Raised by:** user, 2026-09-24

## Problem

Startup and shutdown treat an unreadable or malformed `config.toml` as defaults. Shutdown can then overwrite the original file. Config saves also truncate the destination before writing, so interruption can leave it incomplete.

## Work

- [x] Refuse shutdown persistence when the current on-disk config cannot be loaded and parsed.
- [x] Publish config saves through a same-directory temporary file and atomic replacement.
- [x] Add regression coverage for malformed input and replacement failure, while preserving valid external edits.
- [x] Show a startup error toast when user configuration cannot be read or parsed.
- [x] Run the core aggregate test gate and same-cache smoke.
- [x] Confirm the Windows build and tests natively (hosted CI is blocked during private-submodule checkout).

## Notes

- Preserve normal first-run behavior when the file does not yet exist.
- Keep the app usable with defaults if startup config is malformed, but never overwrite the malformed file on exit.
- A config that existed at startup but vanishes before shutdown is left absent; first-run saves still create a missing file.
- Atomic saves preserve symlink targets and POSIX file permissions. Both `AppConfig` and `ConfigDocument` use the same writer.
- Reload failures already showed an error toast. Startup failures now also show one; shutdown can only log because the UI is exiting. A malformed-startup smoke test verifies defaults remain usable and the file is preserved.
- After the startup-toast change, macOS core aggregate passed 25/25 entries (41.57s), Debug same-cache smoke passed (6.7s), and Release build/startup smoke passed. Windows CI remains blocked before build by private PCBView checkout as recorded below.
- macOS validation: core aggregate, 25/25 CTest entries passed (42.49s); Debug same-cache smoke passed (6.7s); Release build and startup smoke passed.
- Windows native validation (2026-09-24): Debug core aggregate passed 25/25 CTest entries after correcting the CRLF-sensitive preservation assertion; focused config app smoke passed 9 cases/79 assertions; a same-cache single-pane Debug startup smoke and rebuilt Release startup smoke both passed.
- CI run: https://github.com/cmaughan/Draxul/actions/runs/35987183475. Both hosted jobs stopped during `actions/checkout` because the workflow token cannot read the private `cmaughan/draxul-pcbview` submodule; no compilation or tests ran. There is no submodule access secret configured for this repository. Native Windows and macOS runs now cover both platforms; hosted CI access remains a separate infrastructure issue.
