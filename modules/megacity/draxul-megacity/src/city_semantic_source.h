#pragma once

#include <draxul/citydb.h>

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
    [[nodiscard]] virtual std::vector<CityDependencyRecord> list_class_dependencies_in_module(std::string_view module_path) const = 0;
    [[nodiscard]] virtual CodebaseHealthMetrics codebase_health() const = 0;
};

class CityDatabaseSemanticSource final : public ICitySemanticSource
{
public:
    explicit CityDatabaseSemanticSource(CityDatabase& db);

    [[nodiscard]] std::vector<std::string> list_modules() const override;
    [[nodiscard]] CityModuleRecord module_record(std::string_view module_path) const override;
    [[nodiscard]] std::vector<CityClassRecord> list_classes_in_module(std::string_view module_path) const override;
    [[nodiscard]] std::vector<CityDependencyRecord> list_class_dependencies_in_module(std::string_view module_path) const override;
    [[nodiscard]] CodebaseHealthMetrics codebase_health() const override;

private:
    CityDatabase& db_;
};

} // namespace draxul
