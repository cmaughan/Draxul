#pragma once

#include <draxul/city_semantic_records.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

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
