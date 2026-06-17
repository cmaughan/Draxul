# Skip CityDB For Tree-sitter Semantic City Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the MegaCity semantic city directly from the completed Tree-sitter `CodebaseSnapshot`, without opening SQLite or reconciling a `CityDatabase` during the normal Tree-sitter source path.

**Architecture:** Extract the current snapshot-to-city-record transformation out of `CityDatabase::reconcile_snapshot()` into a DB-free semantic source. `MegaCityHost` should start Tree-sitter, wait for a complete scanner snapshot, build an in-memory `ICitySemanticSource`, and call the existing `build_city()` once. SQLite can remain as a legacy/debug component during migration, but it should no longer sit between Tree-sitter analysis and the semantic city.

**Tech Stack:** C++20, CMake, Catch2, existing `draxul-treesitter`, `draxul-megacity`, `draxul-citydb`, and MegaCity semantic layout/render pipeline.

---

## Impact

Skipping the DB build removes one full persistence round trip from the Tree-sitter source path:

- Startup/rebuild work changes from `Tree-sitter scan -> SQLite reconcile -> DB query adapter -> semantic city` to `Tree-sitter scan -> in-memory semantic source -> semantic city`.
- The app stops doing SQLite open/schema/transaction/insert/query work during normal MegaCity Tree-sitter launches.
- The city no longer depends on a stale on-disk snapshot being present or valid.
- The DB failure mode disappears from the main path; parse/scan failures remain.

The removed DB step is not just storage. It currently performs meaningful transformation:

- module grouping via `module_path_for_file()`
- class/struct/free-function role selection
- method aggregation into class building layers
- field records and type dependency resolution
- abstract/interface dependency fan-out through inheritance descendants
- module-level and codebase-level health metrics
- stable ordering for modules, buildings, and dependencies

Those rules must move into a pure in-memory semantic source before the DB can be skipped safely.

Tradeoffs:

- Rebuild latency should drop because SQLite writes and reads disappear.
- Memory use may increase modestly because the semantic records live in process instead of SQLite pages, but it should be lower than carrying both DB rows and semantic records.
- The previous behavior of showing/carrying stale cached DB data before a scan completes goes away unless an explicit snapshot cache is added later.
- External SQLite inspection/debugging is lost from the hot path. Keep `draxul-citydb` temporarily for parity tests and optional debugging.
- The main-thread hitch may still exist if semantic source construction happens in `pump()` like DB reconcile does today; it should be smaller, but the plan leaves room to move it to a worker later if measured.

---

## File Structure

- Create `modules/megacity/draxul-citymodel/CMakeLists.txt`
  Defines a small shared internal library for semantic city records and Tree-sitter snapshot conversion.

- Create `modules/megacity/draxul-citymodel/include/draxul/city_semantic_records.h`
  Owns `CityClassRecord`, `CityDependencyRecord`, `CodebaseHealthMetrics`, and `CityModuleRecord`.

- Create `modules/megacity/draxul-citymodel/include/draxul/city_semantic_source.h`
  Owns `ICitySemanticSource`.

- Create `modules/megacity/draxul-citymodel/include/draxul/treesitter_semantic_source.h`
  Declares `TreeSitterSemanticSource`, a DB-free `ICitySemanticSource` built from a `CodebaseSnapshot`.

- Create `modules/megacity/draxul-citymodel/src/treesitter_semantic_source.cpp`
  Ports the transformation rules currently embedded in `CityDatabase::reconcile_snapshot()` and the DB query methods.

- Modify `modules/megacity/draxul-citydb/include/draxul/citydb.h`
  Remove duplicated semantic record definitions and include `city_semantic_records.h`.

- Modify `modules/megacity/draxul-citydb/src/citydb.cpp`
  Keep SQLite support compiling, but delegate shared transform rules where practical or use the same record names from `draxul-citymodel`.

- Modify `modules/megacity/draxul-megacity/src/city_semantic_source.h`
  Remove `ICitySemanticSource` from this local header. Keep only `CityDatabaseSemanticSource` temporarily, or move that adapter into a DB-specific header.

- Modify `modules/megacity/draxul-megacity/src/city_semantic_source.cpp`
  Keep the DB adapter implementation only while parity tests still use it.

- Modify `modules/megacity/draxul-megacity/src/graphify_semantic_source.h`
  Include the new `draxul/city_semantic_source.h`.

- Modify `modules/megacity/draxul-megacity/src/city_builder.h` and `modules/megacity/draxul-megacity/src/city_builder.cpp`
  Include the new citymodel semantic source/record headers.

- Modify `modules/megacity/draxul-megacity/include/draxul/megacity_host.h`
  Replace runtime `CityDatabase city_db_`/`city_db_reconciled_` state with `std::unique_ptr<TreeSitterSemanticSource> treesitter_source_` and snapshot-applied state.

- Modify `modules/megacity/draxul-megacity/src/megacity_host.cpp`
  Remove SQLite open/reconcile from the normal Tree-sitter path. Build `TreeSitterSemanticSource` directly from `scanner_.snapshot()` when complete.

- Modify root and module CMake files:
  Add `draxul-citymodel`, link it into `draxul-megacity`, `draxul-citydb`, and tests as needed.

- Create `tests/treesitter_semantic_source_tests.cpp`
  Unit tests for module grouping, method aggregation, free-function rows, field dependencies, inheritance fan-out, and health metrics.

- Modify `tests/citydb_tests.cpp`
  Keep DB persistence tests, but compare DB output against `TreeSitterSemanticSource` for one canonical snapshot.

- Modify `tests/megacity_scene_tests.cpp`
  Prefer `TreeSitterSemanticSource` in build-city tests that currently create a DB just to get semantic records.

- Modify `docs/features.md`
  Document that the default Tree-sitter MegaCity source now builds the semantic city directly from scanner output.

---

### Task 1: Move Semantic Record Types To A DB-Free Library

**Files:**
- Create: `modules/megacity/draxul-citymodel/CMakeLists.txt`
- Create: `modules/megacity/draxul-citymodel/include/draxul/city_semantic_records.h`
- Create: `modules/megacity/draxul-citymodel/include/draxul/city_semantic_source.h`
- Modify: `modules/megacity/draxul-citydb/include/draxul/citydb.h`
- Modify: `modules/megacity/draxul-megacity/src/city_semantic_source.h`
- Modify: `modules/megacity/draxul-megacity/src/city_builder.h`
- Modify: `modules/megacity/draxul-megacity/src/graphify_semantic_source.h`
- Modify: top-level/module CMake files that register megacity subdirectories

- [ ] **Step 1: Write the compile-first include test**

Add a small include test to `tests/megacity_scene_tests.cpp` or a new `tests/city_semantic_records_tests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <draxul/city_semantic_records.h>
#include <draxul/city_semantic_source.h>

TEST_CASE("semantic city records are available without citydb", "[megacity][citymodel]")
{
    draxul::CityClassRecord row;
    row.name = "Widget";
    row.qualified_name = "Widget";
    row.module_path = "src";
    row.entity_kind = "building";
    row.base_size = 2;
    row.building_functions = 1;
    row.function_sizes = { 5 };

    REQUIRE(row.name == "Widget");
    REQUIRE(row.function_sizes.size() == 1);
}
```

- [ ] **Step 2: Run the test and verify it fails to compile**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
```

Expected: compile fails because `draxul/city_semantic_records.h` and `draxul/city_semantic_source.h` do not exist.

- [ ] **Step 3: Create the new record header**

Move these structs, unchanged, from `modules/megacity/draxul-citydb/include/draxul/citydb.h` to `modules/megacity/draxul-citymodel/include/draxul/city_semantic_records.h`:

```cpp
#pragma once

#include <string>
#include <vector>

namespace draxul
{

struct CityClassRecord
{
    std::string name;
    std::string qualified_name;
    std::string module_path;
    std::string source_file_path;
    std::string entity_kind;
    bool is_struct = false;
    int base_size = 0;
    int building_functions = 0;
    std::vector<int> function_sizes;
    std::vector<std::string> function_names;
    int road_size = 0;
    bool is_abstract = false;
};

struct CityDependencyRecord
{
    std::string source_qualified_name;
    std::string source_module_path;
    std::string field_name;
    std::string field_type_name;
    std::string target_qualified_name;
    std::string target_module_path;
    std::string source_file_path;
    std::string target_file_path;
    bool is_abstract_ref = false;
};

struct CodebaseHealthMetrics
{
    float complexity = 0.5f;
    float cohesion = 0.5f;
    float coupling = 0.5f;
};

struct CityModuleRecord
{
    std::string module_path;
    int building_count = 0;
    int total_functions = 0;
    int total_function_lines = 0;
    float avg_function_size = 0.0f;
    float quality = 0.5f;
    CodebaseHealthMetrics health;
};

} // namespace draxul
```

- [ ] **Step 4: Create the semantic source interface header**

Create `modules/megacity/draxul-citymodel/include/draxul/city_semantic_source.h`:

```cpp
#pragma once

#include <draxul/city_semantic_records.h>

#include <string_view>
#include <vector>

namespace draxul
{

class ICitySemanticSource
{
public:
    virtual ~ICitySemanticSource() = default;

    [[nodiscard]] virtual std::vector<std::string> list_modules() const = 0;
    [[nodiscard]] virtual CityModuleRecord module_record(std::string_view module_path) const = 0;
    [[nodiscard]] virtual std::vector<CityClassRecord> list_classes_in_module(std::string_view module_path) const = 0;
    [[nodiscard]] virtual std::vector<CityDependencyRecord> list_class_dependencies_in_module(std::string_view module_path) const = 0;
    [[nodiscard]] virtual CodebaseHealthMetrics codebase_health() const = 0;
};

} // namespace draxul
```

- [ ] **Step 5: Update includes and CMake**

Update:

```cpp
// modules/megacity/draxul-citydb/include/draxul/citydb.h
#include <draxul/city_semantic_records.h>
```

Update local MegaCity includes that currently include `"city_semantic_source.h"` only for `ICitySemanticSource` to include:

```cpp
#include <draxul/city_semantic_source.h>
```

Add a `draxul-citymodel` CMake target and link it into `draxul-citydb`, `draxul-megacity`, and `draxul-tests`.

- [ ] **Step 6: Run the compile test and commit**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
```

Expected: build passes.

Commit:

```powershell
git add modules/megacity/draxul-citymodel modules/megacity/draxul-citydb modules/megacity/draxul-megacity tests CMakeLists.txt modules/megacity/CMakeLists.txt
git commit -m "refactor: move semantic city records out of citydb"
```

---

### Task 2: Add TreeSitterSemanticSource With Parity Against CityDatabase

**Files:**
- Create: `modules/megacity/draxul-citymodel/include/draxul/treesitter_semantic_source.h`
- Create: `modules/megacity/draxul-citymodel/src/treesitter_semantic_source.cpp`
- Create: `tests/treesitter_semantic_source_tests.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write a parity test for one canonical snapshot**

Create `tests/treesitter_semantic_source_tests.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <draxul/citydb.h>
#include <draxul/treesitter.h>
#include <draxul/treesitter_semantic_source.h>

#include <filesystem>

using namespace draxul;

namespace
{

CodebaseSnapshot make_semantic_fixture_snapshot()
{
    CodebaseSnapshot snapshot;
    snapshot.complete = true;

    ParsedFile file;
    file.path = "src/app/widget.cpp";

    SymbolRecord iface;
    iface.kind = SymbolKind::Class;
    iface.name = "IWidget";
    iface.is_abstract = true;
    iface.line = 3;
    iface.end_line = 8;

    SymbolRecord concrete;
    concrete.kind = SymbolKind::Class;
    concrete.name = "Widget";
    concrete.line = 10;
    concrete.end_line = 40;
    concrete.field_count = 1;
    concrete.inherited_types = { "IWidget" };
    concrete.fields.push_back(SymbolRecord::FieldRecord{
        "owner",
        "IWidget*",
        { "IWidget" },
    });

    SymbolRecord method;
    method.kind = SymbolKind::Function;
    method.name = "draw";
    method.parent = "Widget";
    method.line = 20;
    method.end_line = 29;

    file.symbols = { iface, concrete, method };
    snapshot.files.push_back(std::move(file));
    return snapshot;
}

} // namespace

TEST_CASE("TreeSitterSemanticSource matches CityDatabase semantic records", "[megacity][treesitter]")
{
    const CodebaseSnapshot snapshot = make_semantic_fixture_snapshot();

    CityDatabase db;
    REQUIRE(db.open(std::filesystem::temp_directory_path() / "draxul-treesitter-semantic-parity.sqlite"));
    REQUIRE(db.reconcile_snapshot(snapshot));
    CityDatabaseSemanticSource db_source(db);

    TreeSitterSemanticSource direct_source(snapshot);

    REQUIRE(direct_source.list_modules() == db_source.list_modules());
    REQUIRE(direct_source.module_record("src").building_count == db_source.module_record("src").building_count);
    REQUIRE(direct_source.list_classes_in_module("src").size() == db_source.list_classes_in_module("src").size());
    REQUIRE(direct_source.list_class_dependencies_in_module("src").size() == db_source.list_class_dependencies_in_module("src").size());
    REQUIRE(direct_source.codebase_health().complexity == db_source.codebase_health().complexity);
}
```

- [ ] **Step 2: Run the test and verify it fails**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[megacity][treesitter]"
```

Expected: compile fails because `TreeSitterSemanticSource` does not exist.

- [ ] **Step 3: Implement the source skeleton**

Create `treesitter_semantic_source.h`:

```cpp
#pragma once

#include <draxul/city_semantic_source.h>
#include <draxul/treesitter.h>

#include <string>
#include <unordered_map>

namespace draxul
{

class TreeSitterSemanticSource final : public ICitySemanticSource
{
public:
    explicit TreeSitterSemanticSource(const CodebaseSnapshot& snapshot);

    [[nodiscard]] std::vector<std::string> list_modules() const override;
    [[nodiscard]] CityModuleRecord module_record(std::string_view module_path) const override;
    [[nodiscard]] std::vector<CityClassRecord> list_classes_in_module(std::string_view module_path) const override;
    [[nodiscard]] std::vector<CityDependencyRecord> list_class_dependencies_in_module(std::string_view module_path) const override;
    [[nodiscard]] CodebaseHealthMetrics codebase_health() const override;

private:
    std::vector<std::string> modules_;
    std::unordered_map<std::string, CityModuleRecord> module_records_;
    std::unordered_map<std::string, std::vector<CityClassRecord>> classes_by_module_;
    std::unordered_map<std::string, std::vector<CityDependencyRecord>> dependencies_by_module_;
    CodebaseHealthMetrics health_;
};

} // namespace draxul
```

- [ ] **Step 4: Port the transformation rules**

In `treesitter_semantic_source.cpp`, port these helpers from `citydb.cpp` into private helpers:

```cpp
module_path_for_file()
make_symbol_id()
make_field_id()
CityRole
entity_spec()
build_inheritance_descendants()
resolve_dependency_targets()
```

Replace SQLite inserts with in-memory pushes:

```cpp
classes_by_module_[module_path].push_back(row);
dependencies_by_module_[source_module_path].push_back(dependency);
module_records_[module_path] = module_record;
modules_.push_back(module_path);
```

Preserve DB query ordering:

```cpp
std::sort(modules_.begin(), modules_.end());
std::sort(rows.begin(), rows.end(), [](const CityClassRecord& a, const CityClassRecord& b) {
    return a.qualified_name < b.qualified_name;
});
std::sort(deps.begin(), deps.end(), [](const CityDependencyRecord& a, const CityDependencyRecord& b) {
    return std::tie(a.source_qualified_name, a.field_name, a.target_qualified_name)
        < std::tie(b.source_qualified_name, b.field_name, b.target_qualified_name);
});
```

- [ ] **Step 5: Run parity tests and commit**

Run:

```powershell
cmake --build build --config Release --target draxul-tests
.\build\tests\Release\draxul-tests.exe "[megacity][treesitter]"
```

Expected: parity test passes.

Commit:

```powershell
git add modules/megacity/draxul-citymodel tests/treesitter_semantic_source_tests.cpp tests/CMakeLists.txt
git commit -m "feat: build semantic records directly from treesitter"
```

---

### Task 3: Switch MegaCityHost Tree-sitter Runtime To The In-Memory Source

**Files:**
- Modify: `modules/megacity/draxul-megacity/include/draxul/megacity_host.h`
- Modify: `modules/megacity/draxul-megacity/src/megacity_host.cpp`
- Modify: `tests/megacity_scene_tests.cpp`

- [ ] **Step 1: Write a host-level regression test**

Add a test that proves Tree-sitter mode no longer requires an open DB. If direct host setup is too heavy, test the extracted host helper that applies a completed snapshot:

```cpp
TEST_CASE("MegaCity Tree-sitter path rebuilds from completed scanner snapshot without citydb", "[megacity][treesitter]")
{
    CodebaseSnapshot snapshot;
    snapshot.complete = true;
    ParsedFile file;
    file.path = "src/app/widget.cpp";
    SymbolRecord widget;
    widget.kind = SymbolKind::Class;
    widget.name = "Widget";
    widget.line = 1;
    widget.end_line = 5;
    file.symbols.push_back(widget);
    snapshot.files.push_back(std::move(file));

    TreeSitterSemanticSource source(snapshot);
    REQUIRE(source.list_modules() == std::vector<std::string>{ "src" });
    REQUIRE(source.list_classes_in_module("src").size() == 1);
}
```

- [ ] **Step 2: Replace DB runtime state**

In `megacity_host.h`, replace:

```cpp
CityDatabase city_db_;
bool city_db_reconciled_ = false;
```

with:

```cpp
std::unique_ptr<TreeSitterSemanticSource> treesitter_source_;
std::shared_ptr<const CodebaseSnapshot> applied_treesitter_snapshot_;
bool treesitter_source_ready_ = false;
```

- [ ] **Step 3: Simplify Tree-sitter source start/stop**

In `start_tree_sitter_semantic_source()`:

```cpp
graphify_source_.reset();
treesitter_source_.reset();
applied_treesitter_snapshot_.reset();
treesitter_source_ready_ = false;
refresh_available_modules();

if (!scanner_started_)
{
    scanner_.start(scan_root_);
    scanner_started_ = true;
    scan_start_time_ = std::chrono::steady_clock::now();
}
```

In `stop_tree_sitter_semantic_source()`:

```cpp
if (scanner_started_)
{
    scanner_.stop();
    scanner_started_ = false;
}
treesitter_source_.reset();
applied_treesitter_snapshot_.reset();
treesitter_source_ready_ = false;
```

- [ ] **Step 4: Update refresh_available_modules()**

Change Tree-sitter mode to:

```cpp
else if (treesitter_source_)
{
    available_modules_ = treesitter_source_->list_modules();
}
```

- [ ] **Step 5: Update rebuild_semantic_city()**

Change source selection to:

```cpp
ICitySemanticSource* semantic_source = treesitter_source_.get();
if (renderer_config_.code_source == MegaCityCodeSource::Graphify)
{
    if (!load_graphify_semantic_source())
        return;
    semantic_source = graphify_source_.get();
}
if (!semantic_source)
    return;
```

- [ ] **Step 6: Replace DB reconcile in pump()**

Replace the `city_db_.reconcile_snapshot(*snapshot)` block with:

```cpp
if (renderer_config_.code_source == MegaCityCodeSource::TreeSitterDb
    && !treesitter_source_ready_
    && scanner_started_)
{
    if (const auto snapshot = scanner_.snapshot(); snapshot && snapshot->complete)
    {
        const auto scan_end = std::chrono::steady_clock::now();
        const auto scan_ms = std::chrono::duration<double, std::milli>(scan_end - scan_start_time_).count();
        const auto semantic_start = std::chrono::steady_clock::now();
        treesitter_source_ = std::make_unique<TreeSitterSemanticSource>(*snapshot);
        applied_treesitter_snapshot_ = snapshot;
        treesitter_source_ready_ = true;
        refresh_available_modules();
        const auto layout_start = std::chrono::steady_clock::now();
        rebuild_semantic_city();
        const auto layout_end = std::chrono::steady_clock::now();
        const auto semantic_ms = std::chrono::duration<double, std::milli>(layout_start - semantic_start).count();
        const auto layout_ms = std::chrono::duration<double, std::milli>(layout_end - layout_start).count();
        DRAXUL_LOG_INFO(LogCategory::App,
            "MegaCityHost: built Tree-sitter semantic source (%zu files, %zu modules)",
            snapshot->files.size(),
            available_modules_.size());
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "MegaCityHost: scan %.0fms, semantic source %.0fms, city layout %.0fms",
            scan_ms, semantic_ms, layout_ms);
    }
}
```

- [ ] **Step 7: Run host and scene tests, then commit**

Run:

```powershell
cmake --build build --config Release --target draxul draxul-tests
.\build\tests\Release\draxul-tests.exe "[megacity]"
```

Expected: MegaCity tests pass and no runtime path requires `city_db_.open()`.

Commit:

```powershell
git add modules/megacity/draxul-megacity tests/megacity_scene_tests.cpp
git commit -m "feat: build megacity directly from treesitter snapshot"
```

---

### Task 4: Retire DB Runtime Dependency From MegaCity

**Files:**
- Modify: `modules/megacity/draxul-megacity/CMakeLists.txt`
- Modify: `modules/megacity/draxul-megacity/src/city_semantic_source.h`
- Modify: `modules/megacity/draxul-megacity/src/city_semantic_source.cpp`
- Modify: `tests/megacity_scene_tests.cpp`
- Modify: `tests/citydb_tests.cpp`

- [ ] **Step 1: Remove DB-only adapter from MegaCity runtime**

If only tests still use `CityDatabaseSemanticSource`, move that adapter to a test helper or to `draxul-citydb` instead of keeping it in `draxul-megacity`.

- [ ] **Step 2: Remove `draxul-citydb` from `draxul-megacity` link libraries**

In `modules/megacity/draxul-megacity/CMakeLists.txt`, remove `draxul-citydb` from the runtime target link list. Keep `draxul-citydb` linked only to tests that explicitly exercise database persistence.

- [ ] **Step 3: Compile to catch accidental DB includes**

Run:

```powershell
cmake --build build --config Release --target draxul
```

Expected: build passes. If it fails because a production MegaCity file still includes `<draxul/citydb.h>`, replace that include with `city_semantic_records.h` for record structs, `city_semantic_source.h` for `ICitySemanticSource`, or `treesitter_semantic_source.h` for the direct Tree-sitter adapter.

- [ ] **Step 4: Commit**

```powershell
git add modules/megacity/draxul-megacity modules/megacity/draxul-citydb tests
git commit -m "refactor: remove citydb from megacity runtime path"
```

---

### Task 5: Update Docs And Final Verification

**Files:**
- Modify: `docs/features.md`
- Optionally modify: `AGENTS.md` if build/test guidance mentions DB-backed MegaCity assumptions

- [ ] **Step 1: Update feature docs**

In `docs/features.md`, update the MegaCity code source description to say:

```markdown
- **MegaCity code source modes**: The City Build UI can build the semantic city directly from a completed Tree-sitter scan or directly from a Graphify graph JSON. The Tree-sitter mode no longer writes a SQLite city snapshot before layout; SQLite citydb remains only for focused persistence/debug tests.
```

- [ ] **Step 2: Run full verification**

Run:

```powershell
py -m unittest tests/do_py_tests.py
cmake --build build --config Release --target draxul draxul-tests
py do.py smoke
ctest --test-dir build --build-config Release --output-on-failure
graphify update .
```

Expected:

- Python unittest exits 0.
- Release build exits 0.
- Smoke test exits 0.
- CTest reports `100% tests passed`.
- Graphify update exits 0.

- [ ] **Step 3: Commit docs**

```powershell
git add docs/features.md graphify-out
git commit -m "docs: document direct treesitter semantic city source"
```

---

## Self-Review

- Scope is limited to removing SQLite from the normal Tree-sitter-to-semantic-city path. It does not delete all citydb code in the first pass.
- The plan preserves Graphify mode and the existing `ICitySemanticSource` shape.
- The main risk is parity: `CityDatabase::reconcile_snapshot()` contains non-storage behavior. Task 2 explicitly ports and tests those rules.
- The config value `treesitter_db` can remain as a backward-compatible alias during this migration, even if the implementation no longer uses the DB.
