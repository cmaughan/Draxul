# Localize and audit internal-target build policy

**Type:** refactor
**Priority:** P1 / sequence 00
**Raised by:** GPT/Codex
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 1

## Goal

Replace the manually maintained root target lists for sanitizers, coverage, and
MSVC `/FS` with one target-local helper plus a configure-time completeness audit.

## Boundary verification

- [x] Inventory every project-owned compiled, OBJECT, executable, INTERFACE,
  imported, alias, and third-party target after all optional subdirectories load.
- [x] Re-inventory current core/plugin-support targets, including NanoVG, renderer OBJECT
      targets, Vulkan resources, and test-support leaves; external product-owned targets
      must be explicitly outside core policy.
- [x] Record the existing ASan/TSan/coverage and `/FS` behavior before changing it: the root manual library list applied sanitizer and coverage compile/link flags plus MSVC `/FS` without target-type filtering; executables/tests were maintained separately. The localized helper preserves compile policy while limiting link flags to targets that actually link.
- [x] Define explicit skip rules for imported, alias, INTERFACE, and third-party targets.

## Implementation and migration

- [x] Add `draxul_configure_internal_target(target)` in project-owned CMake infrastructure.
- [x] Apply compile options to STATIC/OBJECT/executable targets and link options only
  to target types that link.
- [x] Mark every considered target with a property suitable for a final audit.
- [x] Prove the helper on one STATIC, OBJECT, executable, and INTERFACE target.
- [x] Adopt the helper beside each internal target definition, directory by directory.
- [x] Add the end-of-configure audit and remove root enumerations only after it passes.
- [x] Keep every intermediate commit configurable and buildable. The migration landed as one configurable checkpoint rather than a sequence of partially adopted commits.

## Unit and configure tests

- [x] Add a configure-time negative fixture for an unconfigured internal compiled target.
- [x] Add positive coverage for STATIC, OBJECT, executable, INTERFACE, and explicit skip cases.
- [x] Run `python -m unittest tests.do_py_tests` if Python drives any audit fixture. (74 tests passed on macOS; one platform-only case skipped.)
- [x] Build `draxul-renderer-core` and `draxul-tests` under the normal Debug preset.

## Cross-platform validation

- [ ] Windows/MSVC: build compiled targets in parallel and verify `/FS` is applied.
- [ ] Windows: verify supported sanitizer configuration still configures and links.
- [x] macOS ASan gate retired by project decision; no ASan run is required for this card.
- [ ] macOS: configure/build the remaining `mac-tsan` and coverage presets.
- [ ] Verify both Metal and Vulkan OBJECT targets are considered without applying link
  options to OBJECT libraries.
- [ ] Configure with each mounted product disabled or absent and prove external product
      targets are not accidentally treated as core internals.

## Agent documentation and tooling

- [x] Document the one-line requirement for adding a new internal compiled target.
- [x] Update CMake comments so the audit, not a prose target list, is authoritative.

## Acceptance criteria

- [x] No manually maintained global list determines which internal targets receive policy.
- [x] Every internal compiled target is configured or explicitly exempted with a reason.
- [x] Third-party/FetchContent targets receive no project flags accidentally.
- [x] Configure, focused builds, full `draxul-tests`, and smoke remain green. The
      2026-09-21 Debug validation built `draxul` and `draxul-tests`, passed all
      45 unit CTest entries, rendered all five default Metal snapshots, and passed
      the same-cache startup smoke.

## Dependencies and ownership

No prerequisites. One build-system agent owns the helper, root cleanup, and audit.
Module adoption may be delegated only after the helper contract is frozen. Blocks
all other pending refactor cards that add or move targets.
