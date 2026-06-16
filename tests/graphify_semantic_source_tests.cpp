#ifdef DRAXUL_ENABLE_MEGACITY

#include "graphify_semantic_source.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

TEST_CASE("GraphifySemanticSource normalizes merged graph paths", "[megacity][graphify]")
{
    CHECK(draxul::normalize_graphify_source_file("app::main", "main.cpp") == "app/main.cpp");
    CHECK(draxul::normalize_graphify_source_file("modules::src_citydb", "megacity/draxul-citydb/src/citydb.cpp")
        == "modules/megacity/draxul-citydb/src/citydb.cpp");
    CHECK(draxul::normalize_graphify_source_file("::src_grid", "libs/draxul-grid/src/grid.cpp")
        == "libs/draxul-grid/src/grid.cpp");

    CHECK(draxul::module_path_for_graphify_source("app::main", "app/main.cpp") == "app");
    CHECK(draxul::module_path_for_graphify_source(
              "modules::src_citydb", "modules/megacity/draxul-citydb/src/citydb.cpp")
        == "modules/megacity/draxul-citydb");
    CHECK(draxul::module_path_for_graphify_source("::src_grid", "libs/draxul-grid/src/grid.cpp")
        == "libs/draxul-grid");
}

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

TEST_CASE("GraphifySemanticSource maps inheritance edges to class dependencies", "[megacity][graphify]")
{
    const std::filesystem::path path = write_graphify_fixture(
        "draxul-graphify-semantic-source-inherits.json",
        R"json({
          "nodes": [
            { "id": "app::base", "label": "Base", "source_file": "base.h", "source_location": "L3" },
            { "id": "app::base_tick", "label": "tick()", "source_file": "base.h", "source_location": "L6" },
            { "id": "app::derived", "label": "Derived", "source_file": "derived.h", "source_location": "L3" }
          ],
          "links": [
            { "source": "app::base", "target": "app::base_tick", "relation": "method" },
            { "source": "app::derived", "target": "app::base", "relation": "inherits" }
          ]
        })json");

    draxul::GraphifySemanticSource source;
    REQUIRE(source.load(path));

    const auto rows = source.list_classes_in_module("app");
    REQUIRE(rows.size() == 2);
    const auto derived_it = std::find_if(rows.begin(), rows.end(), [](const draxul::CityClassRecord& row) {
        return row.qualified_name == "app::derived";
    });
    REQUIRE(derived_it != rows.end());
    CHECK(derived_it->road_size == 1);

    const auto deps = source.list_class_dependencies_in_module("app");
    REQUIRE(deps.size() == 1);
    CHECK(deps[0].source_qualified_name == "app::derived");
    CHECK(deps[0].target_qualified_name == "app::base");
    CHECK(deps[0].source_module_path == "app");
    CHECK(deps[0].target_module_path == "app");
    CHECK(deps[0].field_name.empty());
    CHECK(deps[0].field_type_name.empty());
    CHECK(deps[0].is_abstract_ref);
}

#endif
