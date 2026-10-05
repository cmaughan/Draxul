# Run the existing Release build with dr rel and drr rel

**Priority:** P2
**Summary:** Provide a fast CLI entry point that forwards arguments without rebuilding Draxul.

- [x] Add `rel` to the repository and parent workspace helpers.
- [x] Preserve arguments, working directory, output and exit code; require an existing Release build.
- [x] Document usage and validate forwarding, missing/Debug builds, and the live Release executable.

The shell functions already delegate to these helpers; no shell edits are needed. The parent helper is `/Users/cmaughan/dev/do.py`, in its own Git repository. On macOS use the Release configuration in `build`; on Windows prefer `build-ninja-release`, then the existing Release configuration in `build`. No configure/build or generator switch occurs. Keep JSON output free of wrapper banners.

## Completed 2026-10-05

- Added `rel` to Draxul's `do.py` and `/Users/cmaughan/dev/do.py` in the parent tooling repository. Existing `dr`/`drr` shell functions pick it up immediately; no shell restart is needed.
- Arguments pass through unchanged, with an optional initial `--`. The workspace wrapper preserves the original caller directory. Both wrappers preserve stdout/stderr and process exit status without build banners or Windows GUI detachment.
- Reads existing CMake metadata to select Release and rejects missing/Debug builds without configuring or compiling. Windows searches Ninja Release before the existing `build` Release configuration.
- Full Python helper suite: 86 tests in 1.494 seconds, passed with one existing skip. Added actual subprocess argument/cwd/output/exit-status coverage and Windows Release selection plus missing/Debug build checks. No C++ changes; no C++ rebuild or broad CTest run needed for this helper-only change.
- Syntax checks passed for both helpers. Exercised the actual `dr`/`drr` definitions from `.zshrc`: `dr rel --help`, `drr rel --help` from `/tmp`, and `drr rel -- --help` all matched direct executable output (0.05–0.07 seconds each).
- Same-cache Release smoke passed with `python3 do.py smoke release --skip-build`. An initial smoke invocation without `release` correctly rejected the Release cache as mismatching the default Debug request, before launching anything; reran with the correct explicit configuration.
- Updated repository feature documentation and parent workspace README. Changes span two repositories.
