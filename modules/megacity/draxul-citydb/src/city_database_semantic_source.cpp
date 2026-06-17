#include <draxul/city_database_semantic_source.h>

namespace draxul
{

CityDatabaseSemanticSource::CityDatabaseSemanticSource(CityDatabase& db)
    : db_(db)
{
}

std::vector<std::string> CityDatabaseSemanticSource::list_modules() const
{
    return db_.is_open() ? db_.list_modules() : std::vector<std::string>{};
}

CityModuleRecord CityDatabaseSemanticSource::module_record(std::string_view module_path) const
{
    return db_.module_record(module_path);
}

std::vector<CityClassRecord> CityDatabaseSemanticSource::list_classes_in_module(std::string_view module_path) const
{
    return db_.list_classes_in_module(module_path);
}

std::vector<CityDependencyRecord> CityDatabaseSemanticSource::list_class_dependencies_in_module(
    std::string_view module_path) const
{
    return db_.list_class_dependencies_in_module(module_path);
}

CodebaseHealthMetrics CityDatabaseSemanticSource::codebase_health() const
{
    return db_.codebase_health();
}

} // namespace draxul
