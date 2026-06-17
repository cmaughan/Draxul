#pragma once

#include <draxul/city_semantic_source.h>
#include <draxul/treesitter.h>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace draxul
{

class TreeSitterSemanticSource final : public ICitySemanticSource
{
public:
    explicit TreeSitterSemanticSource(const CodebaseSnapshot& snapshot);

    [[nodiscard]] std::vector<std::string> list_modules() const override;
    [[nodiscard]] CityModuleRecord module_record(std::string_view module_path) const override;
    [[nodiscard]] std::vector<CityClassRecord> list_classes_in_module(std::string_view module_path) const override;
    [[nodiscard]] std::vector<CityDependencyRecord> list_class_dependencies_in_module(
        std::string_view module_path) const override;
    [[nodiscard]] CodebaseHealthMetrics codebase_health() const override;

private:
    std::vector<std::string> modules_;
    std::unordered_map<std::string, CityModuleRecord> module_records_;
    std::unordered_map<std::string, std::vector<CityClassRecord>> classes_by_module_;
    std::unordered_map<std::string, std::vector<CityDependencyRecord>> dependencies_by_module_;
    CodebaseHealthMetrics health_;
};

} // namespace draxul
