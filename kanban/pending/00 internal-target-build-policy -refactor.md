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

- [x] Windows/MSVC: build compiled targets in parallel and verify `/FS` is applied.
- [x] Windows: verify supported sanitizer configuration still configures and links.
- [x] macOS ASan gate retired by project decision; no ASan run is required for this card.
- [x] macOS: configure/build the policy-sensitive `draxul-renderer-core`,
      `draxul-renderer-metal`, and `draxul-test-nanovg-paint` targets under both
      the `mac-tsan` and `mac-coverage` presets.
- [ ] macOS: build and run the full `draxul-tests` suite under the `mac-tsan` and
      `mac-coverage` presets.
- [x] Verify both Metal and Vulkan OBJECT declarations are covered by the shared
      directory policy and receive compile-only policy; exercise Metal on macOS,
      with Vulkan execution covered by the Windows gates above.
- [x] Configure once with all products disabled and once with enabled products absent;
      prove the optional core graph still audits cleanly and mounted product source
      roots remain under product-owned policy rather than the core audit.

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

## 2026-09-22 validation

- `mac-tsan` configured successfully and built `draxul-renderer-core`,
  `draxul-renderer-metal`, and `draxul-test-nanovg-paint`. The generated rules
  give both OBJECT targets `-fsanitize=thread -fno-omit-frame-pointer` compile
  flags and no link step; the executable owns the `-fsanitize=thread` link flag.
- `mac-coverage` configured successfully and built the same three targets. The
  OBJECT targets receive `-fprofile-instr-generate -fcoverage-mapping` only at
  compile time and have no `link.txt`; the executable owns the coverage link flag.
- The first TSan configure exposed a product-policy bug in PCBView: its local
  sanitizer/coverage loop attempted PRIVATE options on the INTERFACE canvas
  target. `plugins/pcbview/CMakeLists.txt` now skips `INTERFACE_LIBRARY` targets,
  after which both focused policy configurations completed. The restored normal
  Debug validation passed all 45 unit CTest entries, startup smoke, and all five
  default Metal render comparisons.
- A Debug configure with all five products disabled completed the core audit. A
  second configure with all five products enabled but pointed at an absent root
  reported each as enabled but not mounted and also completed the core audit.
- The shared cache was restored to the normal Debug configuration with all five
  real product roots enabled and TSan/coverage disabled.
- A 2026-09-22 Windows Release build compiled and linked all 325 `draxul` target
  steps with Ninja at `--parallel 32`; the generated rules apply `/FS` to the
  project-owned compiled targets. The resulting Release binary also passed the
  same-cache startup smoke.
- A separate Windows Ninja Debug cache with `DRAXUL_ENABLE_SANITIZERS=ON` and
  mounted products disabled configured and linked the instrumented `draxul.exe`.
  This validation corrected MSVC's sanitizer spelling to `/fsanitize=address`,
  relies on link.exe's default `/INFERASANLIBS` behavior, and disables only the
  STL vector/string capacity annotations needed for ABI compatibility with
  uninstrumented third-party static libraries.
