#include <catch2/catch_test_macros.hpp>

#ifdef DRAXUL_ENABLE_MEGACITY

#include <draxul/citydb.h>
#include <draxul/treesitter.h>
#include <draxul/treesitter_semantic_source.h>

#include <filesystem>
#include <string>

namespace draxul
{
namespace
{

void remove_sqlite_artifacts(const std::filesystem::path& db_path)
{
    std::error_code ec;
    std::filesystem::remove(db_path, ec);
    REQUIRE(!ec);

    auto remove_sidecar = [&](const char* suffix) {
        std::filesystem::path sidecar = db_path;
        sidecar += suffix;

        std::error_code sidecar_ec;
        std::filesystem::remove(sidecar, sidecar_ec);
        REQUIRE(!sidecar_ec);
    };

    remove_sidecar("-wal");
    remove_sidecar("-shm");
}

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

} // namespace

TEST_CASE("TreeSitterSemanticSource matches CityDatabase semantic records", "[megacity][treesitter]")
{
    const CodebaseSnapshot snapshot = make_semantic_fixture_snapshot();
    const auto db_path = std::filesystem::temp_directory_path() / "draxul-treesitter-semantic-parity.sqlite3";
    remove_sqlite_artifacts(db_path);

    CityDatabase db;
    REQUIRE(db.open(db_path));
    REQUIRE(db.reconcile_snapshot(snapshot));

    const TreeSitterSemanticSource direct_source(snapshot);

    REQUIRE(direct_source.list_modules() == db.list_modules());

    const CityModuleRecord direct_module = direct_source.module_record("src");
    const CityModuleRecord db_module = db.module_record("src");
    CHECK(direct_module.building_count == db_module.building_count);
    CHECK(direct_module.total_functions == db_module.total_functions);
    CHECK(direct_module.total_function_lines == db_module.total_function_lines);
    CHECK(direct_module.avg_function_size == db_module.avg_function_size);
    CHECK(direct_module.quality == db_module.quality);
    CHECK(direct_module.health.complexity == db_module.health.complexity);
    CHECK(direct_module.health.cohesion == db_module.health.cohesion);
    CHECK(direct_module.health.coupling == db_module.health.coupling);

    const auto direct_classes = direct_source.list_classes_in_module("src");
    const auto db_classes = db.list_classes_in_module("src");
    REQUIRE(direct_classes.size() == db_classes.size());
    for (size_t i = 0; i < direct_classes.size(); ++i)
    {
        CHECK(direct_classes[i].qualified_name == db_classes[i].qualified_name);
        CHECK(direct_classes[i].name == db_classes[i].name);
        CHECK(direct_classes[i].module_path == db_classes[i].module_path);
        CHECK(direct_classes[i].source_file_path == db_classes[i].source_file_path);
        CHECK(direct_classes[i].entity_kind == db_classes[i].entity_kind);
        CHECK(direct_classes[i].is_struct == db_classes[i].is_struct);
        CHECK(direct_classes[i].base_size == db_classes[i].base_size);
        CHECK(direct_classes[i].building_functions == db_classes[i].building_functions);
        CHECK(direct_classes[i].function_sizes == db_classes[i].function_sizes);
        CHECK(direct_classes[i].function_names == db_classes[i].function_names);
        CHECK(direct_classes[i].road_size == db_classes[i].road_size);
        CHECK(direct_classes[i].is_abstract == db_classes[i].is_abstract);
    }

    const auto direct_dependencies = direct_source.list_class_dependencies_in_module("src");
    const auto db_dependencies = db.list_class_dependencies_in_module("src");
    REQUIRE(direct_dependencies.size() == db_dependencies.size());
    for (size_t i = 0; i < direct_dependencies.size(); ++i)
    {
        CHECK(direct_dependencies[i].source_qualified_name == db_dependencies[i].source_qualified_name);
        CHECK(direct_dependencies[i].source_module_path == db_dependencies[i].source_module_path);
        CHECK(direct_dependencies[i].field_name == db_dependencies[i].field_name);
        CHECK(direct_dependencies[i].field_type_name == db_dependencies[i].field_type_name);
        CHECK(direct_dependencies[i].target_qualified_name == db_dependencies[i].target_qualified_name);
        CHECK(direct_dependencies[i].target_module_path == db_dependencies[i].target_module_path);
        CHECK(direct_dependencies[i].source_file_path == db_dependencies[i].source_file_path);
        CHECK(direct_dependencies[i].target_file_path == db_dependencies[i].target_file_path);
        CHECK(direct_dependencies[i].is_abstract_ref == db_dependencies[i].is_abstract_ref);
    }

    CHECK(direct_source.codebase_health().complexity == db.codebase_health().complexity);
    CHECK(direct_source.codebase_health().cohesion == db.codebase_health().cohesion);
    CHECK(direct_source.codebase_health().coupling == db.codebase_health().coupling);

    db.close();
    remove_sqlite_artifacts(db_path);
}

} // namespace draxul

#endif
