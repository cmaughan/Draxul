# Localize and audit internal-target build policy

**Type:** refactor
**Priority:** P1 / sequence 00
**Raised by:** GPT/Codex
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 1

## Goal

Replace the manually maintained root target list for MSVC `/FS` with one
target-local helper plus a configure-time completeness audit.

The project's ASan, TSan, and instrumented-coverage build modes were retired on
2026-09-22. They are intentionally outside this policy contract.

## Boundary verification

- [x] Inventory every project-owned compiled, OBJECT, executable, INTERFACE,
      imported, alias, and third-party target after all optional subdirectories load.
- [x] Re-inventory current core/plugin-support targets, including NanoVG, renderer
      OBJECT targets, Vulkan resources, and test-support leaves; external
      product-owned targets remain explicitly outside core policy.
- [x] Record the existing `/FS` behavior before changing it: the root manual
      library list applied `/FS` without target-type filtering.
- [x] Define explicit skip rules for imported, alias, INTERFACE, and third-party targets.

## Implementation and migration

- [x] Add `draxul_configure_internal_target(target)` in project-owned CMake infrastructure.
- [x] Apply the remaining compile policy to STATIC/OBJECT/executable targets.
- [x] Mark every considered target with a property suitable for a final audit.
- [x] Prove the helper on one STATIC, OBJECT, executable, and INTERFACE target.
- [x] Adopt the helper beside each internal target definition, directory by directory.
- [x] Add the end-of-configure audit and remove root enumerations only after it passes.
- [x] Keep the migration configurable and buildable.

## Unit and configure tests

- [x] Add a configure-time negative fixture for an unconfigured internal compiled target.
- [x] Add positive checks for STATIC, OBJECT, executable, INTERFACE, and explicit skip cases.
- [x] Run the Python configure-test driver.
- [x] Build `draxul-renderer-core` and the aggregate tests under the normal Debug preset.

## Cross-platform validation

- [x] Windows/MSVC: build compiled targets in parallel and verify `/FS` is applied.
- [x] Verify both Metal and Vulkan OBJECT declarations are covered by the shared
      directory policy; exercise Metal on macOS and Vulkan on Windows.
- [x] Configure once with all products disabled and once with enabled products absent;
      prove the optional core graph still audits cleanly and mounted product source
      roots remain under product-owned policy.
- [x] Run normal Debug and Release aggregate tests and same-cache smoke validation.

## Agent documentation and tooling

- [x] Document the one-line requirement for adding a new internal compiled target.
- [x] Update CMake comments so the audit, not a prose target list, is authoritative.

## Acceptance criteria

- [x] No manually maintained global list determines which internal targets receive policy.
- [x] Every internal compiled target is configured or explicitly exempted with a reason.
- [x] Third-party/FetchContent targets receive no project flags accidentally.
- [x] Configure, aggregate tests, and smoke remain green.

## Validation record

- The policy fixture proves positive STATIC, OBJECT, executable, and INTERFACE
  handling plus a negative omitted-target case.
- All-products-off and enabled-but-absent configurations completed the core audit.
- Windows Debug and Release builds compiled the project-owned target graph with
  `/FS`; aggregate tests and same-cache startup smoke passed.
- On 2026-09-22, the obsolete ASan, TSan, and instrumented-coverage helpers,
  presets, and gates were removed. The policy fixture now asserts only supported
  behavior.

## Dependencies and ownership

No prerequisites. The shared helper contract and final audit are now authoritative
for project-owned targets.
