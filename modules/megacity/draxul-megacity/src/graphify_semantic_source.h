#pragma once

#include "city_semantic_source.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace draxul
{

std::string normalize_graphify_source_file(std::string_view graph_id, std::string_view source_file);
std::string module_path_for_graphify_source(std::string_view graph_id, std::string_view normalized_source_file);
std::string clean_graphify_symbol_label(std::string_view label);

class GraphifySemanticSource final : public ICitySemanticSource
{
public:
    bool load(const std::filesystem::path& path);

    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] const std::string& last_error() const;

    [[nodiscard]] std::vector<std::string> list_modules() const override;
    [[nodiscard]] CityModuleRecord module_record(std::string_view module_path) const override;
    [[nodiscard]] std::vector<CityClassRecord> list_classes_in_module(std::string_view module_path) const override;
    [[nodiscard]] std::vector<CityDependencyRecord> list_class_dependencies_in_module(std::string_view module_path) const override;
    [[nodiscard]] CodebaseHealthMetrics codebase_health() const override;

private:
    std::filesystem::path path_;
    std::string last_error_;
    std::vector<std::string> modules_;
    std::unordered_map<std::string, CityModuleRecord> module_records_;
    std::unordered_map<std::string, std::vector<CityClassRecord>> rows_by_module_;
    std::unordered_map<std::string, std::vector<CityDependencyRecord>> deps_by_module_;
    CodebaseHealthMetrics codebase_health_;
};

} // namespace draxul
