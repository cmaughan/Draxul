# Resolve the macOS bundle executable in developer helpers

**Severity:** MEDIUM
**Type:** bug

## Bug

`scripts/update_screenshot.py` and `scripts/store_logs.sh` launch nonexistent
`build/draxul` on macOS. A standard build produces
`build/draxul.app/Contents/MacOS/draxul`.

## Work

- [ ] Share or consistently implement platform executable resolution for both helpers.
- [ ] Preserve Windows and explicitly supported non-macOS Unix behavior.
- [ ] Add clear missing-build diagnostics and safe handling for paths containing spaces.
- [ ] Add platform path-resolution tests for the Python helper and a shell-level check
      for the logging helper.

## Acceptance criteria

- [ ] Both helpers launch a standard macOS app build.
- [ ] Screenshot conversion and caller-supplied log output remain unchanged.
- [ ] Missing builds fail with an actionable resolved path.
