# WI 112 — tsan-build-preset

**Type:** feature
**Priority:** 9 (CI quality — ThreadSanitizer will catch data races before users do)
**Source:** review-consensus.md §6 [Claude]
**Produced by:** claude-sonnet-4-6

---

## Problem

The sanitizer build presets currently cover ASan + UBSan (`mac-asan`) but not ThreadSanitizer (TSan). The codebase has multiple threading concerns: RPC reader thread, `UiRequestWorker`, `CodebaseScanner`, and the recently-surfaced callback init race (WI 100). Without TSan in CI, data races can persist undetected until they manifest as random crashes on users' machines.

---

## Investigation

- [x] Read `CMakePresets.json` — find the `mac-asan` preset; understand how `draxul_apply_sanitizers()` is invoked.
- [x] Read `cmake/Sanitizers.cmake` (or equivalent) — understand how `-fsanitize=address,undefined` is applied; confirm adding `thread` is straightforward. (Sanitizer flags live in the top-level `CMakeLists.txt` in `draxul_apply_sanitizers()`, not a dedicated module.)
- [x] Note: TSan is **incompatible with ASan**; the new preset must be separate (`mac-tsan`), not combined.
- [ ] Check if the Metal backend or SDL has known TSan false positives that would need suppression files. (Deferred until an actual TSan run surfaces noise.)

---

## Fix Strategy

- [x] Add a `mac-tsan` preset to `CMakePresets.json` (modelled after `mac-asan` but with `-fsanitize=thread` instead of `-fsanitize=address,undefined`).
- [x] Update `cmake/Sanitizers.cmake` (or wherever sanitizer flags are set) to support a `TSAN` flag/option. (Added `DRAXUL_ENABLE_TSAN` option + TSan branch in `draxul_apply_sanitizers()` in top-level `CMakeLists.txt`. Mutually-exclusive check with `DRAXUL_ENABLE_SANITIZERS` and an MSVC guard are included.)
- [ ] Create a TSan suppression file (`tsan.supp`) in the repo root if any known false positives exist (SDL event queue races, etc.). (Deferred — add only when a real TSan run identifies them.)
- [ ] Add a `mac-tsan` build + test run to the CI workflow (`.github/workflows/`) or document it in `CLAUDE.md` as a manual step if CI resources are limited. (Documented as a manual step in `CLAUDE.md` Build Commands; CI wiring left as a follow-up.)
- [ ] Build with the new preset: `cmake --preset mac-tsan && cmake --build build --target draxul-tests` (configure verified; full build + test run deferred to a follow-up WI so any surfaced races can be triaged without blocking the preset landing.)
- [ ] Run: `ctest --test-dir build -R draxul-tests`
- [ ] Fix any TSan findings that surface, or add targeted suppressions with comments explaining why each is a false positive.

---

## Acceptance Criteria

- [x] `cmake --preset mac-tsan` configures successfully.
- [x] The preset is documented in `CLAUDE.md` under Build Commands.

The "build + run under TSan + fix/suppress findings + wire into CI" work
is tracked as a follow-up so the preset itself can land independently of
race triage. See WI 134 (tsan-validation-and-ci-wiring).

---

## Interdependencies

- **WI 100** (rpc-callbacks-init-order-race) — fix known races before running TSan CI gates to avoid TSan noise blocking unrelated work.
- **WI 101** (mpackvalue-reader-thread-uncaught) — same rationale.
- **WI 89** (rpc-notification-callback-under-lock, existing) — TSan may surface the deadlock risk; fix WI 89 first.
