# MegaCity Tree-Sitter Module Boundaries Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make MegaCity use Tree-sitter as the only runtime semantic source and group files under `modules/<name>` as separate city modules.

**Architecture:** Move module-path ownership rules into `draxul-citymodel` so the semantic source and Tree-sitter UI share one resolver. Remove the Graphify semantic source from runtime config, host startup, UI, build inputs, tests, and user-facing docs; stale `code_source = "graphify"` config falls back to Tree-sitter.

**Tech Stack:** C++20, CMake, Catch2, Python unittest for `do.py`.

---

### Task 1: Add Tests For Module Boundaries And Graphify Fallback

**Files:**
- Modify: `tests/treesitter_semantic_source_tests.cpp`
- Modify: `tests/app_config_tests.cpp`
- Modify: `tests/do_py_tests.py`
- Modify: `tests/megacity_scene_tests.cpp`

- [ ] **Step 1: Add Tree-sitter module-boundary tests**

Add a test that builds a `CodebaseSnapshot` with files under `modules/markdown`, `modules/kanban`, `modules/megacity`, `libs/draxul-grid`, `app`, and a root file. Assert `TreeSitterSemanticSource::list_modules()` returns distinct module paths, including `modules/markdown`, `modules/kanban`, and `modules/megacity`.

- [ ] **Step 2: Add stale Graphify config fallback test**

Change the existing Megacity config test so `code_source = "graphify"` loads as `MegaCityCodeSource::TreeSitterDb` and saved config serializes `code_source = "treesitter_db"` without `graphify_graph_path`.

- [ ] **Step 3: Add `do.py` parser rejection tests**

Update `tests/do_py_tests.py` so `--parser graphify` raises `ValueError`, `--parser treesitter` still normalizes to `treesitter_db`, and config merge writes only `code_source = "treesitter_db"`.

- [ ] **Step 4: Replace Graphify host tests**

Remove tests that expect Graphify startup. Add a host test where config contains `code_source = "graphify"` and a missing `graphify_graph_path`, but initialization still starts the Tree-sitter scanner instead of failing on the graph path.

- [ ] **Step 5: Run red tests**

Run:

```powershell
python -m unittest tests.do_py_tests
.\build\tests\Debug\draxul-tests.exe "[megacity][treesitter]"
.\build\tests\Debug\draxul-tests.exe "[config][megacity]"
.\build\tests\Debug\draxul-tests.exe "[megacity][graphify]"
```

Expected before implementation: at least the new/changed tests fail because `modules/<name>` still collapses under `modules`, Graphify remains accepted, and Graphify host startup still tries to load graph JSON.

### Task 2: Centralize Module Path Resolution

**Files:**
- Create: `modules/megacity/draxul-citymodel/include/draxul/module_path_resolver.h`
- Create: `modules/megacity/draxul-citymodel/src/module_path_resolver.cpp`
- Modify: `modules/megacity/draxul-citymodel/CMakeLists.txt`
- Modify: `modules/megacity/draxul-citymodel/src/treesitter_semantic_source.cpp`
- Modify: `modules/megacity/draxul-megacity/src/ui_treesitter_panel.cpp`

- [ ] **Step 1: Add resolver API**

Create `draxul::module_path_for_source_file(std::string_view file_path)` with these rules:

```text
empty path -> ""
root file with extension -> "."
app/... -> app
libs/<name>/... -> libs/<name>
modules/<name>/... -> modules/<name>
other/path/... -> other
```

- [ ] **Step 2: Use resolver from semantic source**

Replace the private `module_path_for_file()` helper in `TreeSitterSemanticSource` with `module_path_for_source_file()`.

- [ ] **Step 3: Use resolver from Tree-sitter UI**

Replace the duplicate `logical_module_for_file()` helper in `ui_treesitter_panel.cpp` with `module_path_for_source_file()`.

- [ ] **Step 4: Run module tests**

Run:

```powershell
.\build\tests\Debug\draxul-tests.exe "[megacity][treesitter]"
```

Expected: module-boundary tests pass.

### Task 3: Remove Graphify Runtime Source

**Files:**
- Delete: `modules/megacity/draxul-megacity/src/graphify_semantic_source.cpp`
- Delete: `modules/megacity/draxul-megacity/src/graphify_semantic_source.h`
- Delete: `tests/graphify_semantic_source_tests.cpp`
- Modify: `modules/megacity/draxul-megacity/CMakeLists.txt`
- Modify: `modules/megacity/draxul-megacity/include/draxul/megacity_code_config.h`
- Modify: `modules/megacity/draxul-megacity/src/megacity_code_config.cpp`
- Modify: `modules/megacity/draxul-megacity/include/draxul/megacity_host.h`
- Modify: `modules/megacity/draxul-megacity/src/megacity_host.cpp`
- Modify: `modules/megacity/draxul-megacity/src/ui_treesitter_panel.cpp`
- Modify: `do.py`

- [ ] **Step 1: Simplify config type**

Remove `MegaCityCodeSource::Graphify` and `graphify_graph_path`. Keep parsing `code_source` tolerant: any unknown value, including `graphify`, maps to `TreeSitterDb`.

- [ ] **Step 2: Simplify host startup and rebuild**

Remove graph loading, graph source ownership, graph path resolution, and Graphify-specific rebuild branches. MegaCity always starts the Tree-sitter scanner.

- [ ] **Step 3: Simplify UI**

Remove the source combo and graph path text box. Replace stale "Graphify source" empty-state text with the existing "(starting...)" Tree-sitter state.

- [ ] **Step 4: Simplify `do.py` parser helper**

Reject `graphify`. Keep `treesitter`, `tree_sitter`, `treesitter_db`, and `tree_sitter_db` as accepted aliases so existing Tree-sitter helper usage still works.

- [ ] **Step 5: Remove build inputs**

Remove Graphify source files from CMake and remove the private `nlohmann_json::nlohmann_json` dependency if nothing else in `draxul-megacity` uses it.

### Task 4: Update Docs And Verify

**Files:**
- Modify: `docs/features.md`

- [ ] **Step 1: Update feature docs**

Document MegaCity as Tree-sitter-only, remove Graphify code-source mode, remove `graphify_graph_path`, and update `do.py` examples.

- [ ] **Step 2: Search for remnants**

Run:

```powershell
rg -n "Graphify|graphify|graphify_graph_path|MegaCityCodeSource::Graphify|--parser graphify" modules app libs tests docs do.py CMakeLists.txt
```

Expected: no active code/docs references remain, except historical plan files if intentionally left untouched.

- [ ] **Step 3: Run focused verification**

Run:

```powershell
python -m unittest tests.do_py_tests
cmake --build build --config Debug --target draxul-tests
.\build\tests\Debug\draxul-tests.exe "[megacity]"
.\build\tests\Debug\draxul-tests.exe "[config][megacity]"
```

Expected: all pass.

- [ ] **Step 4: Run broad verification**

Run:

```powershell
python do.py test
git diff --check
```

Expected: local wrapper test suite passes and diff check reports no whitespace errors.
