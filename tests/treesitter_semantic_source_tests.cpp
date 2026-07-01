#include <catch2/catch_test_macros.hpp>

#ifdef DRAXUL_ENABLE_MEGACITY

#include "city_builder.h"

#include <draxul/code_semantic_model.h>
#include <draxul/treesitter.h>

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

std::vector<std::string> semantic_module_paths(const CodeSemanticSnapshot& semantics)
{
    std::vector<std::string> modules;
    for (const CodeSemanticNode& node : semantics.nodes)
    {
        if (node.kind == CodeSemanticNodeKind::Module)
            modules.push_back(node.module_path);
    }
    std::sort(modules.begin(), modules.end());
    modules.erase(std::unique(modules.begin(), modules.end()), modules.end());
    return modules;
}

const SemanticCityBuilding* find_building(
    const SemanticMegacityModel& model,
    std::string_view qualified_name)
{
    for (const SemanticCityModuleModel& module : model.modules)
    {
        for (const SemanticCityBuilding& building : module.buildings)
        {
            if (building.qualified_name == qualified_name)
                return &building;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("Code semantic snapshot feeds the city presentation model", "[megacity][treesitter]")
{
    const CodebaseSnapshot snapshot = make_semantic_fixture_snapshot();

    const CodeSemanticSnapshot semantics = build_code_semantic_snapshot(snapshot);
    CHECK(semantics.complete);
    REQUIRE(semantic_module_paths(semantics) == std::vector<std::string>{ "src" });

    MegaCityCodeConfig config;
    const SemanticCodeModelBuildResult city = build_semantic_code_model(semantics, config);
    REQUIRE(city.semantic_model);
    REQUIRE(city.semantic_model->modules.size() == 1);
    const SemanticCityModuleModel& module = city.semantic_model->modules[0];
    CHECK(module.module_path == "src");
    CHECK(module.quality == module.health.complexity);
    CHECK(module.health.complexity == 0.5f);
    CHECK(module.health.cohesion > 0.45f);
    CHECK(module.health.cohesion < 0.55f);
    CHECK(module.health.coupling > 0.85f);
    CHECK(module.health.coupling < 0.86f);

    const SemanticCityBuilding* widget = find_building(*city.semantic_model, "Widget");
    REQUIRE(widget);
    CHECK(widget->base_size == 1);
    CHECK(widget->function_count == 1);
    REQUIRE(widget->layers.size() == 1);
    CHECK(widget->layers[0].function_name == "draw");
    CHECK(widget->layers[0].function_size == 10);
    CHECK(widget->road_size == 1);

    const SemanticCityBuilding* functions = find_building(*city.semantic_model, "Functions");
    REQUIRE(functions);
    CHECK(functions->is_free_function);
    CHECK(functions->function_count == 1);
    REQUIRE(functions->layers.size() == 1);
    CHECK(functions->layers[0].function_name == "make_widget");
    CHECK(functions->layers[0].function_size == 5);

    const auto& dependencies = city.semantic_model->dependencies;
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

    CHECK(city.semantic_model->codebase_health.complexity == module.health.complexity);
    CHECK(city.semantic_model->codebase_health.cohesion == module.health.cohesion);
    CHECK(city.semantic_model->codebase_health.coupling == module.health.coupling);
}

TEST_CASE("CodeSemanticSnapshot keeps methods fields and relationships independent of city concepts", "[megacity][treesitter]")
{
    const CodebaseSnapshot snapshot = make_semantic_fixture_snapshot();

    const CodeSemanticSnapshot semantics = build_code_semantic_snapshot(snapshot);

    auto count_nodes = [&](CodeSemanticNodeKind kind) {
        return static_cast<size_t>(std::count_if(
            semantics.nodes.begin(),
            semantics.nodes.end(),
            [kind](const CodeSemanticNode& node) { return node.kind == kind; }));
    };
    auto find_node = [&](CodeSemanticNodeKind kind, std::string_view qualified_name) -> const CodeSemanticNode* {
        const auto it = std::find_if(
            semantics.nodes.begin(),
            semantics.nodes.end(),
            [kind, qualified_name](const CodeSemanticNode& node) {
                return node.kind == kind && node.qualified_name == qualified_name;
            });
        return it == semantics.nodes.end() ? nullptr : &*it;
    };

    CHECK(semantics.complete);
    CHECK(count_nodes(CodeSemanticNodeKind::Repository) == 1);
    CHECK(count_nodes(CodeSemanticNodeKind::Module) == 1);
    CHECK(count_nodes(CodeSemanticNodeKind::File) == 1);
    CHECK(count_nodes(CodeSemanticNodeKind::Type) == 2);
    CHECK(count_nodes(CodeSemanticNodeKind::Method) == 1);
    CHECK(count_nodes(CodeSemanticNodeKind::Function) == 1);
    CHECK(count_nodes(CodeSemanticNodeKind::Field) == 1);

    const CodeSemanticNode* widget = find_node(CodeSemanticNodeKind::Type, "Widget");
    const CodeSemanticNode* draw = find_node(CodeSemanticNodeKind::Method, "Widget::draw");
    const CodeSemanticNode* owner = find_node(CodeSemanticNodeKind::Field, "Widget::owner");
    const CodeSemanticNode* iface = find_node(CodeSemanticNodeKind::Type, "IWidget");
    REQUIRE(widget);
    REQUIRE(draw);
    REQUIRE(owner);
    REQUIRE(iface);

    CHECK(widget->type_kind == CodeSemanticTypeKind::Class);
    CHECK(iface->type_kind == CodeSemanticTypeKind::Class);
    CHECK(draw->parent_id == widget->id);
    CHECK(owner->parent_id == widget->id);
    CHECK(widget->metrics.method_count == 1);
    CHECK(widget->metrics.field_count == 1);
    CHECK(semantics.indexes.node_index_by_id.contains(widget->id));
    CHECK(semantics.indexes.nodes_by_module.contains("src"));
    REQUIRE(semantics.indexes.nodes_by_parent.contains(widget->id));
    const auto& widget_children = semantics.indexes.nodes_by_parent.at(widget->id);
    CHECK(std::find(widget_children.begin(), widget_children.end(), draw->id) != widget_children.end());
    CHECK(std::find(widget_children.begin(), widget_children.end(), owner->id) != widget_children.end());
    CHECK(semantics.health.complexity > 0.0f);
    CHECK(semantics.health.cohesion > 0.0f);
    CHECK(semantics.health.coupling > 0.0f);

    const bool has_inherits_edge = std::any_of(
        semantics.edges.begin(),
        semantics.edges.end(),
        [&](const CodeSemanticEdge& edge) {
            return edge.kind == CodeSemanticEdgeKind::Inherits
                && edge.source_id == widget->id
                && edge.target_id == iface->id;
        });
    const bool has_reference_edge = std::any_of(
        semantics.edges.begin(),
        semantics.edges.end(),
        [&](const CodeSemanticEdge& edge) {
            return edge.kind == CodeSemanticEdgeKind::ReferencesType
                && edge.source_id == owner->id
                && edge.target_id == iface->id;
        });
    CHECK(has_inherits_edge);
    CHECK(has_reference_edge);

    const CodeSemanticSnapshot second = build_code_semantic_snapshot(snapshot);
    REQUIRE(second.nodes.size() == semantics.nodes.size());
    REQUIRE(second.edges.size() == semantics.edges.size());
    for (size_t i = 0; i < semantics.nodes.size(); ++i)
        CHECK(second.nodes[i].id == semantics.nodes[i].id);
    for (size_t i = 0; i < semantics.edges.size(); ++i)
        CHECK(second.edges[i].id == semantics.edges[i].id);
}

TEST_CASE("Code semantic snapshot groups files by repository module boundary", "[megacity][treesitter]")
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
    add_class_file("modules/megacity/draxul-code-semantics/src/code_semantic_model.cpp", "CitySource");

    const CodeSemanticSnapshot semantics = build_code_semantic_snapshot(snapshot);

    CHECK(semantic_module_paths(semantics) == std::vector<std::string>{
        ".",
        "app",
        "libs/draxul-grid",
        "modules/kanban",
        "modules/markdown",
        "modules/megacity",
    });

    MegaCityCodeConfig config;
    const SemanticCodeModelBuildResult city = build_semantic_code_model(semantics, config);
    REQUIRE(city.semantic_model);
    CHECK(find_building(*city.semantic_model, "MarkdownDocument"));
    CHECK(find_building(*city.semantic_model, "KanbanBoard"));
    CHECK(find_building(*city.semantic_model, "CitySource"));
}

TEST_CASE("City presentation does not cross-product nested type fields onto the parent class", "[megacity][treesitter]")
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

    const CodeSemanticSnapshot semantics = build_code_semantic_snapshot(*snapshot);
    MegaCityCodeConfig config;
    const SemanticCodeModelBuildResult city = build_semantic_code_model(semantics, config);
    REQUIRE(city.semantic_model);
    std::vector<SemanticCityDependency> outer_deps;
    for (const auto& dep : city.semantic_model->dependencies)
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

TEST_CASE("City presentation expands interface-typed field dependencies across the inheritance graph", "[megacity][treesitter]")
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

    const CodeSemanticSnapshot semantics = build_code_semantic_snapshot(snapshot);
    MegaCityCodeConfig config;
    const SemanticCodeModelBuildResult city = build_semantic_code_model(semantics, config);
    REQUIRE(city.semantic_model);

    const auto& deps = city.semantic_model->dependencies;
    REQUIRE(deps.size() == 2);
    CHECK(deps[0].source_qualified_name == "App");
    CHECK(deps[0].field_name == "renderer_");
    CHECK(deps[0].target_qualified_name == "IRenderer");
    CHECK(deps[1].source_qualified_name == "App");
    CHECK(deps[1].field_name == "renderer_");
    CHECK(deps[1].target_qualified_name == "VkRenderDevice");

    const auto app_it = std::find_if(
        city.semantic_model->modules[0].buildings.begin(),
        city.semantic_model->modules[0].buildings.end(),
        [](const SemanticCityBuilding& row) {
        return row.qualified_name == "App";
    });
    REQUIRE(app_it != city.semantic_model->modules[0].buildings.end());
    CHECK(app_it->road_size == 2);
}

} // namespace draxul

#endif
