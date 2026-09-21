# Resolve the macOS bundle executable in developer helpers

**Severity:** MEDIUM
**Type:** bug

## Bug

`scripts/update_screenshot.py` and `scripts/store_logs.sh` launch nonexistent
`build/draxul` on macOS. A standard build produces
`build/draxul.app/Contents/MacOS/draxul`.

## Work

- [x] Share or consistently implement platform executable resolution for both helpers.
- [x] Preserve Windows and explicitly supported non-macOS Unix behavior.
- [x] Add clear missing-build diagnostics and safe handling for paths containing spaces.
- [x] Add platform path-resolution tests for the Python helper and a shell-level check
      for the logging helper.

## Acceptance criteria

- [x] Both helpers launch a standard macOS app build.
- [x] Screenshot conversion and caller-supplied log output remain unchanged.
- [x] Missing builds fail with an actionable resolved path.
