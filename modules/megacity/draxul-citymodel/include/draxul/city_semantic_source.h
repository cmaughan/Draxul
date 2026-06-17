#pragma once

#include <draxul/city_semantic_records.h>

#include <string_view>
#include <vector>

namespace draxul
{

class ICitySemanticSource
{
public:
    virtual ~ICitySemanticSource() = default;

    [[nodiscard]] virtual std::vector<std::string> list_modules() const = 0;
    [[nodiscard]] virtual CityModuleRecord module_record(std::string_view module_path) const = 0;
    [[nodiscard]] virtual std::vector<CityClassRecord> list_classes_in_module(std::string_view module_path) const = 0;
    [[nodiscard]] virtual std::vector<CityDependencyRecord> list_class_dependencies_in_module(
        std::string_view module_path) const = 0;
    [[nodiscard]] virtual CodebaseHealthMetrics codebase_health() const = 0;
};

} // namespace draxul
