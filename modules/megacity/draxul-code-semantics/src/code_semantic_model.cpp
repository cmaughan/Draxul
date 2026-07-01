#include <draxul/code_semantic_model.h>

#include <draxul/module_path_resolver.h>
#include <draxul/perf_timing.h>

#include <algorithm>
#include <string_view>
#include <tuple>
#include <unordered_map>

namespace draxul
{

namespace
{

constexpr uint64_t kFnvOffset = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

uint64_t stable_hash_append(uint64_t hash, std::string_view text)
{
    for (const char ch : text)
    {
        hash ^= static_cast<uint8_t>(ch);
        hash *= kFnvPrime;
    }
    return hash;
}

uint64_t stable_hash(std::string_view text)
{
    return stable_hash_append(kFnvOffset, text);
}

std::string node_kind_name(CodeSemanticNodeKind kind)
{
    switch (kind)
    {
    case CodeSemanticNodeKind::Repository:
        return "repository";
    case CodeSemanticNodeKind::Module:
        return "module";
    case CodeSemanticNodeKind::File:
        return "file";
    case CodeSemanticNodeKind::Type:
        return "type";
    case CodeSemanticNodeKind::Function:
        return "function";
    case CodeSemanticNodeKind::Method:
        return "method";
    case CodeSemanticNodeKind::Field:
        return "field";
    case CodeSemanticNodeKind::Include:
        return "include";
    }
    return "unknown";
}

CodeSemanticNodeId make_node_id(
    CodeSemanticNodeKind kind,
    std::string_view module_path,
    std::string_view source_file_path,
    std::string_view qualified_name,
    uint32_t line)
{
    std::string key;
    key.reserve(
        module_path.size() + source_file_path.size() + qualified_name.size() + 48u);
    key.append(node_kind_name(kind));
    key.push_back('|');
    key.append(module_path);
    key.push_back('|');
    key.append(source_file_path);
    key.push_back('|');
    key.append(qualified_name);
    key.push_back('|');
    key.append(std::to_string(line));
    return stable_hash(key);
}

CodeSemanticEdgeId make_edge_id(
    CodeSemanticEdgeKind kind,
    CodeSemanticNodeId source_id,
    CodeSemanticNodeId target_id,
    std::string_view label)
{
    std::string key;
    key.reserve(label.size() + 96u);
    key.append(std::to_string(static_cast<int>(kind)));
    key.push_back('|');
    key.append(std::to_string(source_id));
    key.push_back('|');
    key.append(std::to_string(target_id));
    key.push_back('|');
    key.append(label);
    return stable_hash(key);
}

CodeSemanticNodeKind node_kind_for_symbol(const SymbolRecord& symbol)
{
    if (symbol.kind == SymbolKind::Class || symbol.kind == SymbolKind::Struct)
        return CodeSemanticNodeKind::Type;
    if (symbol.kind == SymbolKind::Include)
        return CodeSemanticNodeKind::Include;
    if (symbol.kind == SymbolKind::Function && !symbol.parent.empty())
        return CodeSemanticNodeKind::Method;
    return CodeSemanticNodeKind::Function;
}

CodeSemanticTypeKind type_kind_for_symbol(const SymbolRecord& symbol)
{
    if (symbol.kind == SymbolKind::Class)
        return CodeSemanticTypeKind::Class;
    if (symbol.kind == SymbolKind::Struct)
        return CodeSemanticTypeKind::Struct;
    return CodeSemanticTypeKind::None;
}

std::string qualified_name_for_symbol(const SymbolRecord& symbol)
{
    if (symbol.parent.empty())
        return symbol.name;
    return symbol.parent + "::" + symbol.name;
}

uint32_t line_count_for_symbol(const SymbolRecord& symbol)
{
    if (symbol.end_line >= symbol.line && symbol.line > 0)
        return symbol.end_line - symbol.line + 1;
    return symbol.line > 0 ? 1u : 0u;
}

CodeSemanticNode make_node(
    CodeSemanticNodeId id,
    CodeSemanticNodeId parent_id,
    CodeSemanticNodeKind kind,
    std::string module_path,
    std::string name,
    std::string qualified_name,
    CodeSemanticSourceSpan source,
    CodeSemanticMetrics metrics = {},
    CodeSemanticTypeKind type_kind = CodeSemanticTypeKind::None,
    bool is_abstract = false)
{
    CodeSemanticNode node;
    node.id = id;
    node.parent_id = parent_id;
    node.kind = kind;
    node.module_path = std::move(module_path);
    node.name = std::move(name);
    node.qualified_name = std::move(qualified_name);
    node.source = std::move(source);
    node.metrics = metrics;
    node.type_kind = type_kind;
    node.is_abstract = is_abstract;
    return node;
}

CodeSemanticEdge make_edge(
    CodeSemanticEdgeKind kind,
    CodeSemanticNodeId source_id,
    CodeSemanticNodeId target_id,
    std::string label = {})
{
    CodeSemanticEdge edge;
    edge.id = make_edge_id(kind, source_id, target_id, label);
    edge.source_id = source_id;
    edge.target_id = target_id;
    edge.kind = kind;
    edge.label = std::move(label);
    return edge;
}

void append_contains_edge(
    std::vector<CodeSemanticEdge>& edges,
    CodeSemanticNodeId parent_id,
    CodeSemanticNodeId child_id)
{
    if (parent_id != 0 && child_id != 0)
        edges.push_back(make_edge(CodeSemanticEdgeKind::Contains, parent_id, child_id));
}

CodebaseHealthMetrics compute_health_metrics(const std::vector<CodeSemanticNode>& nodes, const std::vector<CodeSemanticEdge>& edges)
{
    CodebaseHealthMetrics health;
    uint32_t function_count = 0;
    uint32_t function_lines = 0;
    uint32_t type_count = 0;
    uint32_t member_count = 0;
    uint32_t reference_edge_count = 0;

    for (const CodeSemanticNode& node : nodes)
    {
        if (node.kind == CodeSemanticNodeKind::Function || node.kind == CodeSemanticNodeKind::Method)
        {
            ++function_count;
            function_lines += node.metrics.line_count;
        }
        else if (node.kind == CodeSemanticNodeKind::Type)
        {
            ++type_count;
            member_count += node.metrics.field_count + node.metrics.method_count;
        }
    }

    for (const CodeSemanticEdge& edge : edges)
    {
        if (edge.kind == CodeSemanticEdgeKind::ReferencesType || edge.kind == CodeSemanticEdgeKind::Inherits)
            ++reference_edge_count;
    }

    if (function_count > 0)
    {
        const float average_function_lines = static_cast<float>(function_lines) / static_cast<float>(function_count);
        health.complexity = 1.0f / (1.0f + average_function_lines / 10.0f);
    }

    if (type_count > 0)
    {
        const float average_members = static_cast<float>(member_count) / static_cast<float>(type_count);
        health.cohesion = average_members / (average_members + 1.0f);
        const float average_references = static_cast<float>(reference_edge_count) / static_cast<float>(type_count);
        health.coupling = 1.0f / (1.0f + average_references / 3.0f);
    }

    return health;
}

} // namespace

CodeSemanticSnapshot build_code_semantic_snapshot(const CodebaseSnapshot& snapshot)
{
    PERF_MEASURE();
    CodeSemanticSnapshot result;
    result.complete = snapshot.complete;

    const CodeSemanticNodeId repository_id = make_node_id(
        CodeSemanticNodeKind::Repository, {}, {}, "repository", 0);
    result.nodes.push_back(make_node(
        repository_id,
        0,
        CodeSemanticNodeKind::Repository,
        {},
        "repository",
        "repository",
        {}));

    std::unordered_map<std::string, CodeSemanticNodeId> module_nodes;
    std::unordered_map<std::string, CodeSemanticNodeId> file_nodes;
    std::unordered_map<std::string, CodeSemanticNodeId> symbol_nodes_by_key;
    std::unordered_map<std::string, std::vector<CodeSemanticNodeId>> type_nodes_by_name;

    auto module_id_for = [&](const std::string& module_path) {
        if (const auto it = module_nodes.find(module_path); it != module_nodes.end())
            return it->second;

        const CodeSemanticNodeId id = make_node_id(
            CodeSemanticNodeKind::Module, module_path, {}, module_path.empty() ? "." : module_path, 0);
        module_nodes.emplace(module_path, id);
        result.nodes.push_back(make_node(
            id,
            repository_id,
            CodeSemanticNodeKind::Module,
            module_path,
            module_path.empty() ? "." : module_path,
            module_path.empty() ? "." : module_path,
            {}));
        append_contains_edge(result.edges, repository_id, id);
        return id;
    };

    auto file_id_for = [&](const ParsedFile& file, const std::string& module_path) {
        if (const auto it = file_nodes.find(file.path); it != file_nodes.end())
            return it->second;

        const CodeSemanticNodeId module_id = module_id_for(module_path);
        const CodeSemanticNodeId id = make_node_id(
            CodeSemanticNodeKind::File, module_path, file.path, file.path, 0);
        file_nodes.emplace(file.path, id);
        result.nodes.push_back(make_node(
            id,
            module_id,
            CodeSemanticNodeKind::File,
            module_path,
            file.path,
            file.path,
            CodeSemanticSourceSpan{ file.path, 0, 0 }));
        append_contains_edge(result.edges, module_id, id);
        return id;
    };

    for (const ParsedFile& file : snapshot.files)
    {
        const std::string module_path = module_path_for_source_file(file.path);
        const CodeSemanticNodeId file_id = file_id_for(file, module_path);

        for (const SymbolRecord& symbol : file.symbols)
        {
            const CodeSemanticNodeKind kind = node_kind_for_symbol(symbol);
            const std::string qualified_name = qualified_name_for_symbol(symbol);
            const CodeSemanticNodeId id = make_node_id(
                kind, module_path, file.path, qualified_name, symbol.line);

            CodeSemanticNodeId parent_id = file_id;
            if (kind == CodeSemanticNodeKind::Method && !symbol.parent.empty())
            {
                const auto parent_it = symbol_nodes_by_key.find(module_path + "|" + file.path + "|" + symbol.parent);
                if (parent_it != symbol_nodes_by_key.end())
                    parent_id = parent_it->second;
            }

            CodeSemanticMetrics metrics;
            metrics.line_count = line_count_for_symbol(symbol);
            metrics.field_count = symbol.field_count;
            metrics.referenced_type_count = static_cast<uint32_t>(symbol.referenced_types.size());
            if (symbol.kind == SymbolKind::Class || symbol.kind == SymbolKind::Struct)
            {
                for (const SymbolRecord& candidate : file.symbols)
                {
                    if (candidate.kind == SymbolKind::Function && candidate.parent == symbol.name)
                        ++metrics.method_count;
                }
            }

            result.nodes.push_back(make_node(
                id,
                parent_id,
                kind,
                module_path,
                symbol.name,
                qualified_name,
                CodeSemanticSourceSpan{ file.path, symbol.line, symbol.end_line },
                metrics,
                type_kind_for_symbol(symbol),
                symbol.is_abstract));
            append_contains_edge(result.edges, parent_id, id);
            symbol_nodes_by_key.emplace(module_path + "|" + file.path + "|" + qualified_name, id);
            if (kind == CodeSemanticNodeKind::Type)
                type_nodes_by_name[symbol.name].push_back(id);

            if (kind == CodeSemanticNodeKind::Include)
                result.edges.push_back(make_edge(CodeSemanticEdgeKind::Includes, file_id, id, symbol.name));

            if (kind == CodeSemanticNodeKind::Type)
            {
                for (const auto& field : symbol.fields)
                {
                    const std::string field_qualified_name = qualified_name + "::" + field.name;
                    const CodeSemanticNodeId field_id = make_node_id(
                        CodeSemanticNodeKind::Field,
                        module_path,
                        file.path,
                        field_qualified_name,
                        symbol.line);
                    CodeSemanticMetrics field_metrics;
                    field_metrics.referenced_type_count = static_cast<uint32_t>(field.referenced_types.size());
                    result.nodes.push_back(make_node(
                        field_id,
                        id,
                        CodeSemanticNodeKind::Field,
                        module_path,
                        field.name,
                        field_qualified_name,
                        CodeSemanticSourceSpan{ file.path, symbol.line, symbol.line },
                        field_metrics));
                    append_contains_edge(result.edges, id, field_id);
                }
            }
        }
    }

    for (const ParsedFile& file : snapshot.files)
    {
        const std::string module_path = module_path_for_source_file(file.path);
        for (const SymbolRecord& symbol : file.symbols)
        {
            const CodeSemanticNodeKind kind = node_kind_for_symbol(symbol);
            const std::string qualified_name = qualified_name_for_symbol(symbol);
            const auto source_it = symbol_nodes_by_key.find(module_path + "|" + file.path + "|" + qualified_name);
            if (source_it == symbol_nodes_by_key.end())
                continue;
            const CodeSemanticNodeId source_id = source_it->second;

            if (kind == CodeSemanticNodeKind::Type)
            {
                for (const std::string& base_name : symbol.inherited_types)
                {
                    if (const auto targets = type_nodes_by_name.find(base_name);
                        targets != type_nodes_by_name.end() && targets->second.size() == 1)
                    {
                        result.edges.push_back(make_edge(
                            CodeSemanticEdgeKind::Inherits,
                            source_id,
                            targets->second.front(),
                            base_name));
                    }
                }

                for (const auto& field : symbol.fields)
                {
                    const std::string field_qualified_name = qualified_name + "::" + field.name;
                    const CodeSemanticNodeId field_id = make_node_id(
                        CodeSemanticNodeKind::Field,
                        module_path,
                        file.path,
                        field_qualified_name,
                        symbol.line);
                    for (const std::string& type_name : field.referenced_types)
                    {
                        if (const auto targets = type_nodes_by_name.find(type_name);
                            targets != type_nodes_by_name.end() && targets->second.size() == 1)
                        {
                            result.edges.push_back(make_edge(
                                CodeSemanticEdgeKind::ReferencesType,
                                field_id,
                                targets->second.front(),
                                field.type_name.empty() ? type_name : field.type_name));
                        }
                    }
                }
            }
        }
    }

    std::sort(result.nodes.begin(), result.nodes.end(), [](const CodeSemanticNode& lhs, const CodeSemanticNode& rhs) {
        return std::tie(lhs.module_path, lhs.source.file_path, lhs.kind, lhs.qualified_name, lhs.source.start_line, lhs.id)
            < std::tie(rhs.module_path, rhs.source.file_path, rhs.kind, rhs.qualified_name, rhs.source.start_line, rhs.id);
    });
    std::sort(result.edges.begin(), result.edges.end(), [](const CodeSemanticEdge& lhs, const CodeSemanticEdge& rhs) {
        return std::tie(lhs.kind, lhs.source_id, lhs.target_id, lhs.label, lhs.id)
            < std::tie(rhs.kind, rhs.source_id, rhs.target_id, rhs.label, rhs.id);
    });

    result.indexes.node_index_by_id.reserve(result.nodes.size());
    for (size_t index = 0; index < result.nodes.size(); ++index)
    {
        const CodeSemanticNode& node = result.nodes[index];
        result.indexes.node_index_by_id.emplace(node.id, index);
        if (node.parent_id != 0)
            result.indexes.nodes_by_parent[node.parent_id].push_back(node.id);
        if (!node.module_path.empty())
            result.indexes.nodes_by_module[node.module_path].push_back(node.id);
        if (node.kind == CodeSemanticNodeKind::Type)
            result.indexes.type_nodes_by_name[node.name].push_back(node.id);
    }
    result.health = compute_health_metrics(result.nodes, result.edges);

    return result;
}

} // namespace draxul
