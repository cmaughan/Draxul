#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

struct CodebaseSnapshot;

struct CityDbStats
{
    std::filesystem::path path;
    size_t file_count = 0;
    size_t symbol_count = 0;
    size_t city_entity_count = 0;
    std::chrono::steady_clock::time_point last_reconcile_time{};
    bool has_reconciled_snapshot = false;
};

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
    float complexity = 0.5f; // 0..1, higher = smaller avg function size
    float cohesion = 0.5f; // 0..1, higher = better method-to-field ratio
    float coupling = 0.5f; // 0..1, higher = fewer external dependencies
};

struct CityModuleRecord
{
    std::string module_path;
    int building_count = 0;
    int total_functions = 0;
    int total_function_lines = 0;
    float avg_function_size = 0.0f;
    float quality = 0.5f; // 0..1, higher = better-factored code (legacy, == complexity)
    CodebaseHealthMetrics health;
};

class CityDatabase
{
public:
    CityDatabase();
    ~CityDatabase();

    CityDatabase(const CityDatabase&) = delete;
    CityDatabase& operator=(const CityDatabase&) = delete;

    CityDatabase(CityDatabase&&) noexcept;
    CityDatabase& operator=(CityDatabase&&) noexcept;

    bool open(const std::filesystem::path& path);
    void close();

    [[nodiscard]] bool is_open() const;
    [[nodiscard]] bool schema_migrated() const;
    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] const std::string& last_error() const;
    [[nodiscard]] const CityDbStats& stats() const;

    bool reconcile_snapshot(const CodebaseSnapshot& snapshot);
    [[nodiscard]] std::vector<std::string> list_modules() const;
    [[nodiscard]] std::vector<CityClassRecord> list_classes_in_module(std::string_view module_path) const;
    [[nodiscard]] std::vector<CityDependencyRecord> list_class_dependencies_in_module(std::string_view module_path) const;
    [[nodiscard]] CityModuleRecord module_record(std::string_view module_path) const;
    [[nodiscard]] CodebaseHealthMetrics codebase_health() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace draxul
