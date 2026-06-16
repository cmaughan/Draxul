#include "graphify_semantic_source.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <numeric>
#include <set>
#include <utility>

namespace draxul
{
namespace
{

using json = nlohmann::json;

struct GraphNode
{
    std::string id;
    std::string label;
    std::string normalized_source_file;
    std::string module_path;
};

struct GraphLink
{
    std::string source;
    std::string target;
    std::string relation;
    std::string context;
    std::string source_file;
};

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix)
{
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::string json_string_field(const json& object, const char* key)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string())
        return {};
    return it->get<std::string>();
}

[[nodiscard]] std::vector<std::string_view> split_path_components(std::string_view path)
{
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (start <= path.size())
    {
        const size_t end = path.find('/', start);
        const size_t count = (end == std::string_view::npos) ? path.size() - start : end - start;
        if (count > 0)
            parts.push_back(path.substr(start, count));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return parts;
}

[[nodiscard]] const GraphNode* find_node(
    const std::unordered_map<std::string, GraphNode>& nodes, std::string_view id)
{
    const auto it = nodes.find(std::string(id));
    return it == nodes.end() ? nullptr : &it->second;
}

[[nodiscard]] std::string fallback_label_for_id(std::string_view id)
{
    const size_t pos = id.rfind("::");
    if (pos != std::string_view::npos && pos + 2 < id.size())
        return std::string(id.substr(pos + 2));
    return std::string(id);
}

[[nodiscard]] std::string node_label_or_id(const GraphNode* node, std::string_view id)
{
    if (node && !node->label.empty())
        return node->label;
    return fallback_label_for_id(id);
}

[[nodiscard]] std::string node_source_file_or_link(
    const GraphNode* node, std::string_view id, std::string_view link_source_file)
{
    if (node && !node->normalized_source_file.empty())
        return node->normalized_source_file;
    return normalize_graphify_source_file(id, link_source_file);
}

[[nodiscard]] std::string node_module_or_source(
    const GraphNode* node, std::string_view id, std::string_view normalized_source_file)
{
    if (node && !node->module_path.empty())
        return node->module_path;
    return module_path_for_graphify_source(id, normalized_source_file);
}

[[nodiscard]] std::string field_target_name(const GraphNode* target_node, std::string_view target_id)
{
    const std::string label = clean_graphify_symbol_label(node_label_or_id(target_node, target_id));
    return label.empty() ? fallback_label_for_id(target_id) : label;
}

[[nodiscard]] CityDependencyRecord make_class_dependency_record(const GraphNode* source_node,
    std::string_view source_id, const GraphNode* target_node, std::string_view target_id,
    std::string_view source_fallback_file, std::string_view target_fallback_file, std::string field_name,
    std::string field_type_name)
{
    const std::string source_file = node_source_file_or_link(source_node, source_id, source_fallback_file);
    const std::string target_file = node_source_file_or_link(target_node, target_id, target_fallback_file);

    CityDependencyRecord dep;
    dep.source_qualified_name = std::string(source_id);
    dep.source_module_path = node_module_or_source(source_node, source_id, source_file);
    dep.field_name = std::move(field_name);
    dep.field_type_name = std::move(field_type_name);
    dep.target_qualified_name = std::string(target_id);
    dep.target_module_path = node_module_or_source(target_node, target_id, target_file);
    dep.source_file_path = source_file;
    dep.target_file_path = target_file;
    return dep;
}

[[nodiscard]] CodebaseHealthMetrics module_health(int building_count, int total_functions, int total_function_lines,
    int total_fields, int total_road_size)
{
    CodebaseHealthMetrics health;
    if (building_count <= 0)
        return health;

    if (total_functions > 0)
    {
        const float avg_function_size = static_cast<float>(total_function_lines) / static_cast<float>(total_functions);
        health.complexity = 1.0f / (1.0f + avg_function_size / 10.0f);
    }

    const float method_field_ratio = static_cast<float>(total_functions) / static_cast<float>(std::max(total_fields, 1));
    health.cohesion = method_field_ratio / (1.0f + method_field_ratio);

    const float avg_deps = static_cast<float>(total_road_size) / static_cast<float>(building_count);
    health.coupling = 1.0f / (1.0f + avg_deps / 3.0f);

    return health;
}

void sort_records(std::unordered_map<std::string, std::vector<CityClassRecord>>& rows_by_module,
    std::unordered_map<std::string, std::vector<CityDependencyRecord>>& deps_by_module)
{
    for (auto& [_, rows] : rows_by_module)
    {
        std::sort(rows.begin(), rows.end(), [](const CityClassRecord& lhs, const CityClassRecord& rhs) {
            if (lhs.name != rhs.name)
                return lhs.name < rhs.name;
            return lhs.qualified_name < rhs.qualified_name;
        });
    }

    for (auto& [_, deps] : deps_by_module)
    {
        std::sort(deps.begin(), deps.end(), [](const CityDependencyRecord& lhs, const CityDependencyRecord& rhs) {
            if (lhs.source_qualified_name != rhs.source_qualified_name)
                return lhs.source_qualified_name < rhs.source_qualified_name;
            if (lhs.field_name != rhs.field_name)
                return lhs.field_name < rhs.field_name;
            return lhs.target_qualified_name < rhs.target_qualified_name;
        });
    }
}

} // namespace

std::string normalize_graphify_source_file(std::string_view graph_id, std::string_view source_file)
{
    std::string normalized(source_file);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    if (normalized.empty())
        return normalized;

    if (starts_with(graph_id, "app::") && normalized.find('/') == std::string::npos)
        return "app/" + normalized;

    if (starts_with(graph_id, "modules::") && starts_with(normalized, "megacity/"))
        return "modules/" + normalized;

    return normalized;
}

std::string module_path_for_graphify_source(std::string_view graph_id, std::string_view normalized_source_file)
{
    const std::vector<std::string_view> parts = split_path_components(normalized_source_file);
    if (parts.size() >= 2 && parts[0] == "libs")
        return std::string(parts[0]) + "/" + std::string(parts[1]);

    if (parts.size() >= 3 && parts[0] == "modules" && parts[1] == "megacity")
        return std::string(parts[0]) + "/" + std::string(parts[1]) + "/" + std::string(parts[2]);

    if (starts_with(graph_id, "app::"))
        return "app";

    if (!parts.empty())
        return std::string(parts[0]);

    return ".";
}

std::string clean_graphify_symbol_label(std::string_view label)
{
    while (!label.empty() && label.front() == '.')
        label.remove_prefix(1);

    if (label.size() >= 2 && label.substr(label.size() - 2) == "()")
        label.remove_suffix(2);

    return std::string(label);
}

bool GraphifySemanticSource::load(const std::filesystem::path& path)
{
    path_ = path;
    last_error_.clear();
    modules_.clear();
    module_records_.clear();
    rows_by_module_.clear();
    deps_by_module_.clear();
    codebase_health_ = {};

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        last_error_ = "failed to open Graphify JSON file: " + path.string();
        return false;
    }

    json graph;
    try
    {
        in >> graph;
    }
    catch (const std::exception& ex)
    {
        last_error_ = "failed to parse Graphify JSON file: " + std::string(ex.what());
        return false;
    }

    try
    {
        std::unordered_map<std::string, GraphNode> nodes;
        if (const auto node_it = graph.find("nodes"); node_it != graph.end() && node_it->is_array())
        {
            for (const json& node_json : *node_it)
            {
                if (!node_json.is_object())
                    continue;

                GraphNode node;
                node.id = json_string_field(node_json, "id");
                if (node.id.empty())
                    continue;

                node.label = json_string_field(node_json, "label");
                node.normalized_source_file = normalize_graphify_source_file(
                    node.id, json_string_field(node_json, "source_file"));
                node.module_path = module_path_for_graphify_source(node.id, node.normalized_source_file);
                nodes.emplace(node.id, std::move(node));
            }
        }

        std::vector<GraphLink> links;
        if (const auto link_it = graph.find("links"); link_it != graph.end() && link_it->is_array())
        {
            for (const json& link_json : *link_it)
            {
                if (!link_json.is_object())
                    continue;

                GraphLink link;
                link.source = json_string_field(link_json, "source");
                link.target = json_string_field(link_json, "target");
                link.relation = json_string_field(link_json, "relation");
                link.context = json_string_field(link_json, "context");
                link.source_file = json_string_field(link_json, "source_file");
                if (!link.source.empty() && !link.target.empty())
                    links.push_back(std::move(link));
            }
        }

        std::set<std::string> class_like_ids;
        for (const GraphLink& link : links)
        {
            if (link.relation == "method" || link.relation == "inherits"
                || (link.relation == "defines" && link.context == "field")
                || (link.relation == "references" && link.context == "field"))
            {
                class_like_ids.insert(link.source);
            }
        }

        std::unordered_map<std::string, std::string> method_owner_by_method;
        for (const GraphLink& link : links)
        {
            if (link.relation == "method")
                method_owner_by_method[link.target] = link.source;
        }

        std::unordered_map<std::string, int> call_dependency_counts_by_owner;
        for (const GraphLink& link : links)
        {
            if (link.relation != "calls")
                continue;

            const auto source_owner_it = method_owner_by_method.find(link.source);
            const auto target_owner_it = method_owner_by_method.find(link.target);
            if (source_owner_it == method_owner_by_method.end() || target_owner_it == method_owner_by_method.end())
                continue;

            const std::string& source_owner_id = source_owner_it->second;
            const std::string& target_owner_id = target_owner_it->second;
            if (source_owner_id == target_owner_id)
                continue;

            const GraphNode* source_owner_node = find_node(nodes, source_owner_id);
            const GraphNode* target_owner_node = find_node(nodes, target_owner_id);
            CityDependencyRecord dep = make_class_dependency_record(source_owner_node,
                source_owner_id,
                target_owner_node,
                target_owner_id,
                link.source_file,
                std::string_view{},
                {},
                {});
            const std::string source_module_path = dep.source_module_path;
            deps_by_module_[source_module_path].push_back(std::move(dep));
            ++call_dependency_counts_by_owner[source_owner_id];
        }

        std::unordered_map<std::string, std::vector<const GraphLink*>> outgoing_by_source;
        for (const GraphLink& link : links)
            outgoing_by_source[link.source].push_back(&link);

        std::set<std::string> module_names;
        for (const std::string& id : class_like_ids)
        {
            const GraphNode* source_node = find_node(nodes, id);
            const auto outgoing_it = outgoing_by_source.find(id);
            const std::string fallback_source_file
                = (outgoing_it != outgoing_by_source.end() && !outgoing_it->second.empty())
                ? outgoing_it->second.front()->source_file
                : std::string();

            CityClassRecord row;
            row.name = clean_graphify_symbol_label(node_label_or_id(source_node, id));
            row.qualified_name = id;
            row.source_file_path = node_source_file_or_link(source_node, id, fallback_source_file);
            row.module_path = node_module_or_source(source_node, id, row.source_file_path);
            row.entity_kind = "building";

            if (outgoing_it != outgoing_by_source.end())
            {
                for (const GraphLink* link : outgoing_it->second)
                {
                    const GraphNode* target_node = find_node(nodes, link->target);
                    if (link->relation == "method")
                    {
                        row.function_names.push_back(
                            clean_graphify_symbol_label(node_label_or_id(target_node, link->target)));
                        row.function_sizes.push_back(1);
                    }
                    else if (link->relation == "defines" && link->context == "field")
                    {
                        ++row.base_size;
                    }
                    else if (link->relation == "references" && link->context == "field")
                    {
                        if (!class_like_ids.contains(link->target))
                            continue;

                        ++row.road_size;

                        const std::string target_file
                            = node_source_file_or_link(target_node, link->target, link->source_file);
                        CityDependencyRecord dep;
                        dep.source_qualified_name = row.qualified_name;
                        dep.source_module_path = row.module_path;
                        dep.field_name = field_target_name(target_node, link->target);
                        dep.field_type_name = dep.field_name;
                        dep.target_qualified_name = link->target;
                        dep.target_module_path = node_module_or_source(target_node, link->target, target_file);
                        dep.source_file_path = row.source_file_path;
                        dep.target_file_path = target_file;
                        deps_by_module_[row.module_path].push_back(std::move(dep));
                    }
                    else if (link->relation == "inherits")
                    {
                        ++row.road_size;
                        if (link->target != row.qualified_name)
                        {
                            const GraphNode* target_node = find_node(nodes, link->target);
                            CityDependencyRecord dep = make_class_dependency_record(source_node,
                                row.qualified_name,
                                target_node,
                                link->target,
                                row.source_file_path,
                                std::string_view{},
                                {},
                                {});
                            dep.is_abstract_ref = true;
                            deps_by_module_[row.module_path].push_back(std::move(dep));
                        }
                    }
                }
            }

            if (const auto call_count_it = call_dependency_counts_by_owner.find(id);
                call_count_it != call_dependency_counts_by_owner.end())
            {
                row.road_size += call_count_it->second;
            }

            row.building_functions = static_cast<int>(row.function_names.size());
            module_names.insert(row.module_path);
            rows_by_module_[row.module_path].push_back(std::move(row));
        }

        sort_records(rows_by_module_, deps_by_module_);

        int global_weight = 0;
        float weighted_complexity = 0.0f;
        float weighted_cohesion = 0.0f;
        float weighted_coupling = 0.0f;

        for (const std::string& module_name : module_names)
        {
            const auto& rows = rows_by_module_[module_name];

            CityModuleRecord record;
            record.module_path = module_name;
            record.building_count = static_cast<int>(rows.size());
            int total_fields = 0;
            int total_road_size = 0;
            for (const CityClassRecord& row : rows)
            {
                record.total_functions += row.building_functions;
                record.total_function_lines += std::accumulate(row.function_sizes.begin(), row.function_sizes.end(), 0);
                total_fields += row.base_size;
                total_road_size += row.road_size;
            }
            if (record.total_functions > 0)
            {
                record.avg_function_size
                    = static_cast<float>(record.total_function_lines) / static_cast<float>(record.total_functions);
            }
            record.health = module_health(record.building_count, record.total_functions, record.total_function_lines,
                total_fields, total_road_size);
            record.quality = record.health.complexity;

            global_weight += record.building_count;
            weighted_complexity += static_cast<float>(record.building_count) * record.health.complexity;
            weighted_cohesion += static_cast<float>(record.building_count) * record.health.cohesion;
            weighted_coupling += static_cast<float>(record.building_count) * record.health.coupling;

            module_records_[module_name] = record;
        }

        modules_.assign(module_names.begin(), module_names.end());

        if (global_weight > 0)
        {
            codebase_health_.complexity = weighted_complexity / static_cast<float>(global_weight);
            codebase_health_.cohesion = weighted_cohesion / static_cast<float>(global_weight);
            codebase_health_.coupling = weighted_coupling / static_cast<float>(global_weight);
        }
    }
    catch (const std::exception& ex)
    {
        last_error_ = "failed to map Graphify JSON file: " + std::string(ex.what());
        modules_.clear();
        module_records_.clear();
        rows_by_module_.clear();
        deps_by_module_.clear();
        codebase_health_ = {};
        return false;
    }

    return true;
}

const std::filesystem::path& GraphifySemanticSource::path() const
{
    return path_;
}

const std::string& GraphifySemanticSource::last_error() const
{
    return last_error_;
}

std::vector<std::string> GraphifySemanticSource::list_modules() const
{
    return modules_;
}

CityModuleRecord GraphifySemanticSource::module_record(std::string_view module_path) const
{
    const auto it = module_records_.find(std::string(module_path));
    if (it != module_records_.end())
        return it->second;

    CityModuleRecord record;
    record.module_path = std::string(module_path);
    return record;
}

std::vector<CityClassRecord> GraphifySemanticSource::list_classes_in_module(std::string_view module_path) const
{
    const auto it = rows_by_module_.find(std::string(module_path));
    return it == rows_by_module_.end() ? std::vector<CityClassRecord>{} : it->second;
}

std::vector<CityDependencyRecord> GraphifySemanticSource::list_class_dependencies_in_module(
    std::string_view module_path) const
{
    const auto it = deps_by_module_.find(std::string(module_path));
    return it == deps_by_module_.end() ? std::vector<CityDependencyRecord>{} : it->second;
}

CodebaseHealthMetrics GraphifySemanticSource::codebase_health() const
{
    return codebase_health_;
}

} // namespace draxul
