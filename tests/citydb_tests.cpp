#include <catch2/catch_test_macros.hpp>

#include <draxul/citydb.h>
#include <draxul/treesitter.h>

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace draxul
{

namespace
{

class SqliteReadHandle final
{
public:
    explicit SqliteReadHandle(const std::filesystem::path& path)
    {
        REQUIRE(sqlite3_open_v2(path.string().c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    }

    ~SqliteReadHandle()
    {
        if (db_)
            sqlite3_close(db_);
    }

    [[nodiscard]] sqlite3* get() const
    {
        return db_;
    }

private:
    sqlite3* db_ = nullptr;
};

int scalar_int(sqlite3* db, const char* sql)
{
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const int value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

std::string scalar_text(sqlite3* db, const char* sql)
{
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const unsigned char* value = sqlite3_column_text(stmt, 0);
    const std::string text = value ? reinterpret_cast<const char*>(value) : "";
    sqlite3_finalize(stmt);
    return text;
}

} // namespace

TEST_CASE("city database reconciles tree-sitter snapshot into semantic tables", "[citydb]")
{
    const auto db_path = std::filesystem::temp_directory_path() / "draxul-citydb-tests.sqlite3";
    std::filesystem::remove(db_path);

    CodebaseSnapshot snapshot;
    snapshot.complete = true;
    snapshot.scan_time = std::chrono::steady_clock::now();

    ParsedFile file;
    file.path = "src/example.cpp";
    SymbolRecord tower{
        SymbolKind::Class,
        "Tower",
        "",
        false,
        10,
        30,
        3,
        { "IView", "PlainData" },
        {
            { "view_", "IView", { "IView" } },
            { "data_", "PlainData", { "PlainData" } },
            { "count_", "int", {} },
        },
    };
    SymbolRecord plain_data{
        SymbolKind::Struct,
        "PlainData",
        "",
        false,
        20,
        24,
        2,
        {},
        {
            { "x", "int", {} },
            { "y", "int", {} },
        },
    };
    SymbolRecord iview{
        SymbolKind::Class,
        "IView",
        "",
        true,
        30,
        35,
        1,
        { "PlainData" },
        {
            { "data_", "PlainData", { "PlainData" } },
        },
    };
    file.symbols = {
        tower,
        plain_data,
        iview,
        { SymbolKind::Function, "build_city", "", false, 40, 45, 0, {} },
        { SymbolKind::Function, "tick", "Tower", false, 50, 56, 0, {} },
    };
    snapshot.files.push_back(file);

    CityDatabase db;
    REQUIRE(db.open(db_path));
    REQUIRE(db.reconcile_snapshot(snapshot));

    const CityDbStats& stats = db.stats();
    CHECK(stats.file_count == 1);
    CHECK(stats.symbol_count == 5);
    CHECK(stats.city_entity_count == 4);
    CHECK(stats.has_reconciled_snapshot);

    SqliteReadHandle raw(db_path);
    CHECK(scalar_int(raw.get(), "SELECT COUNT(*) FROM files") == 1);
    CHECK(scalar_int(raw.get(), "SELECT COUNT(*) FROM symbols") == 5);
    CHECK(scalar_int(raw.get(), "SELECT COUNT(*) FROM city_entities") == 4);
    CHECK(scalar_text(raw.get(),
              "SELECT city_role FROM symbols WHERE qualified_name = 'Tower'")
        == "concrete_class");
    CHECK(scalar_text(raw.get(),
              "SELECT module_path FROM city_entities WHERE display_name = 'Tower'")
        == "src");
    CHECK(scalar_text(raw.get(),
              "SELECT entity_kind FROM city_entities WHERE display_name = 'build_city'")
        == "tree");
    CHECK(scalar_int(raw.get(),
              "SELECT base_size FROM city_entities WHERE display_name = 'Tower'")
        == 3);
    CHECK(scalar_int(raw.get(),
              "SELECT building_functions FROM city_entities WHERE display_name = 'Tower'")
        == 1);
    CHECK(scalar_text(raw.get(),
              "SELECT building_function_sizes_json FROM city_entities WHERE display_name = 'Tower'")
        == "[7]");
    CHECK(scalar_int(raw.get(),
              "SELECT road_size FROM city_entities WHERE display_name = 'Tower'")
        == 2);
    CHECK(scalar_int(raw.get(), "SELECT COUNT(*) FROM symbol_fields WHERE symbol_id LIKE '%|Tower|10'") == 3);
    CHECK(scalar_int(raw.get(),
              "SELECT COUNT(*) FROM city_entity_dependencies WHERE source_entity_id LIKE '%|Tower|10'")
        == 2);
    CHECK(scalar_text(raw.get(),
              "SELECT city_role FROM symbols WHERE qualified_name = 'IView'")
        == "abstract_class");
    CHECK(scalar_text(raw.get(),
              "SELECT city_role FROM symbols WHERE qualified_name = 'PlainData'")
        == "data_struct");
    CHECK(scalar_text(raw.get(),
              "SELECT city_role FROM symbols WHERE qualified_name = 'Tower::tick'")
        == "method");
    CHECK(scalar_text(raw.get(),
              "SELECT entity_kind FROM city_entities WHERE display_name = 'Tower'")
        == "building");

    const std::vector<std::string> modules = db.list_modules();
    REQUIRE(modules.size() == 1);
    CHECK(modules[0] == "src");

    const std::vector<CityClassRecord> classes = db.list_classes_in_module("src");
    REQUIRE(classes.size() == 3);
    CHECK(classes[0].module_path == "src");
    CHECK(classes[1].qualified_name == "PlainData");
    CHECK(classes[2].qualified_name == "Tower");
    CHECK(classes[2].base_size == 3);
    CHECK(classes[2].building_functions == 1);
    REQUIRE(classes[2].function_sizes.size() == 1);
    CHECK(classes[2].function_sizes[0] == 7);
    CHECK(classes[2].road_size == 2);

    const std::vector<CityDependencyRecord> deps = db.list_class_dependencies_in_module("src");
    REQUIRE(deps.size() == 3);
    CHECK(deps[0].source_qualified_name == "IView");
    CHECK(deps[0].field_name == "data_");
    CHECK(deps[0].target_qualified_name == "PlainData");
    CHECK(deps[1].source_qualified_name == "Tower");
    CHECK(deps[1].field_name == "data_");
    CHECK(deps[1].target_qualified_name == "PlainData");
    CHECK(deps[2].source_qualified_name == "Tower");
    CHECK(deps[2].field_name == "view_");
    CHECK(deps[2].target_qualified_name == "IView");

    // Verify per-module health metrics.
    const CityModuleRecord mod = db.module_record("src");
    CHECK(mod.building_count == 3);
    CHECK(mod.total_functions == 1); // only Tower::tick
    CHECK(mod.total_function_lines == 7);
    // Complexity: 1/(1 + 7/10) = 1/1.7 ≈ 0.588
    CHECK(mod.health.complexity > 0.5f);
    CHECK(mod.health.complexity < 0.65f);
    // Cohesion: ratio = 1 function / max(6 fields, 1) = 0.167; score = 0.167/1.167 ≈ 0.143
    // (Tower has 3 fields, PlainData has 2, IView has 1 → total 6 fields, 1 function)
    CHECK(mod.health.cohesion > 0.0f);
    CHECK(mod.health.cohesion < 0.5f);
    // Coupling: avg_deps = (2+0+1)/3 = 1.0; score = 1/(1+1/3) ≈ 0.75
    CHECK(mod.health.coupling > 0.6f);
    CHECK(mod.health.coupling < 0.85f);

    // Verify global codebase health (single module, so should match).
    const CodebaseHealthMetrics global = db.codebase_health();
    CHECK(global.complexity == mod.health.complexity);
    CHECK(global.cohesion == mod.health.cohesion);
    CHECK(global.coupling == mod.health.coupling);

    std::filesystem::remove(db_path);
}

} // namespace draxul
