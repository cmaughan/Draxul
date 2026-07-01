#include "biology_builder.h"

#include <draxul/codeviz_scene_world.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <draxul/pastel_palette.h>
#include <draxul/perf_timing.h>
#include <draxul/primitive_meshes.h>
#include <glm/gtc/constants.hpp>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace draxul
{

namespace
{

constexpr float kCellFootprint = 3.6f;
constexpr float kCellHeight = 0.10f;
constexpr float kFileSpacing = 4.8f;
constexpr float kModuleGap = 7.0f;
constexpr float kBodyBaseHeight = 0.45f;
constexpr float kOrganelleFootprint = 0.42f;
constexpr float kOrganelleHeight = 0.24f;
constexpr float kFibreWidth = 0.08f;
constexpr float kFibreHeight = 0.06f;
constexpr size_t kMaxFibreCount = 4096;

struct BiologyEndpoint
{
    glm::vec2 center{ 0.0f };
    float elevation = 0.0f;
    glm::vec4 color{ 0.0f };
};

struct FileCell
{
    const CodeSemanticNode* file = nullptr;
    std::vector<const CodeSemanticNode*> bodies;
    std::vector<const CodeSemanticNode*> organelles;
    glm::vec2 center{ 0.0f };
};

struct ModuleTissue
{
    std::string module_path;
    const CodeSemanticNode* module = nullptr;
    std::vector<FileCell> files;
    float width = 0.0f;
    float depth = 0.0f;
    glm::vec2 offset{ 0.0f };
};

const CodeSemanticNode* find_node(const CodeSemanticSnapshot& semantics, CodeSemanticNodeId id)
{
    const auto it = semantics.indexes.node_index_by_id.find(id);
    if (it == semantics.indexes.node_index_by_id.end() || it->second >= semantics.nodes.size())
        return nullptr;
    return &semantics.nodes[it->second];
}

bool has_case_insensitive_token(std::string_view text, std::string_view token)
{
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lower.find(token) != std::string::npos;
}

bool is_biology_test_source(std::string_view source_file_path)
{
    return has_case_insensitive_token(source_file_path, "/test/")
        || has_case_insensitive_token(source_file_path, "/tests/")
        || has_case_insensitive_token(source_file_path, "\\test\\")
        || has_case_insensitive_token(source_file_path, "\\tests\\")
        || has_case_insensitive_token(source_file_path, "_test.")
        || has_case_insensitive_token(source_file_path, "_tests.");
}

bool module_is_visible(std::string_view module_path, const MegaCityCodeConfig& config)
{
    return config.selected_module_path.empty() || module_path == config.selected_module_path;
}

bool source_is_visible(const CodeSemanticNode& node, const MegaCityCodeConfig& config)
{
    if (!module_is_visible(node.module_path, config))
        return false;
    return !config.hide_test_entities || !is_biology_test_source(node.source.file_path);
}

bool is_biology_symbol_body(CodeSemanticNodeKind kind)
{
    return kind == CodeSemanticNodeKind::Type || kind == CodeSemanticNodeKind::Function;
}

bool is_biology_organelle(CodeSemanticNodeKind kind)
{
    return kind == CodeSemanticNodeKind::Method
        || kind == CodeSemanticNodeKind::Field
        || kind == CodeSemanticNodeKind::Include;
}

std::string_view display_name(const CodeSemanticNode& node)
{
    return !node.qualified_name.empty() ? std::string_view(node.qualified_name) : std::string_view(node.name);
}

glm::vec4 biology_module_color(std::string_view module_path, float alpha = 1.0f)
{
    glm::vec4 base = pastel_color_from_hash(stable_color_hash(module_path));
    base = glm::vec4(glm::mix(glm::vec3(base), glm::vec3(1.0f), 0.16f), alpha);
    return base;
}

glm::vec4 body_color(const CodeSemanticNode& node, const glm::vec4& module_color)
{
    glm::vec3 rgb(module_color);
    if (node.kind == CodeSemanticNodeKind::Function)
        rgb = glm::mix(rgb, glm::vec3(0.38f, 0.95f, 0.78f), 0.34f);
    else if (node.is_abstract)
        rgb = glm::mix(rgb, glm::vec3(0.88f, 0.78f, 1.0f), 0.30f);
    else
        rgb = glm::mix(rgb, glm::vec3(0.68f, 0.82f, 1.0f), 0.20f);
    return glm::vec4(glm::clamp(rgb, glm::vec3(0.0f), glm::vec3(1.0f)), 1.0f);
}

glm::vec4 organelle_color(const CodeSemanticNode& node, const glm::vec4& parent_color)
{
    glm::vec3 rgb(parent_color);
    if (node.kind == CodeSemanticNodeKind::Field)
        rgb = glm::mix(rgb, glm::vec3(0.96f, 0.88f, 0.45f), 0.38f);
    else if (node.kind == CodeSemanticNodeKind::Include)
        rgb = glm::mix(rgb, glm::vec3(0.78f, 0.70f, 1.0f), 0.34f);
    else
        rgb = glm::mix(rgb, glm::vec3(1.0f), 0.18f);
    return glm::vec4(glm::clamp(rgb, glm::vec3(0.0f), glm::vec3(1.0f)), 1.0f);
}

float symbol_mass(const CodeSemanticNode& node)
{
    return static_cast<float>(
        std::max<uint32_t>(
            1u,
            node.metrics.line_count
                + node.metrics.field_count
                + node.metrics.method_count
                + node.metrics.referenced_type_count));
}

void expand_bounds(float x, float z, float half_extent, BiologyBuildResult& result)
{
    if (!result.bounds_valid)
    {
        result.min_x = x - half_extent;
        result.max_x = x + half_extent;
        result.min_z = z - half_extent;
        result.max_z = z + half_extent;
        result.bounds_valid = true;
        return;
    }
    result.min_x = std::min(result.min_x, x - half_extent);
    result.max_x = std::max(result.max_x, x + half_extent);
    result.min_z = std::min(result.min_z, z - half_extent);
    result.max_z = std::max(result.max_z, z + half_extent);
}

std::vector<ModuleTissue> collect_tissues(const CodeSemanticSnapshot& semantics, const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    std::map<std::string, ModuleTissue> tissues_by_module;
    std::unordered_map<std::string, std::vector<const CodeSemanticNode*>> bodies_by_file;
    std::unordered_map<std::string, std::vector<const CodeSemanticNode*>> organelles_by_file;

    for (const CodeSemanticNode& node : semantics.nodes)
    {
        if (node.kind == CodeSemanticNodeKind::Module && module_is_visible(node.module_path, config))
        {
            auto& tissue = tissues_by_module[node.module_path];
            tissue.module_path = node.module_path;
            tissue.module = &node;
        }
        else if (node.kind == CodeSemanticNodeKind::File && source_is_visible(node, config))
        {
            auto& tissue = tissues_by_module[node.module_path];
            tissue.module_path = node.module_path;
            tissue.files.push_back(FileCell{ .file = &node });
        }
        else if (is_biology_symbol_body(node.kind) && source_is_visible(node, config))
        {
            bodies_by_file[node.source.file_path].push_back(&node);
        }
        else if (is_biology_organelle(node.kind) && source_is_visible(node, config))
        {
            organelles_by_file[node.source.file_path].push_back(&node);
        }
    }

    std::vector<ModuleTissue> tissues;
    tissues.reserve(tissues_by_module.size());
    for (auto& [module_path, tissue] : tissues_by_module)
    {
        std::sort(tissue.files.begin(), tissue.files.end(), [](const FileCell& lhs, const FileCell& rhs) {
            return lhs.file && rhs.file
                ? lhs.file->source.file_path < rhs.file->source.file_path
                : lhs.file < rhs.file;
        });

        for (FileCell& file : tissue.files)
        {
            if (!file.file)
                continue;
            file.bodies = std::move(bodies_by_file[file.file->source.file_path]);
            file.organelles = std::move(organelles_by_file[file.file->source.file_path]);
            auto by_name = [](const CodeSemanticNode* lhs, const CodeSemanticNode* rhs) {
                return lhs && rhs ? display_name(*lhs) < display_name(*rhs) : lhs < rhs;
            };
            std::sort(file.bodies.begin(), file.bodies.end(), by_name);
            std::sort(file.organelles.begin(), file.organelles.end(), by_name);
        }

        const int file_count = static_cast<int>(tissue.files.size());
        const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(file_count)))));
        const int rows = std::max(1, (file_count + cols - 1) / cols);
        tissue.width = static_cast<float>(cols) * kFileSpacing;
        tissue.depth = static_cast<float>(rows) * kFileSpacing;
        tissues.push_back(std::move(tissue));
    }

    return tissues;
}

void place_modules(std::vector<ModuleTissue>& modules)
{
    PERF_MEASURE();
    if (modules.empty())
        return;

    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(modules.size())))));
    float z_cursor = 0.0f;
    for (size_t row_start = 0; row_start < modules.size(); row_start += static_cast<size_t>(cols))
    {
        const size_t row_end = std::min(row_start + static_cast<size_t>(cols), modules.size());
        float row_depth = 0.0f;
        for (size_t index = row_start; index < row_end; ++index)
            row_depth = std::max(row_depth, modules[index].depth);

        float x_cursor = 0.0f;
        for (size_t index = row_start; index < row_end; ++index)
        {
            modules[index].offset = glm::vec2(x_cursor + modules[index].width * 0.5f, z_cursor + row_depth * 0.5f);
            x_cursor += modules[index].width + kModuleGap;
        }
        z_cursor += row_depth + kModuleGap;
    }

    float min_x = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();
    for (const ModuleTissue& module : modules)
    {
        min_x = std::min(min_x, module.offset.x - module.width * 0.5f);
        max_x = std::max(max_x, module.offset.x + module.width * 0.5f);
        min_z = std::min(min_z, module.offset.y - module.depth * 0.5f);
        max_z = std::max(max_z, module.offset.y + module.depth * 0.5f);
    }

    const glm::vec2 center((min_x + max_x) * 0.5f, (min_z + max_z) * 0.5f);
    for (ModuleTissue& module : modules)
        module.offset -= center;
}

bool valid_endpoint(const BiologyEndpoint& endpoint)
{
    return endpoint.color.a > 0.0f;
}

void create_fibre(
    CodeVizSceneWorld& world,
    const BiologyEndpoint& source,
    const BiologyEndpoint& target,
    const CodeSemanticNode& source_node,
    const CodeSemanticNode& target_node)
{
    const glm::vec2 delta = target.center - source.center;
    const float length = glm::length(delta);
    if (length <= 1e-4f)
        return;

    const float pitch = std::atan2(target.elevation - source.elevation, length);
    const float cos_pitch = std::cos(pitch);
    const glm::vec4 color = glm::mix(source.color, target.color, 0.5f);
    world.create_link_segment(
        (source.center.x + target.center.x) * 0.5f,
        (source.center.y + target.center.y) * 0.5f,
        LinkSegmentMetrics{
            std::max(length, kFibreWidth) / std::max(cos_pitch, 1e-3f),
            kFibreWidth,
            kFibreHeight,
            -std::atan2(delta.y, delta.x),
            pitch,
        },
        glm::vec4(glm::vec3(color), 0.74f),
        CodeVizSemanticRef{},
        (source.elevation + target.elevation) * 0.5f,
        RelationshipLink{
            source_node.source.file_path,
            source_node.module_path,
            std::string(display_name(source_node)),
            target_node.source.file_path,
            target_node.module_path,
            std::string(display_name(target_node)),
        });
}

} // namespace

BiologyBuildResult build_biology_view(
    CodeVizSceneWorld& world,
    const CodeSemanticSnapshot& semantics,
    const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    BiologyBuildResult result;

    world.clear();
    if (!semantics.complete || semantics.nodes.empty())
        return result;

    std::vector<ModuleTissue> modules = collect_tissues(semantics, config);
    place_modules(modules);

    result.stats.tissue_count = modules.size();

    std::unordered_map<CodeSemanticNodeId, BiologyEndpoint> endpoints;
    endpoints.reserve(semantics.nodes.size());
    const auto ellipsoid_mesh = std::make_shared<const GeometryMesh>(
        build_unit_uv_sphere_geometry(16, 32));

    for (ModuleTissue& module : modules)
    {
        const glm::vec4 module_color = biology_module_color(module.module_path);
        const int file_count = static_cast<int>(module.files.size());
        const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(file_count)))));
        const int rows = std::max(1, (file_count + cols - 1) / cols);
        const float origin_x = module.offset.x - (static_cast<float>(cols) - 1.0f) * kFileSpacing * 0.5f;
        const float origin_z = module.offset.y - (static_cast<float>(rows) - 1.0f) * kFileSpacing * 0.5f;

        for (size_t file_index = 0; file_index < module.files.size(); ++file_index)
        {
            FileCell& file = module.files[file_index];
            if (!file.file)
                continue;

            const int col = static_cast<int>(file_index) % cols;
            const int row = static_cast<int>(file_index) / cols;
            file.center = {
                origin_x + static_cast<float>(col) * kFileSpacing,
                origin_z + static_cast<float>(row) * kFileSpacing,
            };

            const float semantic_count = static_cast<float>(file.bodies.size() + file.organelles.size());
            const float cell_footprint = kCellFootprint + std::min(1.4f, std::sqrt(semantic_count) * 0.14f);
            const uint32_t file_hash = stable_color_hash(file.file->source.file_path);
            const glm::vec4 file_color = glm::vec4(
                glm::mix(glm::vec3(module_color), glm::vec3(pastel_color_from_hash(file_hash)), 0.32f),
                0.46f);
            world.create_ellipsoid(
                file.center.x,
                file.center.y,
                0.0f,
                EllipsoidMetrics{
                    .radius_x = cell_footprint * 0.5f,
                    .radius_y = kCellHeight * 0.5f,
                    .radius_z = cell_footprint * 0.5f,
                },
                file_color,
                CodeVizSemanticRef{ file.file->source.file_path, file.file->source.file_path, file.file->module_path },
                ellipsoid_mesh);
            endpoints.emplace(file.file->id, BiologyEndpoint{ file.center, kCellHeight, file_color });
            expand_bounds(file.center.x, file.center.y, cell_footprint * 0.5f, result);
            ++result.stats.file_cell_count;

            const size_t body_count = file.bodies.size();
            const float body_orbit_radius = body_count <= 1 ? 0.0f : cell_footprint * 0.20f;
            for (size_t body_index = 0; body_index < file.bodies.size(); ++body_index)
            {
                const CodeSemanticNode& node = *file.bodies[body_index];
                const float angle = body_count <= 1
                    ? 0.0f
                    : glm::two_pi<float>() * static_cast<float>(body_index) / static_cast<float>(body_count);
                const glm::vec2 center = file.center + glm::vec2(std::cos(angle), std::sin(angle)) * body_orbit_radius;
                const float mass = symbol_mass(node);
                const float footprint = std::clamp(0.58f + std::sqrt(mass) * 0.08f, 0.62f, 1.35f);
                const float height = std::clamp(kBodyBaseHeight + std::sqrt(mass) * 0.10f, 0.42f, 2.2f);
                const glm::vec4 color = body_color(node, module_color);
                const float elevation = kCellHeight + 0.03f;
                world.create_ellipsoid(
                    center.x,
                    center.y,
                    elevation,
                    EllipsoidMetrics{
                        .radius_x = footprint * 0.5f,
                        .radius_y = height * 0.5f,
                        .radius_z = footprint * 0.5f,
                    },
                    color,
                    CodeVizSemanticRef{ node.source.file_path, std::string(display_name(node)), node.module_path },
                    ellipsoid_mesh);
                endpoints.emplace(node.id, BiologyEndpoint{ center, elevation + height, color });
                ++result.stats.symbol_body_count;
            }

            std::unordered_map<CodeSemanticNodeId, size_t> organelles_per_parent;
            std::unordered_map<CodeSemanticNodeId, size_t> organelles_seen_per_parent;
            for (const CodeSemanticNode* organelle : file.organelles)
            {
                const CodeSemanticNodeId parent_id = organelle && organelle->parent_id != 0
                    ? organelle->parent_id
                    : file.file->id;
                ++organelles_per_parent[parent_id];
            }

            for (const CodeSemanticNode* organelle : file.organelles)
            {
                if (!organelle)
                    continue;
                const CodeSemanticNodeId parent_id = organelle->parent_id != 0
                    ? organelle->parent_id
                    : file.file->id;
                const size_t parent_count = std::max<size_t>(1, organelles_per_parent[parent_id]);
                const size_t parent_index = organelles_seen_per_parent[parent_id]++;
                const BiologyEndpoint parent_endpoint = endpoints.contains(parent_id)
                    ? endpoints[parent_id]
                    : BiologyEndpoint{ file.center, kCellHeight, module_color };
                const float angle = parent_count <= 1
                    ? glm::half_pi<float>()
                    : glm::two_pi<float>() * static_cast<float>(parent_index) / static_cast<float>(parent_count);
                const float radius = parent_id == file.file->id ? cell_footprint * 0.32f : 0.58f;
                const glm::vec2 center = parent_endpoint.center + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
                const glm::vec4 color = organelle_color(*organelle, parent_endpoint.color);
                const float elevation = kCellHeight + 0.06f;
                world.create_ellipsoid(
                    center.x,
                    center.y,
                    elevation,
                    EllipsoidMetrics{
                        .radius_x = kOrganelleFootprint * 0.5f,
                        .radius_y = kOrganelleHeight * 0.5f,
                        .radius_z = kOrganelleFootprint * 0.5f,
                    },
                    color,
                    CodeVizSemanticRef{
                        organelle->source.file_path,
                        std::string(display_name(*organelle)),
                        organelle->module_path,
                    },
                    ellipsoid_mesh);
                endpoints.emplace(organelle->id, BiologyEndpoint{ center, elevation + kOrganelleHeight, color });
                ++result.stats.organelle_count;
            }
        }
    }

    for (const CodeSemanticEdge& edge : semantics.edges)
    {
        if (result.stats.fibre_count >= kMaxFibreCount)
            break;
        if (edge.kind == CodeSemanticEdgeKind::Contains)
            continue;
        const auto source_endpoint = endpoints.find(edge.source_id);
        const auto target_endpoint = endpoints.find(edge.target_id);
        if (source_endpoint == endpoints.end() || target_endpoint == endpoints.end())
            continue;
        if (!valid_endpoint(source_endpoint->second) || !valid_endpoint(target_endpoint->second))
            continue;
        const CodeSemanticNode* source_node = find_node(semantics, edge.source_id);
        const CodeSemanticNode* target_node = find_node(semantics, edge.target_id);
        if (!source_node || !target_node)
            continue;
        create_fibre(world, source_endpoint->second, target_endpoint->second, *source_node, *target_node);
        ++result.stats.fibre_count;
    }

    if (result.bounds_valid && !config.point_light_position_valid)
    {
        const float span = std::max(result.max_x - result.min_x, result.max_z - result.min_z);
        result.computed_default_light = true;
        result.default_light_x = result.min_x;
        result.default_light_y = std::max(8.0f, span * 0.42f);
        result.default_light_z = result.min_z;
        result.default_light_radius = std::max(24.0f, span * 0.85f);
    }

    return result;
}

} // namespace draxul
