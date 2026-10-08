# Diagnose native plugin staging failure under long cache paths

**Summary:** MegaCity can become unavailable when its isolated Windows config/cache
root has a long path, and the placeholder reports only an unknown staging error.

**Priority:** P1
**Source:** Native validation of `kanban/pending/85 plugin-texture-release-ownership -bug.md`.

**Evidence:** On 2026-10-02, a Debug MegaCity diagnostic render export under the
`draxul-critical-fixes-<32-character-id>/texture-owners/` temporary profile produced
an unavailable pane saying `Unable to stage plugin generation: Unknown error`.
The same fixture/package and source rendered correctly after moving only the
isolated APPDATA/LOCALAPPDATA profile to a shorter `d85-<6-character-id>/` root.
The failing export still exited with code 0. Path length is a hypothesis to verify;
no underlying Win32 error was exposed by the placeholder.

- [ ] Reproduce with controlled profile/cache path lengths and capture the failing
  file operation and its real native error, including nested package asset paths.
  - Not reproduced (macOS-only agent run, 2026-10-08). Code-reading diagnosis:
    `PluginManager::stage_generation()` shadow-copies the whole published generation
    to `%LOCALAPPDATA%\draxul\plugin-runtime\process-<pid>-<ticks>-<n>\<id>\<ticks>-<n>\`
    with one `std::filesystem::copy(recursive)`. MSVC steady-clock ticks are
    nanoseconds (14-16 digits), so that layout added about 85 characters plus the
    plugin id before every package-relative path. Draxul has no `longPathAware`
    manifest and never uses `\\?\` paths, so Win32 copy/create calls fail once the
    full destination reaches MAX_PATH (260). With the reported profile root
    (`...\Temp\draxul-critical-fixes-<32>\texture-owners\<LocalAppData>`, roughly
    110-120 characters) only about 55 characters remained for MegaCity's nested
    asset paths; the shorter `d85-<6>` root restores about 60 characters, matching
    the observed success (hypothesis consistent with the evidence, not yet proven
    on Windows). The recursive copy reported only `error_code::message()` with no
    operation, path, or numeric code, so the native error was lost behind the
    "Unknown error" text.
- [ ] Fix current-format plugin staging for supported Windows profile paths and
  report the concrete failing path/operation if staging cannot complete.
  - Done: staging now copies entry by entry and reports the operation (`create
    directory`, `copy file`, `enumerate package`, `inspect`), the full path and its
    length, the error category/value, and on Windows a MAX_PATH note when the path
    is 260+ characters. The runtime layout is now `p<pid>-<ticks>-<n>\<id>\g<n>\`,
    removing about 30 characters of fixed overhead.
  - Not done: profile roots long enough to exceed MAX_PATH even with the shorter
    prefix still fail. A complete fix needs Windows long-path support end to end
    (`longPathAware` manifest plus the LongPathsEnabled policy, or `\\?\` paths for
    staging and for the plugin's own asset I/O); staging alone with `\\?\` would
    only move the failure into the plugin's asset loads. Needs a Windows decision and
    run.
- [ ] Cover both staging success and a clear failure diagnostic with a realistic
  native plugin package; ensure render validation detects an unavailable product.
  - Done: `plugin staging keeps nested package paths compact and reports failures` in
    `tests/plugin_manager_tests.cpp` stages the real fixture module with a private
    dependency and nested asset, bounds the staging prefix, and checks the failure
    text through `PluginHost::init_error()` (blocked directory) and, on POSIX, an
    unreadable nested asset.
  - Not done: render validation still exits 0 when a product pane is unavailable.
- [ ] Run scope-appropriate aggregate, same-cache smoke, and native plugin render.
  - macOS Debug `do.py test debug` (56 CTest entries) passed apart from three load-sensitive flakes that pass in isolation or also fail on a pre-change binary, plus the known `draxul-do-py-tests` megacity AGENTS.md failure in submodule-less worktrees; `do.py smoke debug --skip-build` passed (2026-10-08). The Windows native plugin
    render under a long profile root is outstanding.

Preserve the user's installed plugin/profile state. Use unique child-only profiles
for reproduction. Existing artifacts are under the directory recorded by
`build-ninja-debug/critical-validation-directory.txt`, in `texture-owners/`:
`megacity-diagnostics.bmp` (failure), `megacity-diagnostics-short.bmp` (success),
corresponding logs, and `megacity-short-results.json`.

Renumbered from95 while integrating upstream because the incoming app-tab reuse
card already owns95 in this pending lane. The investigation scope is unchanged.
