#pragma once

#include <draxul/city_semantic_source.h>
#include <draxul/citydb.h>

#include <string_view>
#include <vector>

namespace draxul
{

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
