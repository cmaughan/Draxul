# Markdown And Kanban Module Move Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move Markdown and Kanban code out of `libs/` into `modules/` without changing runtime behavior or public target names.

**Architecture:** Keep existing CMake target names (`draxul-markdown`, `draxul-markdown-host`, `draxul-kanban`) so app and tests do not need include or link churn. Add module-level CMake entry points under `modules/markdown` and `modules/kanban`, mirroring the existing `modules/megacity` pattern.

**Tech Stack:** CMake, C++20, Catch2, existing Draxul host-provider registry.

---

### Task 1: Move Module Directories

**Files:**
- Move: `libs/draxul-markdown` -> `modules/markdown/draxul-markdown`
- Move: `libs/draxul-kanban` -> `modules/kanban/draxul-kanban`
- Create: `modules/markdown/CMakeLists.txt`
- Create: `modules/kanban/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Verify the current build references old paths**

Run: `rg -n "libs/draxul-markdown|libs/draxul-kanban|add_subdirectory\\(libs/draxul-(markdown|kanban)\\)" CMakeLists.txt docs AGENTS.md GEMINI.md`

Expected: current root CMake and docs reference old locations.

- [ ] **Step 2: Move the directories with Git history**

Run:
```powershell
New-Item -ItemType Directory -Force -Path modules\markdown,modules\kanban
git mv libs\draxul-markdown modules\markdown\draxul-markdown
git mv libs\draxul-kanban modules\kanban\draxul-kanban
```

- [ ] **Step 3: Add module CMake entry points**

`modules/markdown/CMakeLists.txt`:
```cmake
add_subdirectory(draxul-markdown)
```

`modules/kanban/CMakeLists.txt`:
```cmake
add_subdirectory(draxul-kanban)
```

- [ ] **Step 4: Update root CMake**

Replace:
```cmake
add_subdirectory(libs/draxul-markdown)
add_subdirectory(libs/draxul-kanban)
```

With:
```cmake
add_subdirectory(modules/markdown)
add_subdirectory(modules/kanban)
```

Expected: target names remain unchanged.

### Task 2: Update Documentation And Diagrams

**Files:**
- Modify: `AGENTS.md`
- Modify: `GEMINI.md`
- Modify: `docs/features.md`
- Regenerate: `docs/deps/*`
- Sync: `docs/api/docs/deps/*`

- [ ] **Step 1: Update architecture docs**

Document `modules/markdown/` and `modules/kanban/` as optional/product modules outside core libs. Keep host flags and feature descriptions unchanged.

- [ ] **Step 2: Regenerate dependency docs**

Run: `py scripts/gen_deps.py`

Expected: dependency graph still contains the same target names, but source layout docs no longer describe Markdown/Kanban as core `libs/`.

- [ ] **Step 3: Copy dependency docs into API docs**

Run:
```powershell
Copy-Item docs\deps\deps.dot docs\api\docs\deps\deps.dot -Force
Copy-Item docs\deps\deps_filtered.dot docs\api\docs\deps\deps_filtered.dot -Force
Copy-Item docs\deps\deps.svg docs\api\docs\deps\deps.svg -Force
```

### Task 3: Verification

**Files:**
- Test: `tests/CMakeLists.txt`
- Test: `tests/markdown_parser_tests.cpp`
- Test: `tests/kanban_store_tests.cpp`
- Test: `tests/kanban_host_tests.cpp`

- [ ] **Step 1: Configure/build Release app and tests**

Run: `cmake --build build --config Release --target draxul draxul-tests`

Expected: build succeeds with unchanged target names.

- [ ] **Step 2: Run focused Markdown/Kanban tests**

Run:
```powershell
.\build\tests\Release\draxul-tests.exe "[markdown]"
.\build\tests\Release\draxul-tests.exe "[kanban]"
```

Expected: both filters pass.

- [ ] **Step 3: Run full smoke/test wrapper**

Run: `python do.py test`

Expected: Debug build passes and CTest reports `100% tests passed`.

- [ ] **Step 4: Check diff hygiene**

Run: `git diff --check`

Expected: no whitespace errors.
