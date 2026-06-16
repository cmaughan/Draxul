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
