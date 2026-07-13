# Fast Unit-Test Workflow Design

**Date:** 2026-07-13

## Goal

Make `do.py test` a substantially faster unit-test command without reducing the unit coverage that currently executes by default. Startup smoke tests and render snapshots remain available through their existing explicit commands and through the repository's full validation scripts.

## Current-State Evidence

The existing test workflow has four independent performance problems:

1. `do.py test` delegates to the full validation script. That script builds the default CMake target and runs unit tests, the application smoke test, and every registered render snapshot. A unit-test edit therefore pays for the application executable, platform shaders, staged resources, and GPU-facing validation.
2. All 155 C++ test sources compile into one `draxul-tests` executable. An eight-way Debug compile profile took 74.12 seconds. The largest source, `megacity_scene_tests.cpp`, took 14.81 seconds; the next group took 5.8-7.7 seconds each.
3. The Catch2 suite runs as one CTest entry. The current coverage-instrumented binary took 175.99 seconds serially. Four native Catch2 shards passed in 60.86 seconds, a 2.9x wall-clock improvement.
4. Several tests perform disproportionate work or duplicate stronger coverage. The worst bounds case allocates a clamped 10,000 by 10,000 grid and takes 6.70 seconds. A second large-grid test allocates ten million cells while asserting behavior already covered by a small-grid test.

A targeted precompiled-header experiment on the twelve heaviest test sources reduced their combined compiler time from 79.03 to 55.43 seconds and their four-way wall time from 22.00 to 16.26 seconds once the PCH existed.

## Command Semantics

### `do.py test`

`do.py test` becomes the fast unit-test entry point:

- configure the Debug build when the current cache is missing or incompatible;
- build `draxul-tests`, including its existing `draxul-rpc-fake` dependency and required libraries;
- do not build the `draxul` application executable or its shader/resource staging targets;
- run the C++ unit suite as four parallel Catch2 shards;
- run `tests/do_py_tests.py`, which is a unit suite for the command itself but is currently unregistered;
- preserve the existing `DRAXUL_RUN_SLOW_TESTS` behavior, so locally skipped stress/fuzz cases and CI-enabled slow cases behave as before.

The command succeeds only if every selected shard and the Python unit suite succeed. A failed shard must retain CTest's failure output and non-zero exit status.

### Explicit application validation

Application-facing checks remain explicit:

- `do.py smoke` runs the startup smoke test;
- `do.py basic`, `cmdline`, `unicode`, and `panel` run individual render comparisons;
- `do.py renderall` runs all standard render comparisons;
- `t.sh`, `t.bat`, and the underlying full validation scripts retain their current unit, smoke, and render scope for pre-commit, release, and CI use.

This separation makes the common edit-test loop fast while retaining the existing full validation path.

## Build Architecture

### Test-only precompiled header

Add `tests/support/test_pch.h` and apply it only to `draxul-tests` with `target_precompile_headers`. It contains the stable, broadly shared Catch2 and test-support includes. Production libraries and the application do not inherit it.

CMake owns PCH generation on AppleClang and MSVC. Test sources may keep their direct includes for clarity and standalone tooling; include guards make those includes inexpensive after the PCH is loaded.

### MegaCity test decomposition

Replace the 3,498-line `megacity_scene_tests.cpp` translation unit with focused sources organized around its existing responsibilities:

- world construction, presentation records, performance state, and picking;
- semantic layout, roads, routes, signs, and mesh behavior;
- camera behavior and host integration.

Shared builders and fakes move into a focused MegaCity test-support header. Helpers defined in that header must be inline or class-local so the split does not create duplicate symbols. Test names, tags, and assertions remain unchanged unless a test is removed by the approved redundancy cleanup below.

The split shortens the longest individual compile and allows those groups to compile concurrently. The PCH limits the repeated header-parsing cost introduced by the additional translation units.

### Catch2 sharding through CTest

Replace the single `draxul-tests` CTest registration with four entries that execute the same binary using Catch2's `--shard-count 4` and shard indexes 0 through 3. Every discovered test case belongs to exactly one shard.

All four entries carry a `unit` label. The Python suite is registered as a fifth `unit` test. The unit path runs those labeled tests with four-way CTest parallelism.

Smoke and render entries retain their existing commands. They share a CTest resource lock so multiple application/GPU tests never run concurrently when the full validation scripts use parallel CTest. Unit shards may overlap with each other; the full scripts may either overlap unit work with the GPU lock or run the application checks after units, whichever proves more stable during verification.

Coverage, sanitizer, and CI filters that currently match `draxul-tests` must be updated to include all shard entries. Coverage still exports the same `draxul-tests` executable, so no multi-binary merge is introduced.

## Unit-Test Cleanup

Only tests with direct current-tree evidence of redundancy or ineffectiveness are removed.

### Expensive tests

- Change the pathological grid clamp case to exercise an oversized column with one row and an oversized row with one column. This verifies both independent clamps without allocating 100 million cells.
- Remove `grid clear does not allocate per-cell for large dimensions`. It does not inspect the internal dirty vector and therefore cannot prove its allocation claim. The smaller `grid clear sets full_dirty flag instead of per-cell push` case already verifies the observable full-dirty behavior.
- Remove the 20-step TextService reinitialization case from `dpi_scaling_tests.cpp`. The DPI hotplug integration fixture uses the same scale sequence while also checking coordinated renderer and TextService state.

### Fully subsumed sources

- Delete `config_edge_case_tests.cpp`. Its save/load round trip, missing-file fallback, empty-config fallback, and known-plus-unknown-key behavior are all covered more thoroughly in `app_config_tests.cpp` and `corrupt_config_recovery_tests.cpp`.
- Delete `keybinding_conflict_tests.cpp`. Production TOML warning behavior is covered by `app_config_tests.cpp`, first-match dispatch by `keybinding_dispatch_tests.cpp`, and several cases in this file merely exercise `std::vector` behavior or reproduce the production comparison loop inside the test.

### Consolidated sources

- Remove the nine fuzzy-match cases from `command_palette_tests.cpp`. `fuzzy_match_tests.cpp` covers the same empty, mismatch, case-folding, scoring, boundary, and position behavior more thoroughly. Palette state and host behavior remain in `command_palette_tests.cpp`.
- Move the meaningful grid resize assertions from `resize_cascade_tests.cpp` into `grid_tests.cpp`: dimension changes, in-bounds cell preservation across resize, and full-dirty state after resize. Delete repeated panel-layout arithmetic, trivial repeat-call cases, and the three unconditional integration `SKIP()` stubs, then delete the emptied source.
- Move the fake frame-context/render-pass assertion from `renderer_shutdown_tests.cpp` into the existing renderer state tests. Delete the fake shutdown no-op assertions and the opt-in real-GPU case whose body is unimplemented and deliberately fails when enabled, then delete the emptied source.

### Preserved tests and known gaps

- Preserve the MPack and VT fuzz/stress cases and their environment-controlled execution.
- Preserve no-crash destructor tests that execute real threading or process teardown paths; a `SUCCEED` endpoint does not make those tests redundant when reaching it proves safe cleanup.
- Do not address the macOS exclusion of `session_attach_tests.cpp` in this change. It is a real coverage gap, but fixing the Catch2/chrono incompatibility is separate from test-speed work.
- Do not remove backend-paired Windows and macOS process tests merely because their names are similar; they verify different platform implementations.

## Script and Documentation Changes

The Unix and Windows test scripts gain a unit-only path used by `do.py test`. Their existing default behavior remains the full validation path. Both platforms must:

- select or configure the requested Debug build consistently;
- build only `draxul-tests` for the unit path;
- use the same four-shard CTest selection;
- propagate failures and preserve readable failure output;
- honor `DRAXUL_RUN_SLOW_TESTS` without special platform behavior.

Update `do.py --help`, the README testing section, and `docs/features.md` so the fast unit command and explicit smoke/render commands are accurately described.

## Error Handling

- A missing or incompatible build cache triggers configuration through the existing platform preset rather than attempting to build stale generated files.
- A compile failure stops before test execution and returns the compiler's status.
- Any failed Catch2 shard or Python unit test fails `do.py test`.
- A missing Python interpreter is treated as a configuration error on paths that invoke the Python suite; the command already requires Python to run `do.py`.
- Full validation retains existing timeouts for application-facing tests. Unit shard timeouts must be long enough for `DRAXUL_RUN_SLOW_TESTS=1` in CI.

## Verification

Verification compares behavior and timing, not merely command success:

1. Run the pre-change unit binary serially and record wall time, test-case count, assertion count, and skip count.
2. Build the changed unit target from invalidated test objects and record total build time plus the longest test-source compiles.
3. Run `do.py test` and confirm all four C++ shards and the Python suite pass.
4. Sum shard test-case and assertion counts and reconcile them with the approved removals and the newly registered Python suite.
5. Run with `DRAXUL_RUN_SLOW_TESTS=1` and confirm slow cases execute rather than skip.
6. Run the repository-required full build and `do.py smoke`.
7. Run the full `t.sh`/`t.bat` path on the available platform and confirm smoke/render coverage remains registered.
8. Inspect CTest discovery to verify four disjoint unit shards, the Python unit entry, and serialized GPU-facing entries.

Windows correctness is verified through CMake generation and script review locally when Windows is unavailable, then through the Windows CI job. macOS receives the full local build, unit, smoke, and render validation.

## Success Criteria

- `do.py test` builds and runs only unit-test targets.
- All unit behavior retained by this design runs exactly once per invocation.
- The four Catch2 shards pass concurrently on macOS and Windows.
- The Python `do.py` unit suite is included.
- Smoke and render commands remain available and the full validation scripts retain them.
- The pathological grid test no longer creates a 100-million-cell allocation.
- The measured unit runtime is materially below the 175.99-second serial baseline; the prototype establishes approximately 60.86 seconds before test cleanup.
- The test compile critical path is materially below the 14.81-second MegaCity baseline, and the PCH reduces repeated header parsing without affecting production targets.
- Documentation describes the new command boundary accurately.
