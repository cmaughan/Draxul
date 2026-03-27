#include <draxul/citydb.h>

#include <draxul/treesitter.h>

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace draxul
{

namespace
{

constexpr int kSchemaVersion = 3;

class SqliteError final : public std::runtime_error
{
public:
    SqliteError(int code, std::string message)
        : std::runtime_error(std::move(message))
        , code_(code)
    {
    }

    [[nodiscard]] int code() const
    {
        return code_;
    }

private:
    int code_ = SQLITE_ERROR;
};

class DbHandle final
{
public:
    DbHandle() = default;

    explicit DbHandle(const std::filesystem::path& path)
    {
        open(path);
    }

    ~DbHandle()
    {
        close();
    }

    DbHandle(const DbHandle&) = delete;
    DbHandle& operator=(const DbHandle&) = delete;

    DbHandle(DbHandle&& other) noexcept
        : db_(std::exchange(other.db_, nullptr))
    {
    }

    DbHandle& operator=(DbHandle&& other) noexcept
    {
        if (this != &other)
        {
            close();
            db_ = std::exchange(other.db_, nullptr);
        }
        return *this;
    }

    void open(const std::filesystem::path& path)
    {
        close();

        sqlite3* db = nullptr;
        const int rc = sqlite3_open_v2(path.string().c_str(), &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        if (rc != SQLITE_OK)
        {
            const std::string message = db ? sqlite3_errmsg(db) : "sqlite3_open_v2 failed";
            if (db)
                sqlite3_close(db);
            throw SqliteError(rc, message);
        }

        db_ = db;
    }

    void close()
    {
        if (!db_)
            return;
        sqlite3_close(db_);
        db_ = nullptr;
    }

    [[nodiscard]] sqlite3* get() const
    {
        return db_;
    }

    [[nodiscard]] explicit operator bool() const
    {
        return db_ != nullptr;
    }

private:
    sqlite3* db_ = nullptr;
};

class Statement final
{
public:
    Statement() = default;

    Statement(sqlite3* db, std::string_view sql)
    {
        prepare(db, sql);
    }

    ~Statement()
    {
        reset();
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement(Statement&& other) noexcept
        : stmt_(std::exchange(other.stmt_, nullptr))
    {
    }

    Statement& operator=(Statement&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            stmt_ = std::exchange(other.stmt_, nullptr);
        }
        return *this;
    }

    void prepare(sqlite3* db, std::string_view sql)
    {
        reset();
        sqlite3_stmt* stmt = nullptr;
        const int rc = sqlite3_prepare_v2(
            db, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
        if (rc != SQLITE_OK)
            throw SqliteError(rc, sqlite3_errmsg(db));
        stmt_ = stmt;
    }

    void bind_text(int index, std::string_view value)
    {
        const int rc = sqlite3_bind_text(
            stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
        if (rc != SQLITE_OK)
            throw SqliteError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }

    void bind_int(int index, int value)
    {
        const int rc = sqlite3_bind_int(stmt_, index, value);
        if (rc != SQLITE_OK)
            throw SqliteError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }

    void bind_int64(int index, std::int64_t value)
    {
        const int rc = sqlite3_bind_int64(stmt_, index, value);
        if (rc != SQLITE_OK)
            throw SqliteError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }

    void bind_double(int index, double value)
    {
        const int rc = sqlite3_bind_double(stmt_, index, value);
        if (rc != SQLITE_OK)
            throw SqliteError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }

    [[nodiscard]] int step()
    {
        const int rc = sqlite3_step(stmt_);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE)
            throw SqliteError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
        return rc;
    }

    void step_done()
    {
        const int rc = step();
        if (rc != SQLITE_DONE)
            throw SqliteError(SQLITE_MISUSE, "expected SQLITE_DONE");
    }

    void clear_bindings()
    {
        const int rc = sqlite3_clear_bindings(stmt_);
        if (rc != SQLITE_OK)
            throw SqliteError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }

    void reset_step()
    {
        const int rc = sqlite3_reset(stmt_);
        if (rc != SQLITE_OK)
            throw SqliteError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }

    void reuse()
    {
        reset_step();
        clear_bindings();
    }

    [[nodiscard]] sqlite3_stmt* raw() const
    {
        return stmt_;
    }

private:
    void reset()
    {
        if (stmt_)
        {
            sqlite3_finalize(stmt_);
            stmt_ = nullptr;
        }
    }

    sqlite3_stmt* stmt_ = nullptr;
};

class Transaction final
{
public:
    explicit Transaction(sqlite3* db)
        : db_(db)
    {
        exec("BEGIN IMMEDIATE");
    }

    ~Transaction()
    {
        if (!committed_)
        {
            try
            {
                exec("ROLLBACK");
            }
            catch (...)
            {
            }
        }
    }

    void commit()
    {
        if (committed_)
            return;
        exec("COMMIT");
        committed_ = true;
    }

private:
    void exec(const char* sql)
    {
        char* err = nullptr;
        const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK)
        {
            const std::string message = err ? err : sqlite3_errmsg(db_);
            if (err)
                sqlite3_free(err);
            throw SqliteError(rc, message);
        }
    }

    sqlite3* db_ = nullptr;
    bool committed_ = false;
};

void exec(sqlite3* db, std::string_view sql)
{
    char* err = nullptr;
    const int rc = sqlite3_exec(db, std::string(sql).c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK)
    {
        const std::string message = err ? err : sqlite3_errmsg(db);
        if (err)
            sqlite3_free(err);
        throw SqliteError(rc, message);
    }
}

[[nodiscard]] std::string symbol_kind_name(SymbolKind kind)
{
    switch (kind)
    {
    case SymbolKind::Function:
        return "function";
    case SymbolKind::Class:
        return "class";
    case SymbolKind::Struct:
        return "struct";
    case SymbolKind::Include:
        return "include";
    }
    return "unknown";
}

enum class CityRole
{
    ConcreteClass,
    AbstractClass,
    DataStruct,
    FreeFunction,
    Method,
    Include,
};

[[nodiscard]] std::string city_role_name(CityRole role)
{
    switch (role)
    {
    case CityRole::ConcreteClass:
        return "concrete_class";
    case CityRole::AbstractClass:
        return "abstract_class";
    case CityRole::DataStruct:
        return "data_struct";
    case CityRole::FreeFunction:
        return "free_function";
    case CityRole::Method:
        return "method";
    case CityRole::Include:
        return "include";
    }
    return "unknown";
}

struct EntitySpec
{
    const char* entity_kind = "";
    const char* district = "";
    double height = 1.0;
    double footprint_x = 1.0;
    double footprint_y = 1.0;
};

[[nodiscard]] EntitySpec entity_spec(CityRole role, int member_count)
{
    const double footprint = std::max(1.0, std::ceil(std::sqrt(std::max(member_count, 1))));
    switch (role)
    {
    case CityRole::ConcreteClass:
        return { "building", "classes", 8.0 + static_cast<double>(member_count), footprint, footprint };
    case CityRole::AbstractClass:
        return { "tower", "abstract", 10.0 + static_cast<double>(member_count), footprint, footprint };
    case CityRole::DataStruct:
        return { "block", "structs", 4.0 + static_cast<double>(member_count), footprint, footprint };
    case CityRole::FreeFunction:
        return { "tree", "functions", 3.0, 1.0, 1.0 };
    case CityRole::Method:
    case CityRole::Include:
        return {};
    }
    return {};
}

std::string json_int_array(const std::vector<int>& values)
{
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0)
            out << ",";
        out << values[i];
    }
    out << "]";
    return out.str();
}

std::vector<int> parse_json_int_array(std::string_view text)
{
    std::vector<int> values;
    int current = 0;
    bool in_number = false;
    bool negative = false;
    for (const char ch : text)
    {
        if (ch == '-')
        {
            negative = true;
            in_number = true;
            current = 0;
        }
        else if (ch >= '0' && ch <= '9')
        {
            if (!in_number)
            {
                in_number = true;
                current = 0;
                negative = false;
            }
            current = current * 10 + (ch - '0');
        }
        else if (in_number)
        {
            values.push_back(negative ? -current : current);
            in_number = false;
            negative = false;
            current = 0;
        }
    }
    if (in_number)
        values.push_back(negative ? -current : current);
    return values;
}

std::string module_path_for_file(std::string_view file_path)
{
    const std::filesystem::path path(file_path);
    // Group by library root: libs/<lib-name> or top-level directory (e.g. app).
    auto it = path.begin();
    if (it == path.end())
        return {};
    const std::string first = it->string();
    ++it;
    if (first == "libs" && it != path.end())
    {
        // libs/<lib-name>/... → module is "libs/<lib-name>"
        return (std::filesystem::path(first) / *it).generic_string();
    }
    // top-level dir (app/, shaders/, etc.)
    return first;
}

[[nodiscard]] std::string make_symbol_id(
    const std::string& path, SymbolKind kind, std::string_view qualified_name, uint32_t line)
{
    return path + "|" + symbol_kind_name(kind) + "|" + std::string(qualified_name) + "|"
        + std::to_string(line);
}

void create_schema_v2(sqlite3* db)
{
    exec(db, R"sql(
        CREATE TABLE IF NOT EXISTS files (
            path TEXT PRIMARY KEY NOT NULL,
            module_path TEXT NOT NULL,
            symbol_count INTEGER NOT NULL,
            parse_error_count INTEGER NOT NULL,
            last_scanned_at_unix INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS symbols (
            symbol_id TEXT PRIMARY KEY NOT NULL,
            file_path TEXT NOT NULL REFERENCES files(path) ON DELETE CASCADE,
            module_path TEXT NOT NULL,
            kind TEXT NOT NULL,
            name TEXT NOT NULL,
            qualified_name TEXT NOT NULL,
            parent_name TEXT NOT NULL,
            line_start INTEGER NOT NULL,
            line_end INTEGER NOT NULL,
            is_abstract INTEGER NOT NULL,
            city_role TEXT NOT NULL,
            field_count INTEGER NOT NULL DEFAULT 0
        );

        CREATE INDEX IF NOT EXISTS idx_symbols_file_path ON symbols(file_path);
        CREATE INDEX IF NOT EXISTS idx_symbols_city_role ON symbols(city_role);
        CREATE INDEX IF NOT EXISTS idx_symbols_module_path ON symbols(module_path);

        CREATE TABLE IF NOT EXISTS city_entities (
            entity_id TEXT PRIMARY KEY NOT NULL,
            symbol_id TEXT NOT NULL UNIQUE REFERENCES symbols(symbol_id) ON DELETE CASCADE,
            entity_kind TEXT NOT NULL,
            district TEXT NOT NULL,
            display_name TEXT NOT NULL,
            module_path TEXT NOT NULL,
            source_file_path TEXT NOT NULL,
            source_line INTEGER NOT NULL,
            base_size INTEGER NOT NULL,
            building_functions INTEGER NOT NULL,
            building_function_sizes_json TEXT NOT NULL,
            road_size INTEGER NOT NULL,
            height REAL NOT NULL,
            footprint_x REAL NOT NULL,
            footprint_y REAL NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_city_entities_district ON city_entities(district);
        CREATE INDEX IF NOT EXISTS idx_city_entities_module_path ON city_entities(module_path);
    )sql");
    exec(db, "PRAGMA user_version = 2");
}

void create_schema_v3(sqlite3* db)
{
    create_schema_v2(db);

    exec(db, R"sql(
        CREATE TABLE IF NOT EXISTS city_modules (
            module_path TEXT PRIMARY KEY NOT NULL,
            building_count INTEGER NOT NULL,
            total_functions INTEGER NOT NULL,
            total_function_lines INTEGER NOT NULL,
            avg_function_size REAL NOT NULL,
            quality REAL NOT NULL
        );
    )sql");
    exec(db, "PRAGMA user_version = 3");
}

void migrate_v1_to_v3(sqlite3* db)
{
    // The city DB is a derived cache, so a destructive migration is acceptable
    // here and simpler than preserving the old intermediate layout.
    exec(db, "DROP TABLE IF EXISTS city_modules");
    exec(db, "DROP TABLE IF EXISTS city_entities");
    exec(db, "DROP TABLE IF EXISTS symbols");
    exec(db, "DROP TABLE IF EXISTS files");
    create_schema_v3(db);
}

void migrate_v2_to_v3(sqlite3* db)
{
    exec(db, R"sql(
        CREATE TABLE IF NOT EXISTS city_modules (
            module_path TEXT PRIMARY KEY NOT NULL,
            building_count INTEGER NOT NULL,
            total_functions INTEGER NOT NULL,
            total_function_lines INTEGER NOT NULL,
            avg_function_size REAL NOT NULL,
            quality REAL NOT NULL
        );
    )sql");
    exec(db, "PRAGMA user_version = 3");
}

void migrate_schema(sqlite3* db)
{
    int version = 0;
    {
        sqlite3_stmt* raw_stmt = nullptr;
        const int rc = sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &raw_stmt, nullptr);
        if (rc != SQLITE_OK)
            throw SqliteError(rc, sqlite3_errmsg(db));
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw_stmt, &sqlite3_finalize);
        if (sqlite3_step(stmt.get()) == SQLITE_ROW)
            version = sqlite3_column_int(stmt.get(), 0);
    }

    if (version == 0)
        create_schema_v3(db);
    else if (version == 1)
        migrate_v1_to_v3(db);
    else if (version == 2)
        migrate_v2_to_v3(db);

    {
        sqlite3_stmt* raw_stmt = nullptr;
        const int rc = sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &raw_stmt, nullptr);
        if (rc != SQLITE_OK)
            throw SqliteError(rc, sqlite3_errmsg(db));
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw_stmt, &sqlite3_finalize);
        if (sqlite3_step(stmt.get()) == SQLITE_ROW)
            version = sqlite3_column_int(stmt.get(), 0);
    }

    if (version != kSchemaVersion)
        throw SqliteError(SQLITE_SCHEMA, "unsupported city database schema version");
}

} // namespace

struct CityDatabase::Impl
{
    DbHandle db;
    std::filesystem::path path;
    std::string last_error;
    CityDbStats stats;
};

CityDatabase::CityDatabase()
    : impl_(new Impl())
{
}

CityDatabase::~CityDatabase()
{
    delete impl_;
}

CityDatabase::CityDatabase(CityDatabase&& other) noexcept
    : impl_(std::exchange(other.impl_, nullptr))
{
}

CityDatabase& CityDatabase::operator=(CityDatabase&& other) noexcept
{
    if (this != &other)
    {
        delete impl_;
        impl_ = std::exchange(other.impl_, nullptr);
    }
    return *this;
}

bool CityDatabase::open(const std::filesystem::path& path)
{
    if (!impl_)
        return false;

    try
    {
        std::filesystem::create_directories(path.parent_path());
        impl_->db.open(path);
        impl_->path = path;
        impl_->stats.path = path;
        impl_->last_error.clear();

        exec(impl_->db.get(), "PRAGMA foreign_keys = ON");
        exec(impl_->db.get(), "PRAGMA journal_mode = WAL");
        exec(impl_->db.get(), "PRAGMA synchronous = NORMAL");
        sqlite3_busy_timeout(impl_->db.get(), 1000);
        migrate_schema(impl_->db.get());
        return true;
    }
    catch (const std::exception& ex)
    {
        impl_->last_error = ex.what();
        impl_->db.close();
        return false;
    }
}

void CityDatabase::close()
{
    if (!impl_)
        return;
    impl_->db.close();
}

bool CityDatabase::is_open() const
{
    return impl_ && static_cast<bool>(impl_->db);
}

const std::filesystem::path& CityDatabase::path() const
{
    static const std::filesystem::path empty;
    return impl_ ? impl_->path : empty;
}

const std::string& CityDatabase::last_error() const
{
    static const std::string empty;
    return impl_ ? impl_->last_error : empty;
}

const CityDbStats& CityDatabase::stats() const
{
    static const CityDbStats empty;
    return impl_ ? impl_->stats : empty;
}

bool CityDatabase::reconcile_snapshot(const CodebaseSnapshot& snapshot)
{
    if (!impl_ || !impl_->db)
        return false;

    try
    {
        std::unordered_set<std::string> types_with_methods;
        std::unordered_set<std::string> known_types;
        std::unordered_map<std::string, std::vector<int>> method_sizes_by_type;
        for (const auto& file : snapshot.files)
        {
            for (const auto& sym : file.symbols)
            {
                if (sym.kind == SymbolKind::Class || sym.kind == SymbolKind::Struct)
                    known_types.insert(sym.name);
                if (sym.kind == SymbolKind::Function && !sym.parent.empty())
                {
                    types_with_methods.insert(sym.parent);
                    const int function_size = static_cast<int>(
                        sym.end_line >= sym.line ? (sym.end_line - sym.line + 1) : 1);
                    method_sizes_by_type[sym.parent].push_back(function_size);
                }
            }
        }

        Transaction txn(impl_->db.get());
        exec(impl_->db.get(), "DELETE FROM city_entities");
        exec(impl_->db.get(), "DELETE FROM symbols");
        exec(impl_->db.get(), "DELETE FROM files");

        Statement insert_file(impl_->db.get(),
            "INSERT INTO files(path, module_path, symbol_count, parse_error_count, last_scanned_at_unix) "
            "VALUES(?, ?, ?, ?, ?)");
        Statement insert_symbol(impl_->db.get(),
            "INSERT INTO symbols(symbol_id, file_path, module_path, kind, name, qualified_name, parent_name, "
            "line_start, line_end, is_abstract, city_role, field_count) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        Statement insert_entity(impl_->db.get(),
            "INSERT INTO city_entities(entity_id, symbol_id, entity_kind, district, display_name, module_path, "
            "source_file_path, source_line, base_size, building_functions, building_function_sizes_json, road_size, "
            "height, footprint_x, footprint_y) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

        const auto reconcile_wall_time = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
                                             .count();

        size_t symbol_count = 0;
        size_t entity_count = 0;

        for (const auto& file : snapshot.files)
        {
            const std::string module_path = module_path_for_file(file.path);
            insert_file.reuse();
            insert_file.bind_text(1, file.path);
            insert_file.bind_text(2, module_path);
            insert_file.bind_int(3, static_cast<int>(file.symbols.size()));
            insert_file.bind_int(4, static_cast<int>(file.errors.size()));
            insert_file.bind_int64(5, reconcile_wall_time);
            insert_file.step_done();

            for (const auto& sym : file.symbols)
            {
                CityRole role = CityRole::Include;
                if (sym.kind == SymbolKind::Function)
                    role = sym.parent.empty() ? CityRole::FreeFunction : CityRole::Method;
                else if (sym.kind == SymbolKind::Class || sym.kind == SymbolKind::Struct)
                {
                    if (sym.is_abstract)
                        role = CityRole::AbstractClass;
                    else if (sym.kind == SymbolKind::Struct
                        && types_with_methods.find(sym.name) == types_with_methods.end())
                        role = CityRole::DataStruct;
                    else
                        role = CityRole::ConcreteClass;
                }

                const std::string qualified_name = sym.parent.empty()
                    ? sym.name
                    : (sym.parent + "::" + sym.name);
                const int base_size = (sym.kind == SymbolKind::Class || sym.kind == SymbolKind::Struct)
                    ? static_cast<int>(sym.field_count)
                    : 0;
                std::vector<int> function_sizes;
                if (sym.kind == SymbolKind::Class || sym.kind == SymbolKind::Struct)
                {
                    auto it = method_sizes_by_type.find(sym.name);
                    if (it != method_sizes_by_type.end())
                        function_sizes = it->second;
                }
                const int building_functions = static_cast<int>(function_sizes.size());
                std::unordered_set<std::string> external_refs;
                if (sym.kind == SymbolKind::Class || sym.kind == SymbolKind::Struct)
                {
                    for (const auto& ref : sym.referenced_types)
                    {
                        if (ref != sym.name && known_types.find(ref) != known_types.end())
                            external_refs.insert(ref);
                    }
                }
                const int road_size = static_cast<int>(external_refs.size());
                const std::string symbol_id = make_symbol_id(file.path, sym.kind, qualified_name, sym.line);

                insert_symbol.reuse();
                insert_symbol.bind_text(1, symbol_id);
                insert_symbol.bind_text(2, file.path);
                insert_symbol.bind_text(3, module_path);
                insert_symbol.bind_text(4, symbol_kind_name(sym.kind));
                insert_symbol.bind_text(5, sym.name);
                insert_symbol.bind_text(6, qualified_name);
                insert_symbol.bind_text(7, sym.parent);
                insert_symbol.bind_int(8, static_cast<int>(sym.line));
                insert_symbol.bind_int(9, static_cast<int>(sym.end_line));
                insert_symbol.bind_int(10, sym.is_abstract ? 1 : 0);
                insert_symbol.bind_text(11, city_role_name(role));
                insert_symbol.bind_int(12, static_cast<int>(sym.field_count));
                insert_symbol.step_done();
                ++symbol_count;

                if (role == CityRole::Method || role == CityRole::Include)
                    continue;

                const EntitySpec spec = entity_spec(role, base_size);
                insert_entity.reuse();
                insert_entity.bind_text(1, symbol_id);
                insert_entity.bind_text(2, symbol_id);
                insert_entity.bind_text(3, spec.entity_kind);
                insert_entity.bind_text(4, spec.district);
                insert_entity.bind_text(5, qualified_name);
                insert_entity.bind_text(6, module_path);
                insert_entity.bind_text(7, file.path);
                insert_entity.bind_int(8, static_cast<int>(sym.line));
                insert_entity.bind_int(9, base_size);
                insert_entity.bind_int(10, building_functions);
                insert_entity.bind_text(11, json_int_array(function_sizes));
                insert_entity.bind_int(12, road_size);
                insert_entity.bind_double(13, spec.height);
                insert_entity.bind_double(14, spec.footprint_x);
                insert_entity.bind_double(15, spec.footprint_y);
                insert_entity.step_done();
                ++entity_count;
            }
        }

        // Populate city_modules with aggregated quality metrics.
        exec(impl_->db.get(), "DELETE FROM city_modules");
        Statement insert_module(impl_->db.get(),
            "INSERT INTO city_modules(module_path, building_count, total_functions, "
            "total_function_lines, avg_function_size, quality) VALUES(?, ?, ?, ?, ?, ?)");

        // Aggregate function sizes per module from the just-inserted entities.
        Statement query_modules(impl_->db.get(),
            "SELECT module_path, COUNT(*), SUM(building_functions), building_function_sizes_json "
            "FROM city_entities "
            "WHERE entity_kind IN ('building', 'tower', 'block') "
            "GROUP BY module_path ORDER BY module_path");

        // We need per-module function size lists, but SQL can't easily parse JSON arrays.
        // So group by module_path and accumulate from the per-entity JSON in a second pass.
        struct ModuleAgg
        {
            int building_count = 0;
            int total_functions = 0;
            int total_function_lines = 0;
        };
        std::unordered_map<std::string, ModuleAgg> module_agg;

        {
            Statement entity_scan(impl_->db.get(),
                "SELECT module_path, building_functions, building_function_sizes_json "
                "FROM city_entities WHERE entity_kind IN ('building', 'tower', 'block')");
            const auto col_text = [&](int index) -> std::string {
                const auto* t = sqlite3_column_text(entity_scan.raw(), index);
                return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
            };
            while (entity_scan.step() == SQLITE_ROW)
            {
                const std::string mp = col_text(0);
                auto& agg = module_agg[mp];
                agg.building_count++;
                const int funcs = sqlite3_column_int(entity_scan.raw(), 1);
                agg.total_functions += funcs;
                const std::vector<int> sizes = parse_json_int_array(col_text(2));
                for (const int sz : sizes)
                    agg.total_function_lines += sz;
            }
        }

        for (const auto& [mp, agg] : module_agg)
        {
            const float avg_fn_size = agg.total_functions > 0
                ? static_cast<float>(agg.total_function_lines) / static_cast<float>(agg.total_functions)
                : 0.0f;

            // Quality: 1.0 for tiny avg function size, tapering toward 0.0 for large.
            // An average of ~5 lines is excellent (quality ~0.67), ~30 lines is poor (~0.25).
            // Formula: quality = 1 / (1 + avg/10), gives a smooth 0-1 curve.
            const float quality = agg.total_functions > 0
                ? 1.0f / (1.0f + avg_fn_size / 10.0f)
                : 0.5f; // no functions = neutral

            insert_module.reuse();
            insert_module.bind_text(1, mp);
            insert_module.bind_int(2, agg.building_count);
            insert_module.bind_int(3, agg.total_functions);
            insert_module.bind_int(4, agg.total_function_lines);
            insert_module.bind_double(5, static_cast<double>(avg_fn_size));
            insert_module.bind_double(6, static_cast<double>(quality));
            insert_module.step_done();
        }

        txn.commit();

        impl_->stats.path = impl_->path;
        impl_->stats.file_count = snapshot.files.size();
        impl_->stats.symbol_count = symbol_count;
        impl_->stats.city_entity_count = entity_count;
        impl_->stats.last_reconcile_time = std::chrono::steady_clock::now();
        impl_->stats.has_reconciled_snapshot = true;
        impl_->last_error.clear();
        return true;
    }
    catch (const std::exception& ex)
    {
        impl_->last_error = ex.what();
        return false;
    }
}

std::vector<std::string> CityDatabase::list_modules() const
{
    std::vector<std::string> modules;
    if (!impl_ || !impl_->db)
        return modules;

    try
    {
        Statement stmt(impl_->db.get(),
            "SELECT DISTINCT module_path FROM city_entities "
            "WHERE entity_kind IN ('building', 'tower', 'block') ORDER BY module_path");
        while (stmt.step() == SQLITE_ROW)
        {
            const unsigned char* text = sqlite3_column_text(stmt.raw(), 0);
            modules.emplace_back(text ? reinterpret_cast<const char*>(text) : "");
        }
    }
    catch (const std::exception& ex)
    {
        impl_->last_error = ex.what();
    }

    return modules;
}

std::vector<CityClassRecord> CityDatabase::list_classes_in_module(std::string_view module_path) const
{
    std::vector<CityClassRecord> rows;
    if (!impl_ || !impl_->db)
        return rows;

    try
    {
        Statement stmt(impl_->db.get(),
            "SELECT ce.display_name, s.name, ce.module_path, ce.source_file_path, ce.entity_kind, "
            "ce.base_size, ce.building_functions, ce.building_function_sizes_json, ce.road_size, s.is_abstract "
            "FROM city_entities ce "
            "JOIN symbols s ON s.symbol_id = ce.symbol_id "
            "WHERE ce.module_path = ? AND ce.entity_kind IN ('building', 'tower', 'block') "
            "ORDER BY ce.display_name");
        stmt.bind_text(1, module_path);
        while (stmt.step() == SQLITE_ROW)
        {
            CityClassRecord row;
            const auto read_text = [&](int index) -> std::string {
                const unsigned char* text = sqlite3_column_text(stmt.raw(), index);
                return text ? reinterpret_cast<const char*>(text) : "";
            };
            row.qualified_name = read_text(0);
            row.name = read_text(1);
            row.module_path = read_text(2);
            row.source_file_path = read_text(3);
            row.entity_kind = read_text(4);
            row.base_size = sqlite3_column_int(stmt.raw(), 5);
            row.building_functions = sqlite3_column_int(stmt.raw(), 6);
            row.function_sizes = parse_json_int_array(read_text(7));
            row.road_size = sqlite3_column_int(stmt.raw(), 8);
            row.is_abstract = sqlite3_column_int(stmt.raw(), 9) != 0;
            rows.push_back(std::move(row));
        }
    }
    catch (const std::exception& ex)
    {
        impl_->last_error = ex.what();
    }

    return rows;
}

CityModuleRecord CityDatabase::module_record(std::string_view module_path) const
{
    CityModuleRecord record;
    record.module_path = std::string(module_path);
    if (!impl_ || !impl_->db)
        return record;

    try
    {
        Statement stmt(impl_->db.get(),
            "SELECT building_count, total_functions, total_function_lines, avg_function_size, quality "
            "FROM city_modules WHERE module_path = ?");
        stmt.bind_text(1, module_path);
        if (stmt.step() == SQLITE_ROW)
        {
            record.building_count = sqlite3_column_int(stmt.raw(), 0);
            record.total_functions = sqlite3_column_int(stmt.raw(), 1);
            record.total_function_lines = sqlite3_column_int(stmt.raw(), 2);
            record.avg_function_size = static_cast<float>(sqlite3_column_double(stmt.raw(), 3));
            record.quality = static_cast<float>(sqlite3_column_double(stmt.raw(), 4));
        }
    }
    catch (const std::exception& ex)
    {
        impl_->last_error = ex.what();
    }

    return record;
}

} // namespace draxul
