# Diagnose native plugin staging failure under long cache paths

**Summary:** MegaCity can become unavailable when its isolated Windows config/cache
root has a long path, and the placeholder reports only an unknown staging error.

**Priority:** 96
**Severity:** HIGH
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
- [ ] Fix current-format plugin staging for supported Windows profile paths and
  report the concrete failing path/operation if staging cannot complete.
- [ ] Cover both staging success and a clear failure diagnostic with a realistic
  native plugin package; ensure render validation detects an unavailable product.
- [ ] Run scope-appropriate aggregate, same-cache smoke, and native plugin render.

Preserve the user's installed plugin/profile state. Use unique child-only profiles
for reproduction. Existing artifacts are under the directory recorded by
`build-ninja-debug/critical-validation-directory.txt`, in `texture-owners/`:
`megacity-diagnostics.bmp` (failure), `megacity-diagnostics-short.bmp` (success),
corresponding logs, and `megacity-short-results.json`.

Renumbered from95 while integrating upstream because the incoming app-tab reuse
card already owns95 in this pending lane. The investigation scope is unchanged.
