# Preserve user configuration on load or write failure

**Type:** bug
**Raised by:** user, 2026-09-24

## Problem

Startup and shutdown treat an unreadable or malformed `config.toml` as defaults. Shutdown can then overwrite the original file. Config saves also truncate the destination before writing, so interruption can leave it incomplete.

## Work

- [x] Refuse shutdown persistence when the current on-disk config cannot be loaded and parsed.
- [x] Publish config saves through a same-directory temporary file and atomic replacement.
- [x] Add regression coverage for malformed input and replacement failure, while preserving valid external edits.
- [x] Run the core aggregate test gate and same-cache smoke.
- [ ] Confirm the Windows build and tests in CI.

## Notes

- Preserve normal first-run behavior when the file does not yet exist.
- Keep the app usable with defaults if startup config is malformed, but never overwrite the malformed file on exit.
- A config that existed at startup but vanishes before shutdown is left absent; first-run saves still create a missing file.
- Atomic saves preserve symlink targets and POSIX file permissions. Both `AppConfig` and `ConfigDocument` use the same writer.
- macOS validation: core aggregate, 25/25 CTest entries passed (42.49s); Debug same-cache smoke passed (6.7s); Release build and startup smoke passed.
- Windows validation is pending CI because this host is macOS.
