# Graphify Semantic City Source Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an optional MegaCity code source that loads `graphify-out/graph.json` and jumps directly into the existing semantic city builder without running Tree-sitter or reconciling SQLite.

**Architecture:** Keep Tree-sitter plus SQLite as the default high-fidelity C++ path. Introduce a semantic source interface in `draxul-megacity` so `build_city()` reads module/class/dependency records from either a `CityDatabase` adapter or a new `GraphifySemanticSource`. The first Graphify mode preserves the current class/method/field city metaphor: class-like symbols become buildings, methods become function layers, field/type/call/inheritance links become dependencies, and source paths determine modules with community names retained as labels/diagnostics.

**Tech Stack:** C++20, CMake, Catch2, `nlohmann_json` for Graphify JSON parsing, existing MegaCity semantic layout and render pipeline.

---

## Scope

Build only the first optional source path:

- Default behavior remains unchanged: `code_source = "treesitter_db"` scans with Tree-sitter and reconciles SQLite.
- New behavior: `code_source = "graphify"` loads `graphify_graph_path`, builds semantic records in memory, and rebuilds the city from those records.
- The Graphify source should work from the existing merged graph shape in `graphify-out/graph.json`.
- Do not remove `draxul-treesitter`, `draxul-citydb`, or existing DB-backed UI behavior.
- Do not add live Graphify extraction from inside Draxul; this plan only consumes an existing graph file.

---

## File Structure

- Create `modules/megacity/draxul-megacity/src/city_semantic_source.h`
  Defines `ICitySemanticSource` and `CityDatabaseSemanticSource`, the common record provider used by `build_city()`.

- Create `modules/megacity/draxul-megacity/src/city_semantic_source.cpp`
  Implements the `CityDatabase` adapter by forwarding to the existing DB query API.

- Modify `modules/megacity/draxul-megacity/src/city_builder.h` and `modules/megacity/draxul-megacity/src/city_builder.cpp`
  Change `build_city()` to consume `ICitySemanticSource&` instead of `CityDatabase&`.

- Create `modules/megacity/draxul-megacity/src/graphify_semantic_source.h` and `modules/megacity/draxul-megacity/src/graphify_semantic_source.cpp`
  Loads Graphify JSON, normalizes paths, infers class records and dependency records, and exposes them through `ICitySemanticSource`.

- Modify `modules/megacity/draxul-megacity/include/draxul/megacity_code_config.h` and `modules/megacity/draxul-megacity/src/megacity_code_config.cpp`
  Add `MegaCityCodeSource`, `code_source`, and `graphify_graph_path` config support.

- Modify `modules/megacity/draxul-megacity/include/draxul/megacity_host.h` and `modules/megacity/draxul-megacity/src/megacity_host.cpp`
  Select the semantic source at runtime, skip scanner/DB reconciliation in Graphify mode, and rebuild once the graph loads.

- Modify `modules/megacity/draxul-megacity/src/ui_treesitter_panel.cpp` and `modules/megacity/draxul-megacity/src/ui_treesitter_panel.h`
  Add a source selector and graph path field to the existing MegaCity diagnostics/config panel.

- Modify `modules/megacity/draxul-megacity/CMakeLists.txt` and `cmake/FetchDependencies.cmake`
  Add new source files and a JSON parser dependency.

- Create `tests/graphify_semantic_source_tests.cpp`
  Unit tests for Graphify path normalization, class/method/field mapping, call dependencies, and module records.

- Modify `tests/megacity_scene_tests.cpp`
  Update existing `build_city()` tests to pass a semantic source adapter instead of a raw `CityDatabase`.

- Modify `tests/app_config_tests.cpp`
  Verify config parse/save round trips for `code_source` and `graphify_graph_path`.

- Modify `docs/features.md`
  Document Graphify-backed MegaCity import as an optional feature.

---

### Task 1: Add Semantic Source Interface

**Files:**
- Create: `modules/megacity/draxul-megacity/src/city_semantic_source.h`
- Create: `modules/megacity/draxul-megacity/src/city_semantic_source.cpp`
- Modify: `modules/megacity/draxul-megacity/src/city_builder.h`
- Modify: `modules/megacity/draxul-megacity/src/city_builder.cpp`
- Modify: `modules/megacity/draxul-megacity/src/megacity_host.cpp`
- Modify: `modules/megacity/draxul-megacity/CMakeLists.txt`
- Modify: `tests/megacity_scene_tests.cpp`

- [ ] **Step 1: Write the failing city builder adapter test**

In `tests/megacity_scene_tests.cpp`, add a small fake source near the other MegaCity test helpers:

```cpp
#include "city_semantic_source.h"

namespace
{

class TestSemanticSource final : public draxul::ICitySemanticSource
{
public:
    std::vector<std::string> modules{ "app" };
    draxul::CityModuleRecord module;
    std::vector<draxul::CityClassRecord> rows;
    std::vector<draxul::CityDependencyRecord> deps;
    draxul::CodebaseHealthMetrics health;

    std::vector<std::string> list_modules() const override { return modules; }
    draxul::CityModuleRecord module_record(std::string_view) const override { return module; }
    std::vector<draxul::CityClassRecord> list_classes_in_module(std::string_view) const override { return rows; }
    std::vector<draxul::CityDependencyRecord> list_class_dependencies_in_module(std::string_view) const override { return deps; }
    draxul::CodebaseHealthMetrics codebase_health() const override { return health; }
};

} // namespace
```

Add a focused test:

```cpp
TEST_CASE("MegaCity build_city consumes a semantic source adapter", "[megacity]")
{
    SceneWorld world;
    TestSemanticSource source;
    source.module.module_path = "app";
    source.module.building_count = 1;
    source.module.quality = 0.8f;
    source.rows.push_back({
        "Widget",
        "app::Widget",
        "app",
        "app/widget.cpp",
        "building",
        false,
        2,
        1,
        { 7 },
        { "draw" },
        1,
        false,
    });

    MegaCityCodeConfig config;
    uint64_t sign_revision = 0;
    const CityBuildResult result = build_city(
        world, source, nullptr, source.list_modules(), config, sign_revision);

    REQUIRE(result.semantic_model);
    REQUIRE(result.semantic_model->modules.size() == 1);
    REQUIRE(result.semantic_model->modules[0].module_path == "app");
    REQUIRE(result.semantic_model->modules[0].buildings.size() == 1);
    REQUIRE(result.semantic_model->modules[0].buildings[0].qualified_name == "app::Widget");
}
```

- [ ] **Step 2: Run test build and verify it fails**

Run:

```powershell
cmake --build build --config Debug --target draxul-tests
```

Expected before implementation: compile failure because `city_semantic_source.h` and the `build_city(... ICitySemanticSource& ...)` signature do not exist.

- [ ] **Step 3: Create the semantic source interface**

Create `modules/megacity/draxul-megacity/src/city_semantic_source.h`:

```cpp
#pragma once

#include <draxul/citydb.h>

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

class CityDatabaseSemanticSource final : public ICitySemanticSource
{
public:
    explicit CityDatabaseSemanticSource(CityDatabase& db);

    [[nodiscard]] std::vector<std::string> list_modules() const override;
    [[nodiscard]] CityModuleRecord module_record(std::string_view module_path) const override;
    [[nodiscard]] std::vector<CityClassRecord> list_classes_in_module(std::string_view module_path) const override;
    [[nodiscard]] std::vector<CityDependencyRecord> list_class_dependencies_in_module(std::string_view module_path) const override;
    [[nodiscard]] CodebaseHealthMetrics codebase_health() const override;

private:
    CityDatabase& db_;
};

} // namespace draxul
```

Create `modules/megacity/draxul-megacity/src/city_semantic_source.cpp`:

```cpp
#include "city_semantic_source.h"

namespace draxul
{

CityDatabaseSemanticSource::CityDatabaseSemanticSource(CityDatabase& db)
    : db_(db)
{
}

std::vector<std::string> CityDatabaseSemanticSource::list_modules() const
{
    return db_.is_open() ? db_.list_modules() : std::vector<std::string>{};
}

CityModuleRecord CityDatabaseSemanticSource::module_record(std::string_view module_path) const
{
    return db_.module_record(module_path);
}

std::vector<CityClassRecord> CityDatabaseSemanticSource::list_classes_in_module(std::string_view module_path) const
{
    return db_.list_classes_in_module(module_path);
}

std::vector<CityDependencyRecord> CityDatabaseSemanticSource::list_class_dependencies_in_module(std::string_view module_path) const
{
    return db_.list_class_dependencies_in_module(module_path);
}

CodebaseHealthMetrics CityDatabaseSemanticSource::codebase_health() const
{
    return db_.codebase_health();
}

} // namespace draxul
```

- [ ] **Step 4: Wire the new file into CMake**

Add `src/city_semantic_source.cpp` to both `MEGACITY_SOURCES` lists in `modules/megacity/draxul-megacity/CMakeLists.txt`.

- [ ] **Step 5: Refactor `build_city()` to use the interface**

In `modules/megacity/draxul-megacity/src/city_builder.h`, forward declare `ICitySemanticSource` and change the signature:

```cpp
class ICitySemanticSource;

CityBuildResult build_city(
    SceneWorld& world,
    ICitySemanticSource& semantic_source,
    TextService* text_service,
    const std::vector<std::string>& available_modules,
    const MegaCityCodeConfig& config,
    uint64_t& sign_label_revision);
```

In `city_builder.cpp`, include `"city_semantic_source.h"` and replace direct DB calls:

```cpp
const CityModuleRecord mod_record = semantic_source.module_record(module_path);
modules.push_back({
    module_path,
    semantic_source.list_classes_in_module(module_path),
    semantic_source.list_class_dependencies_in_module(module_path),
    mod_record.quality,
    mod_record.health,
});
```

Replace:

```cpp
semantic_model->codebase_health = city_db.codebase_health();
```

with:

```cpp
semantic_model->codebase_health = semantic_source.codebase_health();
```

- [ ] **Step 6: Keep the host on the DB-backed source**

In `modules/megacity/draxul-megacity/src/megacity_host.cpp`, include `"city_semantic_source.h"` and change rebuild to:

```cpp
CityDatabaseSemanticSource semantic_source(city_db_);
available_modules_ = semantic_source.list_modules();
auto result = build_city(
    *world_, semantic_source, sign_text_service_.get(),
    available_modules_, renderer_config_, sign_label_revision_);
```

Leave scanner and reconciliation logic unchanged in this task.

- [ ] **Step 7: Run the focused test**

Run:

```powershell
cmake --build build --config Debug --target draxul-tests
.\build\tests\Debug\draxul-tests.exe "MegaCity build_city consumes a semantic source adapter"
```

Expected after implementation: the focused test passes.

- [ ] **Step 8: Commit**

Run:

```powershell
git add modules/megacity/draxul-megacity/src/city_semantic_source.h modules/megacity/draxul-megacity/src/city_semantic_source.cpp modules/megacity/draxul-megacity/src/city_builder.h modules/megacity/draxul-megacity/src/city_builder.cpp modules/megacity/draxul-megacity/src/megacity_host.cpp modules/megacity/draxul-megacity/CMakeLists.txt tests/megacity_scene_tests.cpp
git commit -m "refactor: abstract megacity semantic source"
```

---

### Task 2: Add JSON Dependency For Graphify Loading

**Files:**
- Modify: `cmake/FetchDependencies.cmake`
- Modify: `modules/megacity/draxul-megacity/CMakeLists.txt`

- [ ] **Step 1: Add dependency wiring**

In `cmake/FetchDependencies.cmake`, add after the `tomlplusplus` block:

```cmake
# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)
```

In `modules/megacity/draxul-megacity/CMakeLists.txt`, add `nlohmann_json::nlohmann_json` to the private link libraries:

```cmake
target_link_libraries(draxul-megacity PRIVATE
    draxul-geometry
    draxul-ui
    nlohmann_json::nlohmann_json
)
```

- [ ] **Step 2: Configure and build**

Run:

```powershell
cmake --preset default
cmake --build build --config Debug --target draxul-megacity
```

Expected: CMake configures and `draxul-megacity` builds with the new dependency available.

- [ ] **Step 3: Commit**

Run:

```powershell
git add cmake/FetchDependencies.cmake modules/megacity/draxul-megacity/CMakeLists.txt
git commit -m "build: add json parser for graphify import"
```

---

### Task 3: Implement Graphify Semantic Source Core Mapping

**Files:**
- Create: `modules/megacity/draxul-megacity/src/graphify_semantic_source.h`
- Create: `modules/megacity/draxul-megacity/src/graphify_semantic_source.cpp`
- Modify: `modules/megacity/draxul-megacity/CMakeLists.txt`
- Create: `tests/graphify_semantic_source_tests.cpp`

- [ ] **Step 1: Write the failing class/method/field mapping test**

Create `tests/graphify_semantic_source_tests.cpp`:

```cpp
#ifdef DRAXUL_ENABLE_MEGACITY

#include "graphify_semantic_source.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace
{

std::filesystem::path write_graphify_fixture(std::string_view name, std::string_view json)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / std::string(name);
    std::ofstream out(path, std::ios::binary);
    out << json;
    return path;
}

} // namespace

TEST_CASE("GraphifySemanticSource maps classes methods and fields into city records", "[megacity][graphify]")
{
    const std::filesystem::path path = write_graphify_fixture(
        "draxul-graphify-semantic-source-basic.json",
        R"json({
          "directed": false,
          "multigraph": false,
          "nodes": [
            { "id": "app::widget_file", "label": "widget.cpp", "file_type": "code", "source_file": "widget.cpp", "source_location": "L1", "community": 1, "community_name": "App Core" },
            { "id": "app::widget", "label": "Widget", "file_type": "code", "source_file": "widget.cpp", "source_location": "L10", "community": 1, "community_name": "App Core" },
            { "id": "app::widget_draw", "label": "draw()", "file_type": "code", "source_file": "widget.cpp", "source_location": "L21", "community": 1, "community_name": "App Core" },
            { "id": "app::widget_width", "label": "width_", "file_type": "code", "source_file": "widget.cpp", "source_location": "L15", "community": 1, "community_name": "App Core" },
            { "id": "app::renderer", "label": "Renderer", "file_type": "code", "source_file": "renderer.h", "source_location": "L7", "community": 1, "community_name": "App Core" }
          ],
          "links": [
            { "source": "app::widget_file", "target": "app::widget", "relation": "contains", "source_file": "widget.cpp", "source_location": "L10" },
            { "source": "app::widget", "target": "app::widget_draw", "relation": "method", "source_file": "widget.cpp", "source_location": "L21" },
            { "source": "app::widget", "target": "app::widget_width", "relation": "defines", "context": "field", "source_file": "widget.cpp", "source_location": "L15" },
            { "source": "app::widget", "target": "app::renderer", "relation": "references", "context": "field", "source_file": "widget.cpp", "source_location": "L16" }
          ]
        })json");

    draxul::GraphifySemanticSource source;
    REQUIRE(source.load(path));

    REQUIRE(source.list_modules() == std::vector<std::string>{ "app" });
    const auto rows = source.list_classes_in_module("app");
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].name == "Widget");
    CHECK(rows[0].qualified_name == "app::widget");
    CHECK(rows[0].module_path == "app");
    CHECK(rows[0].source_file_path == "app/widget.cpp");
    CHECK(rows[0].entity_kind == "building");
    CHECK(rows[0].base_size == 1);
    CHECK(rows[0].building_functions == 1);
    CHECK(rows[0].function_names == std::vector<std::string>{ "draw" });
    CHECK(rows[0].function_sizes == std::vector<int>{ 1 });
    CHECK(rows[0].road_size == 1);

    const auto deps = source.list_class_dependencies_in_module("app");
    REQUIRE(deps.size() == 1);
    CHECK(deps[0].source_qualified_name == "app::widget");
    CHECK(deps[0].field_type_name == "Renderer");
}

#endif
```

- [ ] **Step 2: Run test build and verify it fails**

Run:

```powershell
cmake --build build --config Debug --target draxul-tests
```

Expected before implementation: compile failure because `graphify_semantic_source.h` does not exist.

- [ ] **Step 3: Create the public test-facing header**

Create `modules/megacity/draxul-megacity/src/graphify_semantic_source.h`:

```cpp
#pragma once

#include "city_semantic_source.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace draxul
{

std::string normalize_graphify_source_file(std::string_view graph_id, std::string_view source_file);
std::string module_path_for_graphify_source(std::string_view graph_id, std::string_view normalized_source_file);
std::string clean_graphify_symbol_label(std::string_view label);

class GraphifySemanticSource final : public ICitySemanticSource
{
public:
    bool load(const std::filesystem::path& path);

    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] const std::string& last_error() const;

    [[nodiscard]] std::vector<std::string> list_modules() const override;
    [[nodiscard]] CityModuleRecord module_record(std::string_view module_path) const override;
    [[nodiscard]] std::vector<CityClassRecord> list_classes_in_module(std::string_view module_path) const override;
    [[nodiscard]] std::vector<CityDependencyRecord> list_class_dependencies_in_module(std::string_view module_path) const override;
    [[nodiscard]] CodebaseHealthMetrics codebase_health() const override;

private:
    std::filesystem::path path_;
    std::string last_error_;
    std::vector<std::string> modules_;
    std::unordered_map<std::string, CityModuleRecord> module_records_;
    std::unordered_map<std::string, std::vector<CityClassRecord>> rows_by_module_;
    std::unordered_map<std::string, std::vector<CityDependencyRecord>> deps_by_module_;
    CodebaseHealthMetrics codebase_health_;
};

} // namespace draxul
```

- [ ] **Step 4: Implement the first mapping pass**

In `graphify_semantic_source.cpp`, implement:

```cpp
#include "graphify_semantic_source.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <unordered_set>

namespace draxul
{
namespace
{

struct GraphifyNode
{
    std::string id;
    std::string label;
    std::string source_file;
    std::string source_location;
    int community = -1;
    std::string community_name;
};

struct GraphifyLink
{
    std::string source;
    std::string target;
    std::string relation;
    std::string context;
    std::string source_file;
    std::string source_location;
};

bool starts_with(std::string_view text, std::string_view prefix)
{
    return text.substr(0, prefix.size()) == prefix;
}

bool has_slash(std::string_view text)
{
    return text.find('/') != std::string_view::npos || text.find('\\') != std::string_view::npos;
}

std::string generic_slashes(std::string text)
{
    std::replace(text.begin(), text.end(), '\\', '/');
    return text;
}

std::string first_component(std::string_view path)
{
    const size_t slash = path.find('/');
    return slash == std::string_view::npos ? std::string(path) : std::string(path.substr(0, slash));
}

} // namespace

std::string normalize_graphify_source_file(std::string_view graph_id, std::string_view source_file)
{
    std::string path = generic_slashes(std::string(source_file));
    if (path.empty())
        return {};
    if (starts_with(graph_id, "app::") && !has_slash(path))
        return "app/" + path;
    if (starts_with(graph_id, "modules::") && starts_with(path, "megacity/"))
        return "modules/" + path;
    return path;
}

std::string module_path_for_graphify_source(std::string_view graph_id, std::string_view normalized_source_file)
{
    std::string path = generic_slashes(std::string(normalized_source_file));
    if (starts_with(path, "libs/"))
    {
        const size_t first = path.find('/');
        const size_t second = path.find('/', first + 1);
        return second == std::string::npos ? path : path.substr(0, second);
    }
    if (starts_with(path, "modules/megacity/"))
    {
        const size_t first = path.find('/');
        const size_t second = path.find('/', first + 1);
        const size_t third = path.find('/', second + 1);
        return third == std::string::npos ? path : path.substr(0, third);
    }
    if (starts_with(graph_id, "app::"))
        return "app";
    if (!path.empty())
        return first_component(path);
    return ".";
}

std::string clean_graphify_symbol_label(std::string_view label)
{
    std::string cleaned(label);
    while (!cleaned.empty() && cleaned.front() == '.')
        cleaned.erase(cleaned.begin());
    if (cleaned.size() >= 2 && cleaned.ends_with("()"))
        cleaned.resize(cleaned.size() - 2);
    return cleaned;
}

bool GraphifySemanticSource::load(const std::filesystem::path& path)
{
    path_ = path;
    last_error_.clear();
    modules_.clear();
    module_records_.clear();
    rows_by_module_.clear();
    deps_by_module_.clear();
    codebase_health_ = {};

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        last_error_ = "failed to open graphify graph";
        return false;
    }

    nlohmann::json root;
    try
    {
        in >> root;
    }
    catch (const std::exception& ex)
    {
        last_error_ = ex.what();
        return false;
    }

    std::unordered_map<std::string, GraphifyNode> nodes;
    for (const auto& item : root.value("nodes", nlohmann::json::array()))
    {
        GraphifyNode node;
        node.id = item.value("id", "");
        node.label = item.value("label", "");
        node.source_file = normalize_graphify_source_file(node.id, item.value("source_file", ""));
        node.source_location = item.value("source_location", "");
        node.community = item.value("community", -1);
        node.community_name = item.value("community_name", "");
        if (!node.id.empty())
            nodes.emplace(node.id, std::move(node));
    }

    std::vector<GraphifyLink> links;
    for (const auto& item : root.value("links", nlohmann::json::array()))
    {
        links.push_back({
            item.value("source", ""),
            item.value("target", ""),
            item.value("relation", ""),
            item.value("context", ""),
            normalize_graphify_source_file(item.value("source", ""), item.value("source_file", "")),
            item.value("source_location", ""),
        });
    }

    std::unordered_set<std::string> class_ids;
    for (const auto& link : links)
    {
        if (link.relation == "method"
            || link.relation == "inherits"
            || (link.relation == "defines" && link.context == "field")
            || (link.relation == "references" && link.context == "field"))
            class_ids.insert(link.source);
    }

    std::unordered_map<std::string, std::string> method_owner;
    for (const auto& link : links)
    {
        if (link.relation == "method")
            method_owner[link.target] = link.source;
    }

    for (const std::string& class_id : class_ids)
    {
        const auto node_it = nodes.find(class_id);
        if (node_it == nodes.end())
            continue;
        const GraphifyNode& node = node_it->second;
        const std::string module = module_path_for_graphify_source(node.id, node.source_file);

        CityClassRecord row;
        row.name = clean_graphify_symbol_label(node.label);
        row.qualified_name = node.id;
        row.module_path = module;
        row.source_file_path = node.source_file;
        row.entity_kind = "building";

        for (const auto& link : links)
        {
            if (link.source != class_id)
                continue;
            if (link.relation == "method")
            {
                const auto target = nodes.find(link.target);
                row.function_names.push_back(target == nodes.end() ? link.target : clean_graphify_symbol_label(target->second.label));
                row.function_sizes.push_back(1);
            }
            else if (link.relation == "defines" && link.context == "field")
            {
                ++row.base_size;
            }
            else if ((link.relation == "references" && link.context == "field") || link.relation == "inherits")
            {
                ++row.road_size;
            }
        }

        row.building_functions = static_cast<int>(row.function_names.size());
        rows_by_module_[module].push_back(std::move(row));
    }

    for (const auto& link : links)
    {
        std::string source_class = link.source;
        std::string target_class = link.target;
        if (link.relation == "calls")
        {
            if (const auto it = method_owner.find(link.source); it != method_owner.end())
                source_class = it->second;
            if (const auto it = method_owner.find(link.target); it != method_owner.end())
                target_class = it->second;
            if (source_class == link.source && target_class == link.target)
                continue;
        }
        else if (!(link.relation == "inherits" || (link.relation == "references" && link.context == "field")))
        {
            continue;
        }

        const auto src = nodes.find(source_class);
        const auto dst = nodes.find(target_class);
        if (src == nodes.end())
            continue;

        CityDependencyRecord dep;
        dep.source_qualified_name = source_class;
        dep.source_module_path = module_path_for_graphify_source(src->second.id, src->second.source_file);
        dep.source_file_path = src->second.source_file;
        dep.target_qualified_name = target_class;
        dep.target_module_path = dst == nodes.end() ? dep.source_module_path : module_path_for_graphify_source(dst->second.id, dst->second.source_file);
        dep.target_file_path = dst == nodes.end() ? std::string{} : dst->second.source_file;
        dep.field_name = link.context == "field" ? clean_graphify_symbol_label(link.target) : std::string{};
        dep.field_type_name = dst == nodes.end() ? link.target : clean_graphify_symbol_label(dst->second.label);
        dep.is_abstract_ref = link.relation == "inherits";
        deps_by_module_[dep.source_module_path].push_back(std::move(dep));
    }

    std::set<std::string> ordered_modules;
    for (const auto& [module, rows] : rows_by_module_)
    {
        ordered_modules.insert(module);
        CityModuleRecord record;
        record.module_path = module;
        record.building_count = static_cast<int>(rows.size());
        for (const auto& row : rows)
        {
            record.total_functions += row.building_functions;
            for (const int size : row.function_sizes)
                record.total_function_lines += size;
        }
        record.avg_function_size = record.total_functions > 0
            ? static_cast<float>(record.total_function_lines) / static_cast<float>(record.total_functions)
            : 0.0f;
        record.quality = 0.5f;
        record.health = { 0.5f, 0.5f, 0.5f };
        module_records_[module] = record;
    }
    modules_.assign(ordered_modules.begin(), ordered_modules.end());
    return true;
}

const std::filesystem::path& GraphifySemanticSource::path() const { return path_; }
const std::string& GraphifySemanticSource::last_error() const { return last_error_; }
std::vector<std::string> GraphifySemanticSource::list_modules() const { return modules_; }

CityModuleRecord GraphifySemanticSource::module_record(std::string_view module_path) const
{
    if (const auto it = module_records_.find(std::string(module_path)); it != module_records_.end())
        return it->second;
    CityModuleRecord record;
    record.module_path = std::string(module_path);
    return record;
}

std::vector<CityClassRecord> GraphifySemanticSource::list_classes_in_module(std::string_view module_path) const
{
    if (const auto it = rows_by_module_.find(std::string(module_path)); it != rows_by_module_.end())
        return it->second;
    return {};
}

std::vector<CityDependencyRecord> GraphifySemanticSource::list_class_dependencies_in_module(std::string_view module_path) const
{
    if (const auto it = deps_by_module_.find(std::string(module_path)); it != deps_by_module_.end())
        return it->second;
    return {};
}

CodebaseHealthMetrics GraphifySemanticSource::codebase_health() const
{
    return codebase_health_;
}

} // namespace draxul
```

- [ ] **Step 5: Add the new source file to CMake**

Add `src/graphify_semantic_source.cpp` to both `MEGACITY_SOURCES` lists in `modules/megacity/draxul-megacity/CMakeLists.txt`.

- [ ] **Step 6: Run focused tests**

Run:

```powershell
cmake --build build --config Debug --target draxul-tests
.\build\tests\Debug\draxul-tests.exe "[graphify]"
```

Expected after implementation: the Graphify semantic source test passes.

- [ ] **Step 7: Commit**

Run:

```powershell
git add modules/megacity/draxul-megacity/src/graphify_semantic_source.h modules/megacity/draxul-megacity/src/graphify_semantic_source.cpp modules/megacity/draxul-megacity/CMakeLists.txt tests/graphify_semantic_source_tests.cpp
git commit -m "feat: map graphify graph to megacity semantic records"
```

---

### Task 4: Cover Path Normalization And Call Dependencies

**Files:**
- Modify: `tests/graphify_semantic_source_tests.cpp`
- Modify: `modules/megacity/draxul-megacity/src/graphify_semantic_source.cpp`

- [ ] **Step 1: Add path normalization tests**

Append tests:

```cpp
TEST_CASE("GraphifySemanticSource normalizes merged graph paths", "[megacity][graphify]")
{
    CHECK(draxul::normalize_graphify_source_file("app::main", "main.cpp") == "app/main.cpp");
    CHECK(draxul::normalize_graphify_source_file("modules::src_citydb", "megacity/draxul-citydb/src/citydb.cpp")
        == "modules/megacity/draxul-citydb/src/citydb.cpp");
    CHECK(draxul::normalize_graphify_source_file("::src_grid", "libs/draxul-grid/src/grid.cpp")
        == "libs/draxul-grid/src/grid.cpp");

    CHECK(draxul::module_path_for_graphify_source("app::main", "app/main.cpp") == "app");
    CHECK(draxul::module_path_for_graphify_source("modules::src_citydb", "modules/megacity/draxul-citydb/src/citydb.cpp")
        == "modules/megacity/draxul-citydb");
    CHECK(draxul::module_path_for_graphify_source("::src_grid", "libs/draxul-grid/src/grid.cpp")
        == "libs/draxul-grid");
}
```

- [ ] **Step 2: Add call dependency test**

Append:

```cpp
TEST_CASE("GraphifySemanticSource maps method calls to owning class dependencies", "[megacity][graphify]")
{
    const std::filesystem::path path = write_graphify_fixture(
        "draxul-graphify-semantic-source-calls.json",
        R"json({
          "nodes": [
            { "id": "app::controller", "label": "Controller", "source_file": "controller.cpp", "source_location": "L3" },
            { "id": "app::controller_run", "label": "run()", "source_file": "controller.cpp", "source_location": "L10" },
            { "id": "app::service", "label": "Service", "source_file": "service.cpp", "source_location": "L3" },
            { "id": "app::service_start", "label": "start()", "source_file": "service.cpp", "source_location": "L10" }
          ],
          "links": [
            { "source": "app::controller", "target": "app::controller_run", "relation": "method" },
            { "source": "app::service", "target": "app::service_start", "relation": "method" },
            { "source": "app::controller_run", "target": "app::service_start", "relation": "calls", "context": "call" }
          ]
        })json");

    draxul::GraphifySemanticSource source;
    REQUIRE(source.load(path));

    const auto deps = source.list_class_dependencies_in_module("app");
    REQUIRE(deps.size() == 1);
    CHECK(deps[0].source_qualified_name == "app::controller");
    CHECK(deps[0].target_qualified_name == "app::service");
    CHECK(deps[0].source_module_path == "app");
    CHECK(deps[0].target_module_path == "app");
}
```

- [ ] **Step 3: Run and confirm the new tests**

Run:

```powershell
cmake --build build --config Debug --target draxul-tests
.\build\tests\Debug\draxul-tests.exe "[graphify]"
```

Expected: all Graphify source tests pass.

- [ ] **Step 4: Commit**

Run:

```powershell
git add tests/graphify_semantic_source_tests.cpp modules/megacity/draxul-megacity/src/graphify_semantic_source.cpp
git commit -m "test: cover graphify path and call mapping"
```

---

### Task 5: Add Config For Selecting Graphify Mode

**Files:**
- Modify: `modules/megacity/draxul-megacity/include/draxul/megacity_code_config.h`
- Modify: `modules/megacity/draxul-megacity/src/megacity_code_config.cpp`
- Modify: `tests/app_config_tests.cpp`

- [ ] **Step 1: Add failing config round-trip test**

In `tests/app_config_tests.cpp`, add near existing `[mega_city_code]` tests:

```cpp
#ifdef DRAXUL_ENABLE_MEGACITY
TEST_CASE("MegaCity code source config supports graphify", "[config][megacity]")
{
    ConfigDocument document;
    auto parsed = toml_support::parse_document(
        "[mega_city_code]\n"
        "code_source = \"graphify\"\n"
        "graphify_graph_path = \"graphify-out/graph.json\"\n");
    REQUIRE(parsed.has_value());
    document.root() = *parsed;

    const MegaCityCodeConfig defaults;
    const MegaCityCodeConfig loaded = load_megacity_code_config(document, defaults);

    CHECK(loaded.code_source == MegaCityCodeSource::Graphify);
    CHECK(loaded.graphify_graph_path == "graphify-out/graph.json");

    ConfigDocument saved;
    store_megacity_code_config(saved, loaded, defaults);
    const std::string text = saved.serialize();
    CHECK(text.find("code_source = \"graphify\"") != std::string::npos);
    CHECK(text.find("graphify_graph_path = \"graphify-out/graph.json\"") != std::string::npos);
}
#endif
```

- [ ] **Step 2: Run test build and verify it fails**

Run:

```powershell
cmake --build build --config Debug --target draxul-tests
```

Expected before implementation: compile failure because `MegaCityCodeSource` and fields do not exist.

- [ ] **Step 3: Add config fields and parser helpers**

In `megacity_code_config.h`, add:

```cpp
enum class MegaCityCodeSource : uint8_t
{
    TreeSitterDb,
    Graphify,
};
```

Add fields to `MegaCityCodeConfig`:

```cpp
MegaCityCodeSource code_source = MegaCityCodeSource::TreeSitterDb;
std::string graphify_graph_path = "graphify-out/graph.json";
```

In `megacity_code_config.cpp`, add string conversion helpers:

```cpp
MegaCityCodeSource code_source_from_string(std::string_view text)
{
    if (text == "graphify")
        return MegaCityCodeSource::Graphify;
    return MegaCityCodeSource::TreeSitterDb;
}

std::string_view code_source_to_string(MegaCityCodeSource source)
{
    switch (source)
    {
    case MegaCityCodeSource::Graphify:
        return "graphify";
    case MegaCityCodeSource::TreeSitterDb:
    default:
        return "treesitter_db";
    }
}
```

In `apply_megacity_code_table()`:

```cpp
if (auto code_source = toml_support::get_string(table, "code_source"); code_source.has_value())
    config.code_source = code_source_from_string(*code_source);
if (auto graphify_graph_path = toml_support::get_string(table, "graphify_graph_path"); graphify_graph_path.has_value())
    config.graphify_graph_path = *graphify_graph_path;
```

In `serialize_megacity_code_table()`:

```cpp
table.insert_or_assign("code_source", std::string(code_source_to_string(config.code_source)));
table.insert_or_assign("graphify_graph_path", config.graphify_graph_path);
```

- [ ] **Step 4: Run focused config test**

Run:

```powershell
cmake --build build --config Debug --target draxul-tests
.\build\tests\Debug\draxul-tests.exe "MegaCity code source config supports graphify"
```

Expected: the focused config test passes.

- [ ] **Step 5: Commit**

Run:

```powershell
git add modules/megacity/draxul-megacity/include/draxul/megacity_code_config.h modules/megacity/draxul-megacity/src/megacity_code_config.cpp tests/app_config_tests.cpp
git commit -m "feat: configure megacity graphify source"
```

---

### Task 6: Wire Graphify Source Into MegaCityHost

**Files:**
- Modify: `modules/megacity/draxul-megacity/include/draxul/megacity_host.h`
- Modify: `modules/megacity/draxul-megacity/src/megacity_host.cpp`
- Modify: `tests/megacity_scene_tests.cpp`

- [ ] **Step 1: Add a host-level regression test**

In `tests/megacity_scene_tests.cpp`, add a test that initializes `MegaCityHost` with a config document containing Graphify mode and a tiny graph file. The file already uses `#define private public` for `MegaCityHost`, so inspect `semantic_model_` directly:

```cpp
TEST_CASE("MegaCityHost loads graphify source without tree-sitter database", "[megacity][graphify]")
{
    tests::TempDir temp("draxul-megacity-graphify-host");
    const std::filesystem::path graph_path = temp.path / "graph.json";
    {
        std::ofstream out(graph_path, std::ios::binary);
        out << R"json({
          "nodes": [
            { "id": "app::widget", "label": "Widget", "source_file": "widget.cpp", "source_location": "L10" },
            { "id": "app::widget_draw", "label": "draw()", "source_file": "widget.cpp", "source_location": "L21" }
          ],
          "links": [
            { "source": "app::widget", "target": "app::widget_draw", "relation": "method" }
          ]
        })json";
    }

    ConfigDocument document;
    auto parsed = toml_support::parse_document(
        "[mega_city_code]\n"
        "code_source = \"graphify\"\n"
        "graphify_graph_path = \"" + graph_path.generic_string() + "\"\n");
    REQUIRE(parsed.has_value());
    document.root() = *parsed;

    tests::FakeWindow window;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    tests::FakeTermRenderer renderer;
    MegaCityHost host;

    HostLaunchOptions launch;
    launch.kind = HostKind::MegaCity;

    HostViewport viewport;
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .config_document = &document,
        .launch_options = std::move(launch),
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));
    host.pump();

    REQUIRE(host.semantic_model_);
    CHECK(host.semantic_model_->building_count() == 1);
    CHECK(host.city_db_reconciled_);
    CHECK(host.scanner_.snapshot() == nullptr);

    host.shutdown();
}
```

- [ ] **Step 2: Run test build and verify it fails behaviorally**

Run:

```powershell
cmake --build build --config Debug --target draxul-tests
.\build\tests\Debug\draxul-tests.exe "MegaCityHost loads graphify source without tree-sitter database"
```

Expected before implementation: the test fails because the host still starts the scanner/DB path and never loads Graphify.

- [ ] **Step 3: Add host state for Graphify source**

In `megacity_host.h`, include or forward-declare `GraphifySemanticSource` and add:

```cpp
std::unique_ptr<GraphifySemanticSource> graphify_source_;
bool graphify_loaded_ = false;
```

If the header should avoid including the private source header, forward declare the class and include `<memory>`.

- [ ] **Step 4: Select source at initialize time**

In `MegaCityHost::initialize()`:

```cpp
if (renderer_config_.code_source == MegaCityCodeSource::Graphify)
{
    city_db_reconciled_ = true;
    graphify_loaded_ = false;
    graphify_source_ = std::make_unique<GraphifySemanticSource>();
    std::filesystem::path graph_path = renderer_config_.graphify_graph_path;
    if (graph_path.is_relative())
        graph_path = std::filesystem::path(DRAXUL_REPO_ROOT) / graph_path;
    if (graphify_source_->load(graph_path))
    {
        graphify_loaded_ = true;
        available_modules_ = graphify_source_->list_modules();
        rebuild_semantic_city();
    }
    else
    {
        DRAXUL_LOG_WARN(LogCategory::App, "MegaCityHost: failed to load Graphify graph %s: %s",
            graph_path.string().c_str(), graphify_source_->last_error().c_str());
    }
}
else
{
    const std::filesystem::path city_db_path = megacity_db_path();
    if (!city_db_.open(city_db_path))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "MegaCityHost: failed to open city DB at %s: %s",
            city_db_path.string().c_str(), city_db_.last_error().c_str());
    }
    if (city_db_.schema_migrated())
        restore_camera_after_initial_build_ = false;
    refresh_available_modules();
    scanner_.start(scan_root_);
    scan_start_time_ = std::chrono::steady_clock::now();
}
```

Keep route worker startup and rendering setup outside that branch.

- [ ] **Step 5: Route `refresh_available_modules()` and `rebuild_semantic_city()` by source**

Update `refresh_available_modules()`:

```cpp
available_modules_.clear();
if (renderer_config_.code_source == MegaCityCodeSource::Graphify)
{
    if (graphify_source_)
        available_modules_ = graphify_source_->list_modules();
    return;
}
if (city_db_.is_open())
    available_modules_ = city_db_.list_modules();
```

Update `rebuild_semantic_city()`:

```cpp
if (renderer_config_.code_source == MegaCityCodeSource::Graphify)
{
    if (!graphify_source_)
        return;
    auto result = build_city(
        *world_, *graphify_source_, sign_text_service_.get(),
        available_modules_, renderer_config_, sign_label_revision_);
    // keep the existing result application block unchanged
}
else
{
    CityDatabaseSemanticSource semantic_source(city_db_);
    auto result = build_city(
        *world_, semantic_source, sign_text_service_.get(),
        available_modules_, renderer_config_, sign_label_revision_);
    // keep the existing result application block unchanged
}
```

Extract the common result application body into a private helper if keeping it inline would duplicate more than a few lines.

- [ ] **Step 6: Skip Tree-sitter reconciliation in Graphify mode**

At the start of the reconciliation block in `pump()`:

```cpp
if (renderer_config_.code_source != MegaCityCodeSource::Graphify
    && !city_db_reconciled_
    && city_db_.is_open())
{
    ...
}
```

In `shutdown()`, only call `scanner_.stop()` and `city_db_.close()` when the source is `TreeSitterDb`; reset `graphify_source_` in all modes.

- [ ] **Step 7: Run focused host test**

Run:

```powershell
cmake --build build --config Debug --target draxul-tests
.\build\tests\Debug\draxul-tests.exe "MegaCityHost loads graphify source without tree-sitter database"
```

Expected: the test passes and does not require a SQLite DB.

- [ ] **Step 8: Commit**

Run:

```powershell
git add modules/megacity/draxul-megacity/include/draxul/megacity_host.h modules/megacity/draxul-megacity/src/megacity_host.cpp tests/megacity_scene_tests.cpp
git commit -m "feat: load megacity from graphify graph"
```

---

### Task 7: Add Minimal UI Controls

**Files:**
- Modify: `modules/megacity/draxul-megacity/src/ui_treesitter_panel.h`
- Modify: `modules/megacity/draxul-megacity/src/ui_treesitter_panel.cpp`
- Modify: `tests/megacity_scene_tests.cpp`

- [ ] **Step 1: Add UI config plumbing test**

Do not add a UI interaction test in this task. The existing ImGui panel code is not directly driven by current tests, and `tests/app_config_tests.cpp` plus the host Graphify-mode test cover the persistent behavior. This task uses compile coverage for the immediate-mode UI wiring.

- [ ] **Step 2: Add source selector**

In `ui_treesitter_panel.cpp`, near the module selector, add:

```cpp
const char* source_items[] = { "Tree-sitter DB", "Graphify" };
int source_index = config.code_source == MegaCityCodeSource::Graphify ? 1 : 0;
if (ImGui::Combo("Code Source", &source_index, source_items, IM_ARRAYSIZE(source_items)))
{
    config.code_source = source_index == 1 ? MegaCityCodeSource::Graphify : MegaCityCodeSource::TreeSitterDb;
    changed = true;
}
```

When `config.code_source == MegaCityCodeSource::Graphify`, show a path input:

```cpp
std::array<char, 512> graph_path{};
std::strncpy(graph_path.data(), config.graphify_graph_path.c_str(), graph_path.size() - 1);
if (ImGui::InputText("Graphify Graph", graph_path.data(), graph_path.size()))
{
    config.graphify_graph_path = graph_path.data();
    changed = true;
}
```

- [ ] **Step 3: Ensure source changes rebuild the world**

In `world_rebuild_signature()` or `requires_world_rebuild()`, ensure `code_source` and `graphify_graph_path` are part of the compared config. The defaulted equality operator already includes fields if they are in `MegaCityCodeConfig`; only change this if the function strips them before comparison.

- [ ] **Step 4: Build tests**

Run:

```powershell
cmake --build build --config Debug --target draxul-tests
```

Expected: tests compile.

- [ ] **Step 5: Commit**

Run:

```powershell
git add modules/megacity/draxul-megacity/src/ui_treesitter_panel.h modules/megacity/draxul-megacity/src/ui_treesitter_panel.cpp tests/megacity_scene_tests.cpp
git commit -m "feat: expose megacity graphify source controls"
```

---

### Task 8: Document And Verify

**Files:**
- Modify: `docs/features.md`

- [ ] **Step 1: Update features docs**

Add a MegaCity bullet describing the optional Graphify import:

```markdown
- MegaCity can optionally build its semantic city from an existing Graphify `graphify-out/graph.json`, preserving the class/method/field city metaphor while bypassing Tree-sitter scanning and SQLite reconciliation.
```

- [ ] **Step 2: Run focused MegaCity tests**

Run:

```powershell
cmake --build build --config Debug --target draxul-tests
.\build\tests\Debug\draxul-tests.exe "[megacity]"
.\build\tests\Debug\draxul-tests.exe "[graphify]"
```

Expected: focused tests pass.

- [ ] **Step 3: Run required repo validation before final commit**

Run:

```powershell
cmake --build build --config Release --target draxul draxul-tests
py do.py smoke
```

Expected: build succeeds and smoke test passes. If `py` is not available, run `python do.py smoke`.

- [ ] **Step 4: Commit docs**

Run:

```powershell
git add docs/features.md
git commit -m "docs: document graphify megacity source"
```

---

## Review Checklist

- [ ] Tree-sitter/SQLite remains the default source.
- [ ] Graphify mode loads from `graphify_graph_path` and does not start scanner reconciliation.
- [ ] `build_city()` has one record-provider interface and no duplicated city layout logic.
- [ ] Graphify class-like entities become `CityClassRecord` rows with method/function and field/road metrics.
- [ ] Graphify call edges between methods become class-to-class `CityDependencyRecord` rows.
- [ ] Path normalization handles merged graphs from `libs`, `app`, and `modules` extractions.
- [ ] Config round-trips through `ConfigDocument`.
- [ ] `docs/features.md` mentions the optional source.
- [ ] Required build and smoke validation pass before merging.
