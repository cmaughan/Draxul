# `do.py clean` Design

**Date:** 2026-07-13

## Goal

Add a cross-platform `do.py clean` command that completely removes Draxul's repository-local CMake build directories without touching any other generated or source artifacts.

## Command Contract

- `do.py clean` removes repository-root directories named exactly `build/` or beginning with `build-` recursively. This covers Visual Studio, per-configuration Ninja, tooling, legacy, and custom CMake trees.
- The paths are derived from `repo_root()` rather than the caller's working directory.
- The command does not remove `deploy/`, render outputs, render references, databases, plans, or source files.
- Similarly named regular files and names such as `builder/` are preserved.
- If no matching build directory exists, the command succeeds and reports that the build directories are already absent.
- Successful deletion returns status 0. A filesystem deletion error is reported by Python and returns a non-zero status.

## Implementation

Use Python's `shutil.rmtree` rather than platform-specific shell commands or CMake's `clean` target. CMake's target intentionally preserves the cache, downloaded dependencies, and generated build system, which does not satisfy the request to clean out the build.

Extract the read-only-file retry callback currently nested in deployment staging into a shared directory-removal helper. Both deployment replacement and `do.py clean` use that helper, preserving the existing Windows behavior while avoiding duplicate deletion logic.

Register `clean` alongside the other single-word commands in `main()` and describe it in `help_text()`.

## Safety and Error Handling

The deletion helper receives the exact path to remove, but the command itself always supplies `build_dir(repo_root())`. It does not accept a caller-provided path. This keeps the destructive action bounded to the repository's conventional build directory.

The helper retries read-only entries by making the failing path writable before retrying the failed removal operation. Other errors remain visible and fail the command rather than being silently ignored.

## Tests and Documentation

Add Python unit coverage for:

- help text advertising `clean`;
- recursive removal of a populated build directory;
- successful, idempotent behavior when the build directory is absent;
- preservation of neighboring directories and files.

Update the README convenience-command section and `docs/features.md` to document the bounded `build/` and `build-*` removal rule.

## Success Criteria

- `python do.py clean` and `py do.py clean` behave identically on supported platforms.
- Populated repository-local `build/` and `build-*` directories are completely removed.
- Repeating the command succeeds without error.
- Neighboring generated artifacts remain untouched.
- The `do.py` unit suite passes on macOS and Windows-compatible code paths.
