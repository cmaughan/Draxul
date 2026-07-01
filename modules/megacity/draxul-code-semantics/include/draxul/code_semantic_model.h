#pragma once

#include <draxul/treesitter.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace draxul
{

using CodeSemanticNodeId = uint64_t;
using CodeSemanticEdgeId = uint64_t;

enum class CodeSemanticNodeKind : uint8_t
{
    Repository,
    Module,
    File,
    Type,
    Function,
    Method,
    Field,
    Include,
};

enum class CodeSemanticTypeKind : uint8_t
{
    None,
    Class,
    Struct,
};

enum class CodeSemanticEdgeKind : uint8_t
{
    Contains,
    Inherits,
    ReferencesType,
    Includes,
};

struct CodeSemanticSourceSpan
{
    std::string file_path;
    uint32_t start_line = 0;
    uint32_t end_line = 0;
};

struct CodeSemanticMetrics
{
    uint32_t line_count = 0;
    uint32_t field_count = 0;
    uint32_t method_count = 0;
    uint32_t referenced_type_count = 0;
};

struct CodebaseHealthMetrics
{
    float complexity = 0.5f;
    float cohesion = 0.5f;
    float coupling = 0.5f;
};

struct CodeSemanticNode
{
    CodeSemanticNodeId id = 0;
    CodeSemanticNodeId parent_id = 0;
    CodeSemanticNodeKind kind = CodeSemanticNodeKind::File;
    std::string module_path;
    std::string name;
    std::string qualified_name;
    CodeSemanticSourceSpan source;
    CodeSemanticMetrics metrics;
    CodeSemanticTypeKind type_kind = CodeSemanticTypeKind::None;
    bool is_abstract = false;
};

struct CodeSemanticEdge
{
    CodeSemanticEdgeId id = 0;
    CodeSemanticNodeId source_id = 0;
    CodeSemanticNodeId target_id = 0;
    CodeSemanticEdgeKind kind = CodeSemanticEdgeKind::Contains;
    std::string label;
};

struct CodeSemanticIndexes
{
    std::unordered_map<CodeSemanticNodeId, size_t> node_index_by_id;
    std::unordered_map<CodeSemanticNodeId, std::vector<CodeSemanticNodeId>> nodes_by_parent;
    std::unordered_map<std::string, std::vector<CodeSemanticNodeId>> nodes_by_module;
    std::unordered_map<std::string, std::vector<CodeSemanticNodeId>> type_nodes_by_name;
};

struct CodeSemanticSnapshot
{
    std::vector<CodeSemanticNode> nodes;
    std::vector<CodeSemanticEdge> edges;
    CodeSemanticIndexes indexes;
    CodebaseHealthMetrics health;
    bool complete = false;
};

[[nodiscard]] CodeSemanticSnapshot build_code_semantic_snapshot(const CodebaseSnapshot& snapshot);

} // namespace draxul
