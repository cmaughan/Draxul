# Resolve the macOS bundle executable in the screenshot updater

**Severity:** MEDIUM  
**Type:** Bug

## Bug description

`scripts/update_screenshot.py` looks for `build/draxul` on macOS although the standard build produces `build/draxul.app/Contents/MacOS/draxul`.

**Trigger:** Run the screenshot updater after a standard macOS CMake build.

## Investigation

- [ ] Confirm executable paths for Windows, macOS, and other supported script platforms.
- [ ] Add path-resolution tests with each supported `sys.platform` value.
- [ ] Check whether multi-config build directories require additional handling.

## Fix strategy

- [ ] Return the application-bundle executable for Darwin.
- [ ] Preserve existing Windows behavior and explicitly define any supported non-macOS Unix path.
- [ ] Keep missing-build diagnostics pointed at the resolved path.

## Acceptance criteria

- [ ] The updater finds and launches a standard macOS build.
- [ ] Platform path-resolution tests cover Darwin and Windows.
- [ ] Existing screenshot conversion behavior remains unchanged.
