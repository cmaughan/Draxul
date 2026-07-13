# Fast Unit-Test Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `do.py test` build and run only the complete unit suite, while retaining explicit smoke/render validation and the full `t.sh`/`t.bat` workflow.

**Architecture:** Keep one Catch2 executable for coverage and linkage simplicity, but register it as four disjoint CTest shards and add the Python `do.py` suite under a shared `unit` label. Reduce compilation with a test-only PCH and three focused MegaCity translation units, then remove tests proven redundant or ineffective by the profiling audit.

**Tech Stack:** Python 3, POSIX shell, Windows batch, CMake/CTest, Catch2 3.7, C++20, AppleClang/MSVC.

---

## File Map

- `do.py`: route `test` to the scripts' unit-only mode, describe the new command boundary, and make coverage select the labeled unit suite.
- `scripts/run_tests.sh`: add `--unit`; build only `draxul-tests` and run `ctest -L unit -j 4` in that mode while preserving the default full workflow.
- `scripts/run_tests.bat`: provide the same unit-only behavior for Visual Studio/Ninja builds on Windows.
- `tests/CMakeLists.txt`: apply the test PCH, register four Catch2 shards plus the Python unit suite, and label all five entries `unit`.
- `CMakeLists.txt`: label and serialize app smoke/render tests with a shared CTest resource lock.
- `tests/support/test_pch.h`: stable Catch2 and common test-support includes used only by `draxul-tests`.
- `tests/support/megacity_scene_test_support.h`: inline helpers/fakes shared by the split MegaCity test sources.
- `tests/megacity_scene_world_tests.cpp`: world construction, snapshots, performance state, and picking tests.
- `tests/megacity_scene_layout_tests.cpp`: semantic layout, roads, routes, signs, and mesh tests.
- `tests/megacity_scene_host_tests.cpp`: camera and MegaCity/BioView host integration tests.
- `tests/megacity_scene_tests.cpp`: removed after its tests are moved unchanged into the focused files.
- `tests/grid_oob_tests.cpp`, `tests/grid_tests.cpp`, `tests/dpi_scaling_tests.cpp`: remove pathological work and consolidate resize coverage.
- `tests/command_palette_tests.cpp`, `tests/renderer_state_tests.cpp`: retain unique behavior moved from redundant files.
- `tests/config_edge_case_tests.cpp`, `tests/keybinding_conflict_tests.cpp`, `tests/resize_cascade_tests.cpp`, `tests/renderer_shutdown_tests.cpp`: delete fully subsumed or ineffective sources.
- `README.md`, `docs/features.md`: document fast unit versus explicit/full application validation.

### Task 1: Unit-only command contract

**Files:**
- Modify: `tests/do_py_tests.py`
- Modify: `do.py`
- Modify: `scripts/run_tests.sh`
- Modify: `scripts/run_tests.bat`

- [ ] **Step 1: Add command-routing assertions**

Patch `tests/do_py_tests.py` to mock `run`, set a temporary `sys.argv`, and assert that `main()` passes `--unit` to the platform script. Also assert `help_text()` describes `test` as unit-only and the full scripts separately.

```python
class TestCommandTests(unittest.TestCase):
    def test_help_describes_fast_unit_scope(self) -> None:
        help_output = draxul_do.help_text()
        self.assertIn("Run unit tests", help_output)
        self.assertNotIn("test         Run the full local test suite", help_output)
```

- [ ] **Step 2: Run the focused Python suite and observe the expected failure**

Run: `python3 -m unittest tests.do_py_tests.TestCommandTests -v`

Expected: FAIL because `help_text()` and `main()` still use the full test scripts without `--unit`.

- [ ] **Step 3: Route `do.py test` into unit mode**

Change the dispatch to:

```python
if command == "test":
    if sys.platform.startswith("win"):
        return run(["cmd", "/c", "t.bat", "--unit"], root)
    return run(["sh", "./scripts/run_tests.sh", "--unit"], root)
```

Change the help entry to `Run unit tests (four C++ shards plus do.py tests)`.

- [ ] **Step 4: Add `--unit` to both platform scripts**

Parse a `UNIT_ONLY` boolean. In each platform's `run_config`, use the following branch after configuration:

```sh
if [ "$UNIT_ONLY" -eq 1 ]; then
  run cmake --build build --target draxul-tests --parallel
  run ctest --test-dir build --label-regex unit --parallel 4 --output-on-failure --timeout 120
  return
fi
```

Use the equivalent `%UNIT_ONLY%`, `--config %CONFIG%`, and `--build-config %CONFIG%` commands in `run_tests.bat`. Reject `both --unit` because the shared build directory cannot retain both single-config macOS configurations and a unit edit loop should have one deterministic Debug configuration.

- [ ] **Step 5: Run the Python command tests**

Run: `python3 -m unittest tests.do_py_tests -v`

Expected: all Python tests pass.

- [ ] **Step 6: Commit the command contract**

```bash
git add do.py scripts/run_tests.sh scripts/run_tests.bat tests/do_py_tests.py
git commit -m "test: add fast unit-only command path"
```

### Task 2: CTest sharding, Python registration, and GPU serialization

**Files:**
- Create: `tests/support/test_pch.h`
- Modify: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `do.py`

- [ ] **Step 1: Add the test-only PCH**

Create:

```cpp
#pragma once

#include <catch2/catch_all.hpp>
#include "test_support.h"
```

Apply it only to `draxul-tests`:

```cmake
target_precompile_headers(draxul-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support/test_pch.h
)
```

- [ ] **Step 2: Register four disjoint Catch2 shards**

Replace the single test entry with:

```cmake
foreach(DRAXUL_TEST_SHARD_INDEX RANGE 0 3)
    add_test(
        NAME draxul-tests-shard-${DRAXUL_TEST_SHARD_INDEX}
        COMMAND $<TARGET_FILE:draxul-tests>
            --shard-count 4
            --shard-index ${DRAXUL_TEST_SHARD_INDEX}
    )
    set_tests_properties(draxul-tests-shard-${DRAXUL_TEST_SHARD_INDEX}
        PROPERTIES LABELS unit TIMEOUT 120)
endforeach()
```

Use `find_package(Python3 REQUIRED COMPONENTS Interpreter)` and register `draxul-do-py-tests` with `${Python3_EXECUTABLE} -m unittest tests.do_py_tests`, working directory `${CMAKE_SOURCE_DIR}`, label `unit`, and timeout 120.

- [ ] **Step 3: Label and serialize application tests**

Set `draxul-app-smoke` to `LABELS smoke`, and every `draxul-render-*` entry to `LABELS render`. Give both groups `RESOURCE_LOCK draxul_gpu` so full CTest runs never overlap app/GPU processes.

- [ ] **Step 4: Update coverage selection**

Change the coverage command in `do.py` from `-R draxul-tests` to `--label-regex unit --parallel 4`, retaining `--output-on-failure`.

- [ ] **Step 5: Verify discovery and shard completeness**

Run:

```bash
cmake --preset mac-debug
ctest --test-dir build --show-only=json-v1
```

Expected: four `draxul-tests-shard-*` entries and one `draxul-do-py-tests` entry labeled `unit`; smoke/render entries carry the resource lock.

- [ ] **Step 6: Build and run the unit label**

Run: `cmake --build build --target draxul-tests --parallel && ctest --test-dir build -L unit -j 4 --output-on-failure`

Expected: five tests pass and no application executable is invoked.

- [ ] **Step 7: Commit the CTest topology**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/support/test_pch.h do.py
git commit -m "test: shard unit suite across CTest workers"
```

### Task 3: Split the MegaCity compile bottleneck

**Files:**
- Create: `tests/support/megacity_scene_test_support.h`
- Create: `tests/megacity_scene_world_tests.cpp`
- Create: `tests/megacity_scene_layout_tests.cpp`
- Create: `tests/megacity_scene_host_tests.cpp`
- Delete: `tests/megacity_scene_tests.cpp`

- [ ] **Step 1: Extract shared helpers as inline definitions**

Move shared includes, helper functions, `ShutdownOrderImGuiHost`, `TestLotRect`, roof-sign constants, `RoofSignSemanticFixture`, and its builder into the support header. Keep the existing `#ifdef DRAXUL_ENABLE_MEGACITY` guard and mark namespace-scope functions `inline` to avoid duplicate definitions.

- [ ] **Step 2: Move world and snapshot cases**

Move lines 262-1339 of the original source into `megacity_scene_world_tests.cpp`, preserving every test name, tag, assertion, and section. Include only the shared support header plus directly needed production headers.

- [ ] **Step 3: Move layout and route cases**

Move the transform case and lines 1506-2461 plus the final mesh-library case into `megacity_scene_layout_tests.cpp`. Preserve ordering inside each test and retain all geometry assertions.

- [ ] **Step 4: Move camera and host cases**

Move camera cases at lines 1386-1505 and 2462-3449 into `megacity_scene_host_tests.cpp`. Keep the `private` test seam around `megacity_host.h` in the shared support header and preserve asynchronous cleanup assertions.

- [ ] **Step 5: Remove the monolithic source and verify names**

Compare sorted `TEST_CASE` names before/after:

```bash
rg '^TEST_CASE' tests/megacity_scene_{world,layout,host}_tests.cpp | sed 's/^[^:]*://' | sort
```

Expected: the same 70 test cases as the original source, with no duplicate names.

- [ ] **Step 6: Rebuild and run MegaCity tests**

Run: `cmake --build build --target draxul-tests --parallel && build/tests/draxul-tests '[megacity]'`

Expected: all retained MegaCity cases pass.

- [ ] **Step 7: Commit the test decomposition**

```bash
git add tests/megacity_scene_tests.cpp tests/megacity_scene_world_tests.cpp tests/megacity_scene_layout_tests.cpp tests/megacity_scene_host_tests.cpp tests/support/megacity_scene_test_support.h
git commit -m "test: split megacity scene test compilation"
```

### Task 4: Remove pathological and subsumed tests

**Files:**
- Modify: `tests/grid_oob_tests.cpp`
- Modify: `tests/grid_tests.cpp`
- Modify: `tests/dpi_scaling_tests.cpp`
- Modify: `tests/command_palette_tests.cpp`
- Modify: `tests/renderer_state_tests.cpp`
- Delete: `tests/config_edge_case_tests.cpp`
- Delete: `tests/keybinding_conflict_tests.cpp`
- Delete: `tests/resize_cascade_tests.cpp`
- Delete: `tests/renderer_shutdown_tests.cpp`

- [ ] **Step 1: Replace the 100-million-cell clamp case**

Use two small sections:

```cpp
SECTION("excessive columns clamp independently")
{
    grid.resize(100000, 1);
    REQUIRE(grid.cols() == 10000);
    REQUIRE(grid.rows() == 1);
}
SECTION("excessive rows clamp independently")
{
    grid.resize(1, 100000);
    REQUIRE(grid.cols() == 1);
    REQUIRE(grid.rows() == 10000);
}
```

- [ ] **Step 2: Consolidate unique resize behavior**

Move dimension, in-bounds content preservation, and dirty-after-resize assertions into `grid_tests.cpp`. Delete the ten-million-cell clear test and remove `resize_cascade_tests.cpp`, including its duplicated layout arithmetic and unconditional `SKIP()` stubs.

- [ ] **Step 3: Remove the duplicated DPI stress case**

Delete `dpi hotplug stress: 20 rapid scale changes produce consistent final state` from `dpi_scaling_tests.cpp`; retain the stronger integration fixture with renderer and TextService checks.

- [ ] **Step 4: Remove duplicated fuzzy-match coverage**

Delete only the first nine `[fuzzy]` cases from `command_palette_tests.cpp`. Keep palette lifecycle, rendering, prompt, and host cases.

- [ ] **Step 5: Preserve the useful renderer-frame assertion**

Move `TestRenderPass` and `renderer frame context: fake renderer accepts recorded render passes` into `renderer_state_tests.cpp`, adding the fake renderer/window includes there. Delete `renderer_shutdown_tests.cpp` and its no-op fake shutdown cases and deliberately failing GPU stub.

- [ ] **Step 6: Delete fully subsumed sources**

Delete `config_edge_case_tests.cpp` and `keybinding_conflict_tests.cpp`; their stronger production-path coverage remains in the files identified by the design audit.

- [ ] **Step 7: Build and run focused tags**

Run:

```bash
cmake --build build --target draxul-tests --parallel
build/tests/draxul-tests '[grid]'
build/tests/draxul-tests '[renderer]'
build/tests/draxul-tests '[display],[dpi_integration]'
build/tests/draxul-tests '[palette],[fuzzy_match]'
```

Expected: every retained focused suite passes and the grid clamp case completes without a huge allocation.

- [ ] **Step 8: Commit the cleanup**

```bash
git add tests
git commit -m "test: remove redundant and pathological cases"
```

### Task 5: Document the validation boundary

**Files:**
- Modify: `README.md`
- Modify: `docs/features.md`

- [ ] **Step 1: Update user-facing command examples**

Describe `do.py test`/`do test` as the fast unit workflow. State that `do.py smoke` and render shortcuts are explicit, while `t.sh`, `t.bat`, and `scripts/run_tests.*` still perform full unit + smoke + render validation.

- [ ] **Step 2: Document the build/test architecture**

Add the four Catch2 shards, Python `do.py` suite, unit-only target build, test PCH, and serialized GPU checks to `docs/features.md`.

- [ ] **Step 3: Check the docs for stale wording**

Run: `rg -n 'do(\.py)? test.*[Ff]ull|test         Run the full' README.md docs/features.md do.py`

Expected: no result claiming `do.py test` is full validation.

- [ ] **Step 4: Commit the documentation**

```bash
git add README.md docs/features.md
git commit -m "docs: explain fast and full test workflows"
```

### Task 6: Benchmark and full verification

**Files:**
- Modify only if verification exposes a defect in the files above.

- [ ] **Step 1: Reconfigure Debug and invalidate the test PCH and objects**

Run: `cmake --preset mac-debug && touch tests/support/test_pch.h`

Expected: CMake retains built dependencies, while the changed PCH timestamp makes the next `draxul-tests` build regenerate its PCH and every dependent test object.

- [ ] **Step 2: Measure the unit build**

Run: `/usr/bin/time -p cmake --build build --target draxul-tests --parallel 8`

Expected: build passes; the split prevents one 14.81-second translation unit from defining the compile critical path and PCH creation is amortized across test sources.

- [ ] **Step 3: Measure the fast command**

Run: `/usr/bin/time -p python3 do.py test`

Expected: four C++ shards and `draxul-do-py-tests` pass; wall time is materially below the 175.99-second serial baseline and no smoke/render entry runs.

- [ ] **Step 4: Verify slow-test opt-in**

Run: `DRAXUL_RUN_SLOW_TESTS=1 ctest --test-dir build -L unit -j 4 --output-on-failure --timeout 600`

Expected: all environment-gated slow cases execute and pass rather than skipping.

- [ ] **Step 5: Run repository-required application validation**

Run:

```bash
cmake --build build --target draxul draxul-tests --parallel
python3 do.py smoke
```

Expected: both targets build and the smoke application exits successfully.

- [ ] **Step 6: Run full local validation**

Run: `./t.sh`

Expected: all unit shards, Python tests, app smoke, and available render snapshots pass; app/GPU tests execute serially.

- [ ] **Step 7: Cross-platform static checks**

Review the Windows batch branch and run `cmake --preset mac-debug` plus CMake generation checks. Windows runtime behavior is completed by the repository Windows CI because no Windows host is locally available.

- [ ] **Step 8: Final consistency checks**

Run:

```bash
git diff --check
git status --short
```

Expected: no whitespace errors and only intentional implementation changes remain.
