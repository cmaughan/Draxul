#pragma once

#include <string>
#include <vector>

namespace draxul
{

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
    float complexity = 0.5f;
    float cohesion = 0.5f;
    float coupling = 0.5f;
};

struct CityModuleRecord
{
    std::string module_path;
    int building_count = 0;
    int total_functions = 0;
    int total_function_lines = 0;
    float avg_function_size = 0.0f;
    float quality = 0.5f;
    CodebaseHealthMetrics health;
};

} // namespace draxul
