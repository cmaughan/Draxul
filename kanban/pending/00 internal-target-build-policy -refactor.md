# Localize and audit internal-target build policy

**Type:** refactor
**Priority:** P1 / sequence 00
**Raised by:** GPT/Codex
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 1

## Goal

Replace the manually maintained root target lists for sanitizers, coverage, and
MSVC `/FS` with one target-local helper plus a configure-time completeness audit.

## Boundary verification

- [ ] Inventory every project-owned compiled, OBJECT, executable, INTERFACE,
  imported, alias, and third-party target after all optional subdirectories load.
- [ ] Confirm omissions called out by consensus, including Score support targets,
  NanoVG, renderer OBJECT targets, and Vulkan resources.
- [ ] Record the existing ASan/TSan/coverage and `/FS` behavior before changing it.
- [ ] Define explicit skip rules for imported, alias, INTERFACE, and third-party targets.

## Implementation and migration

- [ ] Add `draxul_configure_internal_target(target)` in project-owned CMake infrastructure.
- [ ] Apply compile options to STATIC/OBJECT/executable targets and link options only
  to target types that link.
- [ ] Mark every considered target with a property suitable for a final audit.
- [ ] Prove the helper on one STATIC, OBJECT, executable, and INTERFACE target.
- [ ] Adopt the helper beside each internal target definition, directory by directory.
- [ ] Add the end-of-configure audit and remove root enumerations only after it passes.
- [ ] Keep every intermediate commit configurable and buildable.

## Unit and configure tests

- [ ] Add a configure-time negative fixture for an unconfigured internal compiled target.
- [ ] Add positive coverage for STATIC, OBJECT, executable, INTERFACE, and explicit skip cases.
- [ ] Run `python -m unittest tests.do_py_tests` if Python drives any audit fixture.
- [ ] Build `draxul-renderer-core` and `draxul-tests` under the normal preset.

## Cross-platform validation

- [ ] Windows/MSVC: build compiled targets in parallel and verify `/FS` is applied.
- [ ] Windows: verify supported sanitizer configuration still configures and links.
- [ ] macOS: configure/build `mac-asan`, `mac-tsan`, and coverage presets.
- [ ] Verify both Metal and Vulkan OBJECT targets are considered without applying link
  options to OBJECT libraries.
- [ ] Configure with MegaCity, SatView, and ScoreView individually disabled.

## Agent documentation and tooling

- [ ] Document the one-line requirement for adding a new internal compiled target.
- [ ] Update CMake comments so the audit, not a prose target list, is authoritative.

## Acceptance criteria

- [ ] No manually maintained global list determines which internal targets receive policy.
- [ ] Every internal compiled target is configured or explicitly exempted with a reason.
- [ ] Third-party/FetchContent targets receive no project flags accidentally.
- [ ] Configure, focused builds, full `draxul-tests`, and smoke remain green.

## Dependencies and ownership

No prerequisites. One build-system agent owns the helper, root cleanup, and audit.
Module adoption may be delegated only after the helper contract is frozen. Blocks
all other pending refactor cards that add or move targets.
