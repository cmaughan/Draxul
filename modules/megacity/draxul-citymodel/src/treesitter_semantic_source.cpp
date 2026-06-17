#include <draxul/perf_timing.h>
#include <draxul/treesitter_semantic_source.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>

namespace draxul
{
namespace
{

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

struct EntitySpec
{
    const char* entity_kind = "";
};

[[nodiscard]] EntitySpec entity_spec(CityRole role)
{
    switch (role)
    {
    case CityRole::ConcreteClass:
        return { "building" };
    case CityRole::AbstractClass:
        return { "tower" };
    case CityRole::DataStruct:
        return { "block" };
    case CityRole::FreeFunction:
        return { "tree" };
    case CityRole::Method:
    case CityRole::Include:
        return {};
    }
    return {};
}

std::string module_path_for_file(std::string_view file_path)
{
    PERF_MEASURE();
    const std::filesystem::path path(file_path);
    auto it = path.begin();
    if (it == path.end())
        return {};
    const std::string first = it->string();
    ++it;
    if (first == "libs" && it != path.end())
        return (std::filesystem::path(first) / *it).generic_string();
    if (it == path.end() && path.has_extension())
        return ".";
    return first;
}

[[nodiscard]] std::string make_symbol_id(
    const std::string& path, SymbolKind kind, std::string_view qualified_name, uint32_t line)
{
    return path + "|" + symbol_kind_name(kind) + "|" + std::string(qualified_name) + "|"
        + std::to_string(line);
}

std::unordered_map<std::string, std::vector<std::string>> build_inheritance_descendants(
    const std::unordered_map<std::string, std::vector<std::string>>& direct_base_refs_by_symbol_id,
    const std::unordered_map<std::string, std::vector<std::string>>& known_type_symbol_ids)
{
    PERF_MEASURE();
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
            {
                for (const std::string& child : it->second)
                    frontier.push_back(child);
            }
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
    const std::string& source_symbol_id,
    const std::string& referenced_type_name,
    const std::unordered_map<std::string, std::vector<std::string>>& known_type_symbol_ids,
    const std::unordered_map<std::string, std::vector<std::string>>& inheritance_descendants,
    const std::unordered_set<std::string>& abstract_symbol_ids)
{
    PERF_MEASURE();
    const auto it = known_type_symbol_ids.find(referenced_type_name);
    if (it == known_type_symbol_ids.end() || it->second.size() != 1)
        return {};

    const std::string& direct_target_symbol_id = it->second.front();
    const bool is_abstract = abstract_symbol_ids.contains(direct_target_symbol_id);
    std::vector<std::string> targets;

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

struct ModuleAgg
{
    int building_count = 0;
    int total_functions = 0;
    int total_function_lines = 0;
    int total_fields = 0;
    int total_road_size = 0;
};

struct EntityInfo
{
    std::string symbol_id;
    std::string qualified_name;
    std::string module_path;
    std::string source_file_path;
};

struct PendingDependency
{
    std::string source_symbol_id;
    std::string field_name;
    std::string field_type_name;
    std::string target_symbol_id;
    bool is_abstract_ref = false;
};

} // namespace

TreeSitterSemanticSource::TreeSitterSemanticSource(const CodebaseSnapshot& snapshot)
{
    PERF_MEASURE();
    std::unordered_set<std::string> types_with_methods;
    std::unordered_map<std::string, std::vector<int>> method_sizes_by_type;
    std::unordered_map<std::string, std::vector<std::string>> method_names_by_type;
    std::unordered_map<std::string, int> method_line_totals_by_type;
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
                method_line_totals_by_type[sym.parent] += function_size;
            }
        }
    }

    const auto inheritance_descendants
        = build_inheritance_descendants(direct_base_refs_by_symbol_id, known_type_symbol_ids);

    std::unordered_map<std::string, ModuleAgg> module_agg;
    std::unordered_map<std::string, EntityInfo> entities_by_symbol_id;
    std::vector<PendingDependency> pending_dependencies;

    for (const auto& file : snapshot.files)
    {
        const std::string module_path = module_path_for_file(file.path);

        for (const auto& sym : file.symbols)
        {
            CityRole role = CityRole::Include;
            if (sym.kind == SymbolKind::Function)
                role = sym.parent.empty() ? CityRole::FreeFunction : CityRole::Method;
            else if (sym.kind == SymbolKind::Class || sym.kind == SymbolKind::Struct)
            {
                if (sym.is_abstract)
                    role = CityRole::AbstractClass;
                else if (sym.kind == SymbolKind::Struct && !types_with_methods.contains(sym.name))
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
            const std::vector<int>* function_sizes = nullptr;
            const std::vector<std::string>* function_names = nullptr;
            int function_line_total = 0;
            if (sym.kind == SymbolKind::Class || sym.kind == SymbolKind::Struct)
            {
                auto it = method_sizes_by_type.find(sym.name);
                if (it != method_sizes_by_type.end())
                    function_sizes = &it->second;
                auto name_it = method_names_by_type.find(sym.name);
                if (name_it != method_names_by_type.end())
                    function_names = &name_it->second;
                auto total_it = method_line_totals_by_type.find(sym.name);
                if (total_it != method_line_totals_by_type.end())
                    function_line_total = total_it->second;
            }
            const int building_functions = function_sizes ? static_cast<int>(function_sizes->size()) : 0;
            const std::string symbol_id = make_symbol_id(file.path, sym.kind, qualified_name, sym.line);
            std::unordered_set<std::string> dependency_targets;
            const bool is_free_function = sym.kind == SymbolKind::Function && sym.parent.empty();
            if ((sym.kind == SymbolKind::Class || sym.kind == SymbolKind::Struct || is_free_function)
                && !sym.fields.empty())
            {
                for (const auto& field : sym.fields)
                {
                    for (const std::string& ref : field.referenced_types)
                    {
                        if (ref == sym.name)
                            continue;
                        const auto resolved = resolve_dependency_targets(
                            symbol_id,
                            ref,
                            known_type_symbol_ids,
                            inheritance_descendants,
                            abstract_symbol_ids);
                        for (const std::string& target_symbol_id : resolved.symbol_ids)
                        {
                            dependency_targets.insert(target_symbol_id);
                            pending_dependencies.push_back(PendingDependency{
                                symbol_id,
                                field.name,
                                field.type_name,
                                target_symbol_id,
                                resolved.is_abstract_ref,
                            });
                        }
                    }
                }
            }
            const int road_size = static_cast<int>(dependency_targets.size());

            if (role == CityRole::Method || role == CityRole::Include)
                continue;

            const EntitySpec spec = entity_spec(role);
            CityClassRecord row;
            row.name = sym.name;
            row.qualified_name = qualified_name;
            row.module_path = module_path;
            row.source_file_path = file.path;
            row.entity_kind = spec.entity_kind;
            row.is_struct = sym.kind == SymbolKind::Struct && building_functions == 0;
            row.base_size = base_size;
            row.building_functions = building_functions;
            row.function_sizes = function_sizes ? *function_sizes : std::vector<int>{};
            row.function_names = function_names ? *function_names : std::vector<std::string>{};
            row.road_size = road_size;
            row.is_abstract = sym.is_abstract;

            if (row.entity_kind == "tree")
            {
                const int line_count = std::max(1, static_cast<int>(sym.end_line) - static_cast<int>(sym.line));
                row.building_functions = 1;
                row.function_sizes = { line_count };
                row.function_names = { row.name };
            }

            classes_by_module_[module_path].push_back(std::move(row));
            entities_by_symbol_id.emplace(symbol_id, EntityInfo{
                symbol_id,
                qualified_name,
                module_path,
                file.path,
            });

            if (role == CityRole::ConcreteClass || role == CityRole::AbstractClass || role == CityRole::DataStruct)
            {
                auto& agg = module_agg[module_path];
                ++agg.building_count;
                agg.total_functions += building_functions;
                agg.total_function_lines += function_line_total;
                agg.total_fields += base_size;
                agg.total_road_size += road_size;
            }
        }
    }

    std::set<std::tuple<std::string, std::string, std::string, std::string>> dependency_keys;
    for (const auto& pending : pending_dependencies)
    {
        const auto source_it = entities_by_symbol_id.find(pending.source_symbol_id);
        const auto target_it = entities_by_symbol_id.find(pending.target_symbol_id);
        if (source_it == entities_by_symbol_id.end() || target_it == entities_by_symbol_id.end())
            continue;

        const auto key = std::make_tuple(
            pending.source_symbol_id,
            pending.target_symbol_id,
            pending.field_name,
            pending.field_type_name);
        if (!dependency_keys.insert(key).second)
            continue;

        const EntityInfo& source = source_it->second;
        const EntityInfo& target = target_it->second;
        CityDependencyRecord dependency;
        dependency.source_qualified_name = source.qualified_name;
        dependency.source_module_path = source.module_path;
        dependency.field_name = pending.field_name;
        dependency.field_type_name = pending.field_type_name;
        dependency.target_qualified_name = target.qualified_name;
        dependency.target_module_path = target.module_path;
        dependency.source_file_path = source.source_file_path;
        dependency.target_file_path = target.source_file_path;
        dependency.is_abstract_ref = pending.is_abstract_ref;
        dependencies_by_module_[source.module_path].push_back(std::move(dependency));
    }

    for (const auto& [module_path, rows] : classes_by_module_)
        modules_.push_back(module_path);
    std::sort(modules_.begin(), modules_.end());

    for (auto& [module_path, rows] : classes_by_module_)
    {
        std::sort(rows.begin(), rows.end(), [](const CityClassRecord& a, const CityClassRecord& b) {
            return a.qualified_name < b.qualified_name;
        });
    }

    for (auto& [module_path, deps] : dependencies_by_module_)
    {
        std::sort(deps.begin(), deps.end(), [](const CityDependencyRecord& a, const CityDependencyRecord& b) {
            return std::tie(a.source_qualified_name, a.field_name, a.target_qualified_name)
                < std::tie(b.source_qualified_name, b.field_name, b.target_qualified_name);
        });
    }

    float weighted_complexity = 0.0f;
    float weighted_cohesion = 0.0f;
    float weighted_coupling = 0.0f;
    int total_weight = 0;
    for (const auto& [module_path, agg] : module_agg)
    {
        CityModuleRecord record;
        record.module_path = module_path;
        record.building_count = agg.building_count;
        record.total_functions = agg.total_functions;
        record.total_function_lines = agg.total_function_lines;
        record.avg_function_size = agg.total_functions > 0
            ? static_cast<float>(agg.total_function_lines) / static_cast<float>(agg.total_functions)
            : 0.0f;
        record.health.complexity = agg.total_functions > 0
            ? 1.0f / (1.0f + record.avg_function_size / 10.0f)
            : 0.5f;
        const float avg_cohesion_ratio = agg.building_count > 0
            ? static_cast<float>(agg.total_functions) / static_cast<float>(std::max(agg.total_fields, 1))
            : 0.0f;
        record.health.cohesion = agg.building_count > 0
            ? avg_cohesion_ratio / (avg_cohesion_ratio + 1.0f)
            : 0.5f;
        const float avg_deps = agg.building_count > 0
            ? static_cast<float>(agg.total_road_size) / static_cast<float>(agg.building_count)
            : 0.0f;
        record.health.coupling = agg.building_count > 0
            ? 1.0f / (1.0f + avg_deps / 3.0f)
            : 0.5f;
        record.quality = record.health.complexity;

        weighted_complexity += static_cast<float>(record.building_count) * record.health.complexity;
        weighted_cohesion += static_cast<float>(record.building_count) * record.health.cohesion;
        weighted_coupling += static_cast<float>(record.building_count) * record.health.coupling;
        total_weight += record.building_count;
        module_records_[module_path] = record;
    }

    if (total_weight > 0)
    {
        health_.complexity = weighted_complexity / static_cast<float>(total_weight);
        health_.cohesion = weighted_cohesion / static_cast<float>(total_weight);
        health_.coupling = weighted_coupling / static_cast<float>(total_weight);
    }
}

std::vector<std::string> TreeSitterSemanticSource::list_modules() const
{
    return modules_;
}

CityModuleRecord TreeSitterSemanticSource::module_record(std::string_view module_path) const
{
    const auto it = module_records_.find(std::string(module_path));
    if (it != module_records_.end())
        return it->second;

    CityModuleRecord record;
    record.module_path = std::string(module_path);
    return record;
}

std::vector<CityClassRecord> TreeSitterSemanticSource::list_classes_in_module(std::string_view module_path) const
{
    const auto it = classes_by_module_.find(std::string(module_path));
    if (it == classes_by_module_.end())
        return {};
    return it->second;
}

std::vector<CityDependencyRecord> TreeSitterSemanticSource::list_class_dependencies_in_module(
    std::string_view module_path) const
{
    const auto it = dependencies_by_module_.find(std::string(module_path));
    if (it == dependencies_by_module_.end())
        return {};
    return it->second;
}

CodebaseHealthMetrics TreeSitterSemanticSource::codebase_health() const
{
    return health_;
}

} // namespace draxul
