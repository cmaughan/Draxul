#include <draxul/citydb.h>
#include <draxul/perf_timing.h>

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

constexpr int kSchemaVersion = 8;

class SqliteError final : public std::runtime_error
{
public:
    SqliteError(int code, std::string message)
        : std::runtime_error(message)
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
        PERF_MEASURE();
        if (this != &other)
        {
            close();
            db_ = std::exchange(other.db_, nullptr);
        }
        return *this;
    }

    void open(const std::filesystem::path& path)
    {
        PERF_MEASURE();
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
        PERF_MEASURE();
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
        PERF_MEASURE();
        if (this != &other)
        {
            reset();
            stmt_ = std::exchange(other.stmt_, nullptr);
        }
        return *this;
    }

    void prepare(sqlite3* db, std::string_view sql)
    {
        PERF_MEASURE();
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
        PERF_MEASURE();
        const int rc = sqlite3_bind_text(
            stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
        if (rc != SQLITE_OK)
            throw SqliteError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }

    void bind_int(int index, int value)
    {
        PERF_MEASURE();
        const int rc = sqlite3_bind_int(stmt_, index, value);
        if (rc != SQLITE_OK)
            throw SqliteError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }

    void bind_int64(int index, std::int64_t value)
    {
        PERF_MEASURE();
        const int rc = sqlite3_bind_int64(stmt_, index, value);
        if (rc != SQLITE_OK)
            throw SqliteError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }

    void bind_double(int index, double value)
    {
        PERF_MEASURE();
        const int rc = sqlite3_bind_double(stmt_, index, value);
        if (rc != SQLITE_OK)
            throw SqliteError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }

    [[nodiscard]] int step()
    {
        PERF_MEASURE();
        const int rc = sqlite3_step(stmt_);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE)
            throw SqliteError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
        return rc;
    }

    void step_done()
    {
        PERF_MEASURE();
        const int rc = step();
        if (rc != SQLITE_DONE)
            throw SqliteError(SQLITE_MISUSE, "expected SQLITE_DONE");
    }

    void clear_bindings()
    {
        PERF_MEASURE();
        const int rc = sqlite3_clear_bindings(stmt_);
        if (rc != SQLITE_OK)
            throw SqliteError(rc, sqlite3_errmsg(sqlite3_db_handle(stmt_)));
    }

    void reset_step()
    {
        PERF_MEASURE();
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
        PERF_MEASURE();
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
        PERF_MEASURE();
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
        PERF_MEASURE();
        if (committed_)
            return;
        exec("COMMIT");
        committed_ = true;
    }

private:
    void exec(const char* sql)
    {
        PERF_MEASURE();
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
    PERF_MEASURE();
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
    PERF_MEASURE();
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

std::string json_string_array(const std::vector<std::string>& values)
{
    PERF_MEASURE();
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0)
            out << ",";
        out << "\"";
        for (const char ch : values[i])
        {
            if (ch == '"' || ch == '\\')
                out << '\\';
            out << ch;
        }
        out << "\"";
    }
    out << "]";
    return out.str();
}

std::vector<std::string> parse_json_string_array(std::string_view text)
{
    PERF_MEASURE();
    std::vector<std::string> values;
    bool in_string = false;
    bool escaped = false;
    std::string current;
    for (const char ch : text)
    {
        if (escaped)
        {
            current += ch;
            escaped = false;
        }
        else if (ch == '\\' && in_string)
        {
            escaped = true;
        }
        else if (ch == '"')
        {
            if (in_string)
            {
                values.push_back(std::move(current));
                current.clear();
            }
            in_string = !in_string;
        }
        else if (in_string)
        {
            current += ch;
        }
    }
    return values;
}

std::vector<int> parse_json_int_array(std::string_view text)
{
    PERF_MEASURE();
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
    PERF_MEASURE();
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

[[nodiscard]] std::string make_field_id(
    std::string_view symbol_id, std::string_view field_name, size_t ordinal)
{
    return std::string(symbol_id) + "|field|" + std::string(field_name) + "|" + std::to_string(ordinal);
}

// Build a directed parent→children map from the inheritance graph, then for each
// type that has children compute all transitive descendants.  This replaces the old
// undirected connected-component approach which flooded across multiple-inheritance
// boundaries (e.g. IImGuiHost → MetalRenderer → IGridRenderer → entire renderer tree).
std::unordered_map<std::string, std::vector<std::string>> build_inheritance_descendants(
    const std::unordered_map<std::string, std::vector<std::string>>& direct_base_refs_by_symbol_id,
    const std::unordered_map<std::string, std::vector<std::string>>& known_type_symbol_ids)
{
    PERF_MEASURE();
    // child → parent edges already exist in direct_base_refs_by_symbol_id.
    // Invert to parent → [children].
    std::unordered_map<std::string, std::vector<std::string>> children_of;
    for (const auto& [symbol_id, base_refs] : direct_base_refs_by_symbol_id)
    {
        for (const std::string& base_ref : base_refs)
        {
            const auto it = known_type_symbol_ids.find(base_ref);
            if (it == known_type_symbol_ids.end() || it->second.size() != 1)
                continue;
            const std::string& parent_symbol_id = it->second.front();
            if (parent_symbol_id == symbol_id)
                continue;
            children_of[parent_symbol_id].push_back(symbol_id);
        }
    }

    // For each parent, BFS to collect itself + all transitive descendants.
    std::unordered_map<std::string, std::vector<std::string>> descendants;
    for (const auto& [parent, direct_children] : children_of)
    {
        std::vector<std::string> all;
        all.push_back(parent);
        std::unordered_set<std::string> visited;
        visited.insert(parent);
        std::vector<std::string> frontier = direct_children;
        while (!frontier.empty())
        {
            const std::string current = frontier.back();
            frontier.pop_back();
            if (!visited.insert(current).second)
                continue;
            all.push_back(current);
            const auto it = children_of.find(current);
            if (it != children_of.end())
                for (const std::string& child : it->second)
                    frontier.push_back(child);
        }
        descendants[parent] = std::move(all);
    }

    return descendants;
}

struct ResolvedTargets
{
    std::vector<std::string> symbol_ids;
    bool is_abstract_ref = false;
};

ResolvedTargets resolve_dependency_targets(
    std::string_view source_symbol_id,
    std::string_view referenced_type_name,
    const std::unordered_map<std::string, std::vector<std::string>>& known_type_symbol_ids,
    const std::unordered_map<std::string, std::vector<std::string>>& inheritance_descendants,
    const std::unordered_set<std::string>& abstract_symbol_ids)
{
    PERF_MEASURE();
    const auto it = known_type_symbol_ids.find(std::string(referenced_type_name));
    if (it == known_type_symbol_ids.end() || it->second.size() != 1)
        return {};

    const std::string& direct_target_symbol_id = it->second.front();
    const bool is_abstract = abstract_symbol_ids.contains(direct_target_symbol_id);
    std::vector<std::string> targets;

    // Only fan out to descendants for abstract/interface types.
    // Concrete types get a single direct edge.
    if (is_abstract)
    {
        const auto component_it = inheritance_descendants.find(direct_target_symbol_id);
        if (component_it != inheritance_descendants.end())
            targets = component_it->second;
        else
            targets = { direct_target_symbol_id };
    }
    else
    {
        targets = { direct_target_symbol_id };
    }

    targets.erase(
        std::remove_if(
            targets.begin(),
            targets.end(),
            [&](const std::string& target_symbol_id) { return target_symbol_id == source_symbol_id; }),
        targets.end());
    return { std::move(targets), is_abstract };
}

void create_schema_v2(sqlite3* db)
{
    PERF_MEASURE();
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
            building_function_names_json TEXT NOT NULL,
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
    PERF_MEASURE();
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

void create_schema_v4(sqlite3* db)
{
    PERF_MEASURE();
    create_schema_v2(db);

    exec(db, R"sql(
        CREATE TABLE IF NOT EXISTS city_modules (
            module_path TEXT PRIMARY KEY NOT NULL,
            building_count INTEGER NOT NULL,
            total_functions INTEGER NOT NULL,
            total_function_lines INTEGER NOT NULL,
            avg_function_size REAL NOT NULL,
            quality REAL NOT NULL,
            complexity REAL NOT NULL DEFAULT 0.5,
            cohesion REAL NOT NULL DEFAULT 0.5,
            coupling REAL NOT NULL DEFAULT 0.5
        );
    )sql");
    exec(db, "PRAGMA user_version = 4");
}

void create_schema_v5(sqlite3* db)
{
    PERF_MEASURE();
    create_schema_v4(db);

    exec(db, R"sql(
        CREATE TABLE IF NOT EXISTS symbol_fields (
            field_id TEXT PRIMARY KEY NOT NULL,
            symbol_id TEXT NOT NULL REFERENCES symbols(symbol_id) ON DELETE CASCADE,
            field_name TEXT NOT NULL,
            field_type_name TEXT NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_symbol_fields_symbol_id ON symbol_fields(symbol_id);

        CREATE TABLE IF NOT EXISTS city_entity_dependencies (
            source_entity_id TEXT NOT NULL REFERENCES city_entities(entity_id) ON DELETE CASCADE,
            target_entity_id TEXT NOT NULL REFERENCES city_entities(entity_id) ON DELETE CASCADE,
            field_name TEXT NOT NULL,
            field_type_name TEXT NOT NULL,
            is_abstract_ref INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY(source_entity_id, target_entity_id, field_name, field_type_name)
        );

        CREATE INDEX IF NOT EXISTS idx_city_entity_dependencies_source ON city_entity_dependencies(source_entity_id);
        CREATE INDEX IF NOT EXISTS idx_city_entity_dependencies_target ON city_entity_dependencies(target_entity_id);
    )sql");
    exec(db, "PRAGMA user_version = 5");
}

void drop_all_tables(sqlite3* db)
{
    PERF_MEASURE();
    exec(db, "DROP TABLE IF EXISTS city_entity_dependencies");
    exec(db, "DROP TABLE IF EXISTS symbol_fields");
    exec(db, "DROP TABLE IF EXISTS city_modules");
    exec(db, "DROP TABLE IF EXISTS city_entities");
    exec(db, "DROP TABLE IF EXISTS symbols");
    exec(db, "DROP TABLE IF EXISTS files");
}

void create_schema_v6(sqlite3* db)
{
    PERF_MEASURE();
    create_schema_v5(db);
    exec(db, "PRAGMA user_version = 6");
}

void create_schema_v7(sqlite3* db)
{
    PERF_MEASURE();
    create_schema_v6(db);
    exec(db, "PRAGMA user_version = 7");
}

void create_schema_v8(sqlite3* db)
{
    PERF_MEASURE();
    create_schema_v7(db);
    exec(db, "PRAGMA user_version = 8");
}

void migrate_to_current_destructive(sqlite3* db)
{
    PERF_MEASURE();
    // The city DB is a derived cache, so a destructive migration is acceptable
    // here and simpler than preserving the old intermediate layout.
    drop_all_tables(db);
    create_schema_v8(db);
}

// Returns true if a destructive migration was performed (all data rebuilt).
bool migrate_schema(sqlite3* db)
{
    PERF_MEASURE();
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

    bool migrated = false;
    if (version == 0)
        create_schema_v8(db);
    else if (version < kSchemaVersion)
    {
        migrate_to_current_destructive(db);
        migrated = true;
    }

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
    return migrated;
}

} // namespace

struct CityDatabase::Impl
{
    DbHandle db;
    std::filesystem::path path;
    std::string last_error;
    CityDbStats stats;
    bool schema_migrated = false;
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
    PERF_MEASURE();
    if (this != &other)
    {
        delete impl_;
        impl_ = std::exchange(other.impl_, nullptr);
    }
    return *this;
}

bool CityDatabase::open(const std::filesystem::path& path)
{
    PERF_MEASURE();
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
        impl_->schema_migrated = migrate_schema(impl_->db.get());
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
    PERF_MEASURE();
    if (!impl_)
        return;
    impl_->db.close();
}

bool CityDatabase::is_open() const
{
    return impl_ && static_cast<bool>(impl_->db);
}

bool CityDatabase::schema_migrated() const
{
    return impl_ && impl_->schema_migrated;
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
    PERF_MEASURE();
    if (!impl_ || !impl_->db)
        return false;

    try
    {
        std::unordered_set<std::string> types_with_methods;
        std::unordered_map<std::string, std::vector<int>> method_sizes_by_type;
        std::unordered_map<std::string, std::vector<std::string>> method_names_by_type;
        std::unordered_map<std::string, std::vector<std::string>> known_type_symbol_ids;
        std::unordered_map<std::string, std::vector<std::string>> direct_base_refs_by_symbol_id;
        std::unordered_set<std::string> abstract_symbol_ids;
        for (const auto& file : snapshot.files)
        {
            for (const auto& sym : file.symbols)
            {
                if (sym.kind == SymbolKind::Class || sym.kind == SymbolKind::Struct)
                {
                    const std::string qualified_name = sym.parent.empty()
                        ? sym.name
                        : (sym.parent + "::" + sym.name);
                    const std::string symbol_id = make_symbol_id(file.path, sym.kind, qualified_name, sym.line);
                    known_type_symbol_ids[sym.name].push_back(symbol_id);
                    if (sym.is_abstract)
                        abstract_symbol_ids.insert(symbol_id);
                    if (!sym.inherited_types.empty())
                        direct_base_refs_by_symbol_id.emplace(symbol_id, sym.inherited_types);
                }
                if (sym.kind == SymbolKind::Function && !sym.parent.empty())
                {
                    types_with_methods.insert(sym.parent);
                    const int function_size = static_cast<int>(
                        sym.end_line >= sym.line ? (sym.end_line - sym.line + 1) : 1);
                    method_sizes_by_type[sym.parent].push_back(function_size);
                    method_names_by_type[sym.parent].push_back(sym.name);
                }
            }
        }
        const std::unordered_map<std::string, std::vector<std::string>> inheritance_descendants
            = build_inheritance_descendants(direct_base_refs_by_symbol_id, known_type_symbol_ids);

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
            "source_file_path, source_line, base_size, building_functions, building_function_sizes_json, "
            "building_function_names_json, road_size, "
            "height, footprint_x, footprint_y) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        Statement insert_field(impl_->db.get(),
            "INSERT INTO symbol_fields(field_id, symbol_id, field_name, field_type_name) VALUES(?, ?, ?, ?)");
        Statement insert_dependency(impl_->db.get(),
            "INSERT OR IGNORE INTO city_entity_dependencies(source_entity_id, target_entity_id, field_name, field_type_name, is_abstract_ref) "
            "VALUES(?, ?, ?, ?, ?)");

        struct PendingFieldRows
        {
            std::string symbol_id;
            std::vector<SymbolRecord::FieldRecord> fields;
        };
        std::vector<PendingFieldRows> pending_field_rows;

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
                        && !types_with_methods.contains(sym.name))
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
                std::vector<std::string> function_names;
                if (sym.kind == SymbolKind::Class || sym.kind == SymbolKind::Struct)
                {
                    auto it = method_sizes_by_type.find(sym.name);
                    if (it != method_sizes_by_type.end())
                        function_sizes = it->second;
                    auto name_it = method_names_by_type.find(sym.name);
                    if (name_it != method_names_by_type.end())
                        function_names = name_it->second;
                }
                const int building_functions = static_cast<int>(function_sizes.size());
                const std::string symbol_id = make_symbol_id(file.path, sym.kind, qualified_name, sym.line);
                std::unordered_set<std::string> dependency_targets;
                if ((sym.kind == SymbolKind::Class || sym.kind == SymbolKind::Struct) && !sym.fields.empty())
                {
                    for (const auto& field : sym.fields)
                    {
                        for (const auto& ref : field.referenced_types)
                        {
                            if (ref == sym.name)
                                continue;
                            const auto resolved = resolve_dependency_targets(
                                symbol_id,
                                ref,
                                known_type_symbol_ids,
                                inheritance_descendants,
                                abstract_symbol_ids);
                            dependency_targets.insert(resolved.symbol_ids.begin(), resolved.symbol_ids.end());
                        }
                    }
                }
                const int road_size = static_cast<int>(dependency_targets.size());

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

                if (!sym.fields.empty())
                    pending_field_rows.push_back({ symbol_id, sym.fields });

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
                insert_entity.bind_text(12, json_string_array(function_names));
                insert_entity.bind_int(13, road_size);
                insert_entity.bind_double(14, spec.height);
                insert_entity.bind_double(15, spec.footprint_x);
                insert_entity.bind_double(16, spec.footprint_y);
                insert_entity.step_done();
                ++entity_count;
            }
        }

        for (const auto& pending : pending_field_rows)
        {
            for (size_t field_index = 0; field_index < pending.fields.size(); ++field_index)
            {
                const auto& field = pending.fields[field_index];
                insert_field.reuse();
                insert_field.bind_text(1, make_field_id(pending.symbol_id, field.name, field_index));
                insert_field.bind_text(2, pending.symbol_id);
                insert_field.bind_text(3, field.name);
                insert_field.bind_text(4, field.type_name);
                insert_field.step_done();

                for (const auto& ref : field.referenced_types)
                {
                    const auto resolved = resolve_dependency_targets(
                        pending.symbol_id,
                        ref,
                        known_type_symbol_ids,
                        inheritance_descendants,
                        abstract_symbol_ids);
                    for (const std::string& target_symbol_id : resolved.symbol_ids)
                    {
                        insert_dependency.reuse();
                        insert_dependency.bind_text(1, pending.symbol_id);
                        insert_dependency.bind_text(2, target_symbol_id);
                        insert_dependency.bind_text(3, field.name);
                        insert_dependency.bind_text(4, field.type_name);
                        insert_dependency.bind_int(5, resolved.is_abstract_ref ? 1 : 0);
                        insert_dependency.step_done();
                    }
                }
            }
        }

        // Populate city_modules with aggregated quality metrics.
        exec(impl_->db.get(), "DELETE FROM city_modules");
        Statement insert_module(impl_->db.get(),
            "INSERT INTO city_modules(module_path, building_count, total_functions, "
            "total_function_lines, avg_function_size, quality, complexity, cohesion, coupling) "
            "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?)");

        // We need per-module function size lists, but SQL can't easily parse JSON arrays.
        // So group by module_path and accumulate from the per-entity JSON in a second pass.
        struct ModuleAgg
        {
            int building_count = 0;
            int total_functions = 0;
            int total_function_lines = 0;
            int total_fields = 0;
            int total_road_size = 0;
        };
        std::unordered_map<std::string, ModuleAgg> module_agg;

        {
            Statement entity_scan(impl_->db.get(),
                "SELECT module_path, building_functions, building_function_sizes_json, "
                "base_size, road_size "
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
                agg.total_fields += sqlite3_column_int(entity_scan.raw(), 3);
                agg.total_road_size += sqlite3_column_int(entity_scan.raw(), 4);
            }
        }

        for (const auto& [mp, agg] : module_agg)
        {
            const float avg_fn_size = agg.total_functions > 0
                ? static_cast<float>(agg.total_function_lines) / static_cast<float>(agg.total_functions)
                : 0.0f;

            // Complexity: smaller avg function size = higher score.
            // ~5 LOC → 0.67, ~10 LOC → 0.5, ~30 LOC → 0.25.
            const float complexity = agg.total_functions > 0
                ? 1.0f / (1.0f + avg_fn_size / 10.0f)
                : 0.5f;

            // Cohesion: method-to-field ratio per entity, averaged across the module.
            // A class with many methods relative to fields is well-encapsulated.
            // ratio = functions / max(fields, 1); score = ratio / (ratio + 1) to map to 0..1.
            const float avg_cohesion_ratio = agg.building_count > 0
                ? static_cast<float>(agg.total_functions)
                    / static_cast<float>(std::max(agg.total_fields, 1))
                : 0.0f;
            const float cohesion = agg.building_count > 0
                ? avg_cohesion_ratio / (avg_cohesion_ratio + 1.0f)
                : 0.5f;

            // Coupling: fewer external type dependencies = higher score.
            // avg_deps = total_road_size / building_count; score = 1 / (1 + avg_deps / 3).
            const float avg_deps = agg.building_count > 0
                ? static_cast<float>(agg.total_road_size) / static_cast<float>(agg.building_count)
                : 0.0f;
            const float coupling = agg.building_count > 0
                ? 1.0f / (1.0f + avg_deps / 3.0f)
                : 0.5f;

            // Legacy quality = complexity for backward compatibility.
            const float quality = complexity;

            insert_module.reuse();
            insert_module.bind_text(1, mp);
            insert_module.bind_int(2, agg.building_count);
            insert_module.bind_int(3, agg.total_functions);
            insert_module.bind_int(4, agg.total_function_lines);
            insert_module.bind_double(5, static_cast<double>(avg_fn_size));
            insert_module.bind_double(6, static_cast<double>(quality));
            insert_module.bind_double(7, static_cast<double>(complexity));
            insert_module.bind_double(8, static_cast<double>(cohesion));
            insert_module.bind_double(9, static_cast<double>(coupling));
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
    PERF_MEASURE();
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
    PERF_MEASURE();
    std::vector<CityClassRecord> rows;
    if (!impl_ || !impl_->db)
        return rows;

    try
    {
        Statement stmt(impl_->db.get(),
            "SELECT ce.display_name, s.name, ce.module_path, ce.source_file_path, ce.entity_kind, "
            "ce.base_size, ce.building_functions, ce.building_function_sizes_json, "
            "ce.building_function_names_json, ce.road_size, s.is_abstract, s.kind "
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
            row.function_names = parse_json_string_array(read_text(8));
            row.road_size = sqlite3_column_int(stmt.raw(), 9);
            row.is_abstract = sqlite3_column_int(stmt.raw(), 10) != 0;
            row.is_struct = read_text(11) == "struct";
            rows.push_back(std::move(row));
        }
    }
    catch (const std::exception& ex)
    {
        impl_->last_error = ex.what();
    }

    return rows;
}

std::vector<CityDependencyRecord> CityDatabase::list_class_dependencies_in_module(std::string_view module_path) const
{
    PERF_MEASURE();
    std::vector<CityDependencyRecord> rows;
    if (!impl_ || !impl_->db)
        return rows;

    try
    {
        Statement stmt(impl_->db.get(),
            "SELECT src.display_name, src.module_path, dep.field_name, dep.field_type_name, "
            "dst.display_name, dst.module_path, src.source_file_path, dst.source_file_path, "
            "dep.is_abstract_ref "
            "FROM city_entity_dependencies dep "
            "JOIN city_entities src ON src.entity_id = dep.source_entity_id "
            "JOIN city_entities dst ON dst.entity_id = dep.target_entity_id "
            "WHERE src.module_path = ? "
            "ORDER BY src.display_name, dep.field_name, dst.display_name");
        stmt.bind_text(1, module_path);
        while (stmt.step() == SQLITE_ROW)
        {
            CityDependencyRecord row;
            const auto read_text = [&](int index) -> std::string {
                const unsigned char* text = sqlite3_column_text(stmt.raw(), index);
                return text ? reinterpret_cast<const char*>(text) : "";
            };
            row.source_qualified_name = read_text(0);
            row.source_module_path = read_text(1);
            row.field_name = read_text(2);
            row.field_type_name = read_text(3);
            row.target_qualified_name = read_text(4);
            row.target_module_path = read_text(5);
            row.source_file_path = read_text(6);
            row.target_file_path = read_text(7);
            row.is_abstract_ref = sqlite3_column_int(stmt.raw(), 8) != 0;
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
    PERF_MEASURE();
    CityModuleRecord record;
    record.module_path = std::string(module_path);
    if (!impl_ || !impl_->db)
        return record;

    try
    {
        Statement stmt(impl_->db.get(),
            "SELECT building_count, total_functions, total_function_lines, avg_function_size, "
            "quality, complexity, cohesion, coupling "
            "FROM city_modules WHERE module_path = ?");
        stmt.bind_text(1, module_path);
        if (stmt.step() == SQLITE_ROW)
        {
            record.building_count = sqlite3_column_int(stmt.raw(), 0);
            record.total_functions = sqlite3_column_int(stmt.raw(), 1);
            record.total_function_lines = sqlite3_column_int(stmt.raw(), 2);
            record.avg_function_size = static_cast<float>(sqlite3_column_double(stmt.raw(), 3));
            record.quality = static_cast<float>(sqlite3_column_double(stmt.raw(), 4));
            record.health.complexity = static_cast<float>(sqlite3_column_double(stmt.raw(), 5));
            record.health.cohesion = static_cast<float>(sqlite3_column_double(stmt.raw(), 6));
            record.health.coupling = static_cast<float>(sqlite3_column_double(stmt.raw(), 7));
        }
    }
    catch (const std::exception& ex)
    {
        impl_->last_error = ex.what();
    }

    return record;
}

CodebaseHealthMetrics CityDatabase::codebase_health() const
{
    PERF_MEASURE();
    CodebaseHealthMetrics health;
    if (!impl_ || !impl_->db)
        return health;

    try
    {
        // Weighted average of per-module metrics, weighted by building_count so larger
        // modules contribute proportionally more to the global score.
        Statement stmt(impl_->db.get(),
            "SELECT SUM(building_count * complexity), SUM(building_count * cohesion), "
            "SUM(building_count * coupling), SUM(building_count) FROM city_modules");
        if (stmt.step() == SQLITE_ROW)
        {
            const double total_weight = sqlite3_column_double(stmt.raw(), 3);
            if (total_weight > 0.0)
            {
                health.complexity = static_cast<float>(sqlite3_column_double(stmt.raw(), 0) / total_weight);
                health.cohesion = static_cast<float>(sqlite3_column_double(stmt.raw(), 1) / total_weight);
                health.coupling = static_cast<float>(sqlite3_column_double(stmt.raw(), 2) / total_weight);
            }
        }
    }
    catch (const std::exception& ex)
    {
        impl_->last_error = ex.what();
    }

    return health;
}

} // namespace draxul
