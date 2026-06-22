#include <catch2/catch_test_macros.hpp>

#ifdef DRAXUL_ENABLE_MEGACITY

#include <draxul/treesitter.h>
#include <draxul/treesitter_semantic_source.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace draxul
{
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

    SymbolRecord free_function;
    free_function.kind = SymbolKind::Function;
    free_function.name = "make_widget";
    free_function.line = 50;
    free_function.end_line = 55;

    file.symbols = { iface, concrete, method, free_function };
    snapshot.files.push_back(std::move(file));
    return snapshot;
}

std::shared_ptr<const CodebaseSnapshot> wait_for_complete_snapshot(
    CodebaseScanner& scanner,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(2000))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (const auto snapshot = scanner.snapshot(); snapshot && snapshot->complete)
            return snapshot;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return scanner.snapshot();
}

} // namespace

TEST_CASE("TreeSitterSemanticSource projects snapshot semantic records", "[megacity][treesitter]")
{
    const CodebaseSnapshot snapshot = make_semantic_fixture_snapshot();

    const TreeSitterSemanticSource source(snapshot);

    REQUIRE(source.list_modules() == std::vector<std::string>{ "src" });

    const CityModuleRecord module = source.module_record("src");
    CHECK(module.building_count == 2);
    CHECK(module.total_functions == 1);
    CHECK(module.total_function_lines == 10);
    CHECK(module.avg_function_size == 10.0f);
    CHECK(module.quality == module.health.complexity);
    CHECK(module.health.complexity == 0.5f);
    CHECK(module.health.cohesion > 0.45f);
    CHECK(module.health.cohesion < 0.55f);
    CHECK(module.health.coupling > 0.85f);
    CHECK(module.health.coupling < 0.86f);

    const auto classes = source.list_classes_in_module("src");
    REQUIRE(classes.size() == 3);
    CHECK(classes[0].qualified_name == "IWidget");
    CHECK(classes[0].entity_kind == "tower");
    CHECK(classes[0].is_abstract);
    CHECK(classes[1].qualified_name == "Widget");
    CHECK(classes[1].entity_kind == "building");
    CHECK(classes[1].base_size == 1);
    CHECK(classes[1].building_functions == 1);
    REQUIRE(classes[1].function_sizes.size() == 1);
    CHECK(classes[1].function_sizes[0] == 10);
    REQUIRE(classes[1].function_names.size() == 1);
    CHECK(classes[1].function_names[0] == "draw");
    CHECK(classes[1].road_size == 1);
    CHECK(classes[2].qualified_name == "make_widget");
    CHECK(classes[2].entity_kind == "tree");
    CHECK(classes[2].building_functions == 1);
    REQUIRE(classes[2].function_sizes.size() == 1);
    CHECK(classes[2].function_sizes[0] == 5);

    const auto dependencies = source.list_class_dependencies_in_module("src");
    REQUIRE(dependencies.size() == 1);
    CHECK(dependencies[0].source_qualified_name == "Widget");
    CHECK(dependencies[0].source_module_path == "src");
    CHECK(dependencies[0].field_name == "owner");
    CHECK(dependencies[0].field_type_name == "IWidget*");
    CHECK(dependencies[0].target_qualified_name == "IWidget");
    CHECK(dependencies[0].target_module_path == "src");
    CHECK(dependencies[0].source_file_path == "src/app/widget.cpp");
    CHECK(dependencies[0].target_file_path == "src/app/widget.cpp");
    CHECK(dependencies[0].is_abstract_ref);

    CHECK(source.codebase_health().complexity == module.health.complexity);
    CHECK(source.codebase_health().cohesion == module.health.cohesion);
    CHECK(source.codebase_health().coupling == module.health.coupling);
}

TEST_CASE("TreeSitterSemanticSource groups files by repository module boundary", "[megacity][treesitter]")
{
    CodebaseSnapshot snapshot;
    snapshot.complete = true;

    auto add_class_file = [&](std::string path, std::string name) {
        ParsedFile file;
        file.path = std::move(path);

        SymbolRecord symbol;
        symbol.kind = SymbolKind::Class;
        symbol.name = std::move(name);
        symbol.line = 1;
        symbol.end_line = 4;

        file.symbols.push_back(std::move(symbol));
        snapshot.files.push_back(std::move(file));
    };

    add_class_file("main.cpp", "RootMain");
    add_class_file("app/main.cpp", "AppMain");
    add_class_file("libs/draxul-grid/src/grid.cpp", "Grid");
    add_class_file("modules/markdown/draxul-markdown/src/markdown_document.cpp", "MarkdownDocument");
    add_class_file("modules/kanban/draxul-kanban/src/kanban_board.cpp", "KanbanBoard");
    add_class_file("modules/megacity/draxul-citymodel/src/treesitter_semantic_source.cpp", "CitySource");

    const TreeSitterSemanticSource source(snapshot);

    CHECK(source.list_modules() == std::vector<std::string>{
        ".",
        "app",
        "libs/draxul-grid",
        "modules/kanban",
        "modules/markdown",
        "modules/megacity",
    });
    CHECK(source.module_record("modules/markdown").building_count == 1);
    CHECK(source.module_record("modules/kanban").building_count == 1);
    CHECK(source.module_record("modules/megacity").building_count == 1);
}

TEST_CASE("TreeSitterSemanticSource does not cross-product nested type fields onto the parent class", "[megacity][treesitter]")
{
    const auto temp_root = std::filesystem::temp_directory_path() / "draxul-treesitter-nested-fields";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);

    const auto source_path = temp_root / "nested_fields.h";
    {
        std::ofstream out(source_path);
        REQUIRE(out.is_open());
        out << "class Foo {};\n";
        out << "class Bar {};\n";
        out << "class Outer {\n";
        out << "public:\n";
        out << "    struct Deps {\n";
        out << "        Foo* foo = nullptr;\n";
        out << "        Bar* bar = nullptr;\n";
        out << "    };\n";
        out << "private:\n";
        out << "    Deps deps_;\n";
        out << "    int count_;\n";
        out << "};\n";
    }

    CodebaseScanner scanner;
    scanner.start(temp_root);
    const auto snapshot = wait_for_complete_snapshot(scanner);
    scanner.stop();

    REQUIRE(snapshot);
    REQUIRE(snapshot->complete);

    const TreeSitterSemanticSource source(*snapshot);
    const std::vector<CityDependencyRecord> deps = source.list_class_dependencies_in_module("nested_fields.h");
    std::vector<CityDependencyRecord> outer_deps;
    for (const auto& dep : deps)
    {
        if (dep.source_qualified_name == "Outer")
            outer_deps.push_back(dep);
    }

    for (const auto& dep : outer_deps)
    {
        CHECK(dep.field_name != "foo");
        CHECK(dep.field_name != "bar");
        CHECK(dep.target_qualified_name != "Foo");
        CHECK(dep.target_qualified_name != "Bar");
    }

    std::filesystem::remove_all(temp_root);
}

TEST_CASE("TreeSitterSemanticSource expands interface-typed field dependencies across the inheritance graph", "[megacity][treesitter]")
{
    CodebaseSnapshot snapshot;
    snapshot.complete = true;
    snapshot.scan_time = std::chrono::steady_clock::now();

    ParsedFile file;
    file.path = "src/render.cpp";

    SymbolRecord irenderer{
        SymbolKind::Class,
        "IRenderer",
        "",
        true,
        10,
        12,
        0,
        {},
        {},
    };

    SymbolRecord vk_render_device{
        SymbolKind::Class,
        "VkRenderDevice",
        "",
        false,
        20,
        30,
        0,
        { "IRenderer" },
        {},
    };
    vk_render_device.inherited_types = { "IRenderer" };

    SymbolRecord app{
        SymbolKind::Class,
        "App",
        "",
        false,
        40,
        60,
        1,
        { "IRenderer" },
        {
            { "renderer_", "IRenderer", { "IRenderer" } },
        },
    };

    file.symbols = { irenderer, vk_render_device, app };
    snapshot.files.push_back(file);

    const TreeSitterSemanticSource source(snapshot);

    const std::vector<CityDependencyRecord> deps = source.list_class_dependencies_in_module("src");
    REQUIRE(deps.size() == 2);
    CHECK(deps[0].source_qualified_name == "App");
    CHECK(deps[0].field_name == "renderer_");
    CHECK(deps[0].target_qualified_name == "IRenderer");
    CHECK(deps[1].source_qualified_name == "App");
    CHECK(deps[1].field_name == "renderer_");
    CHECK(deps[1].target_qualified_name == "VkRenderDevice");

    const auto classes = source.list_classes_in_module("src");
    const auto app_it = std::find_if(classes.begin(), classes.end(), [](const CityClassRecord& row) {
        return row.qualified_name == "App";
    });
    REQUIRE(app_it != classes.end());
    CHECK(app_it->road_size == 2);
}

} // namespace draxul

#endif
