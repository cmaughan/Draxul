#include <draxul/perf_timing.h>
#include <draxul/tree_generator.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace draxul
{

namespace
{

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kLeafAtlasSize = 1024.0f;
constexpr size_t kMaxGeometryVertices = std::numeric_limits<uint16_t>::max();

// Padded atlas rects derived from LeafSet023_1K-JPG_Opacity.jpg.
constexpr std::array<glm::vec4, 6> kLeafAtlasRects = { {
    glm::vec4(532.0f / kLeafAtlasSize, 17.0f / kLeafAtlasSize, 1022.0f / kLeafAtlasSize, 335.0f / kLeafAtlasSize),
    glm::vec4(20.0f / kLeafAtlasSize, 32.0f / kLeafAtlasSize, 486.0f / kLeafAtlasSize, 300.0f / kLeafAtlasSize),
    glm::vec4(32.0f / kLeafAtlasSize, 368.0f / kLeafAtlasSize, 489.0f / kLeafAtlasSize, 662.0f / kLeafAtlasSize),
    glm::vec4(524.0f / kLeafAtlasSize, 378.0f / kLeafAtlasSize, 1022.0f / kLeafAtlasSize, 657.0f / kLeafAtlasSize),
    glm::vec4(10.0f / kLeafAtlasSize, 702.0f / kLeafAtlasSize, 503.0f / kLeafAtlasSize, 1001.0f / kLeafAtlasSize),
    glm::vec4(564.0f / kLeafAtlasSize, 715.0f / kLeafAtlasSize, 1002.0f / kLeafAtlasSize, 990.0f / kLeafAtlasSize),
} };

struct BranchFrame
{
    glm::vec3 center{ 0.0f };
    glm::vec3 axis{ 0.0f, 1.0f, 0.0f };
    glm::vec3 basis_u{ 1.0f, 0.0f, 0.0f };
    glm::vec3 basis_v{ 0.0f, 0.0f, 1.0f };
    float radius = 0.1f;
    float t = 0.0f;
    float distance = 0.0f;
};

struct BranchAttachment
{
    bool enabled = false;
    glm::vec3 surface_center{ 0.0f };
    glm::vec3 surface_normal{ 0.0f, 1.0f, 0.0f };
    glm::vec3 surface_u{ 1.0f, 0.0f, 0.0f };
    glm::vec3 root_basis_u{ 1.0f, 0.0f, 0.0f };
    glm::vec3 root_basis_v{ 0.0f, 0.0f, 1.0f };
    float collar_radius = 0.05f;
    uint32_t parent_ring_start = 0;
    int parent_frame_index = 0;
    int parent_row_count = 0;
    int parent_segment_center = 0;
    int parent_segment_span = 1;
    int child_segment_center = 0;
    int child_segment_span = 1;
    std::vector<uint32_t> parent_arc_indices;
};

float hash_to_unit_float(uint64_t seed, uint32_t a, uint32_t b)
{
    uint64_t x = seed;
    x ^= static_cast<uint64_t>(a) + 0x9e3779b97f4a7c15ULL + (x << 6U) + (x >> 2U);
    x ^= static_cast<uint64_t>(b) + 0x9e3779b97f4a7c15ULL + (x << 6U) + (x >> 2U);
    x ^= x >> 30U;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27U;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31U;
    constexpr double kInvMax = 1.0 / static_cast<double>(std::numeric_limits<uint64_t>::max());
    return static_cast<float>(static_cast<double>(x) * kInvMax);
}

int hash_to_int(uint64_t seed, uint32_t a, uint32_t b, int min_value, int max_value)
{
    if (max_value <= min_value)
        return min_value;
    const float unit = hash_to_unit_float(seed, a, b);
    return min_value + static_cast<int>(std::floor(unit * static_cast<float>((max_value - min_value) + 1)));
}

glm::vec3 orthogonal_unit(const glm::vec3& axis)
{
    const glm::vec3 up = std::abs(axis.y) < 0.95f
        ? glm::vec3(0.0f, 1.0f, 0.0f)
        : glm::vec3(1.0f, 0.0f, 0.0f);
    return glm::normalize(glm::cross(up, axis));
}

glm::vec3 project_basis(const glm::vec3& previous_basis, const glm::vec3& axis)
{
    glm::vec3 projected = previous_basis - axis * glm::dot(previous_basis, axis);
    if (glm::dot(projected, projected) < 1e-6f)
        projected = orthogonal_unit(axis);
    return glm::normalize(projected);
}

glm::vec3 bark_color_for_vertex(const DraxulTreeParams& params, float t, uint32_t ring_index, uint32_t segment_index)
{
    const glm::vec3 base = glm::mix(params.bark_color_root, params.bark_color_tip, std::clamp(t, 0.0f, 1.0f));
    const float noise = (hash_to_unit_float(params.seed, ring_index, segment_index) * 2.0f - 1.0f) * params.bark_color_noise;
    return glm::clamp(base + glm::vec3(noise), glm::vec3(0.0f), glm::vec3(1.0f));
}

glm::vec3 leaf_color_for_quad(const DraxulTreeParams& params, uint32_t branch_id, uint32_t leaf_index)
{
    (void)params;
    (void)branch_id;
    (void)leaf_index;
    return glm::vec3(1.0f);
}

bool can_append_vertices(const GeometryMesh& mesh, size_t additional_vertices)
{
    if (additional_vertices > kMaxGeometryVertices)
        return false;
    return mesh.vertices.size() <= (kMaxGeometryVertices - additional_vertices);
}

int wrap_segment(int segment, int radial_segments)
{
    if (radial_segments <= 0)
        return 0;
    int wrapped = segment % radial_segments;
    if (wrapped < 0)
        wrapped += radial_segments;
    return wrapped;
}

int shortest_segment_delta(int segment, int center, int radial_segments)
{
    int delta = wrap_segment(segment - center, radial_segments);
    if (delta > radial_segments / 2)
        delta -= radial_segments;
    return delta;
}

uint32_t ring_vertex_index(uint32_t ring_start, int radial_segments, int row_index, int segment)
{
    const uint32_t row_offset = static_cast<uint32_t>(row_index * (radial_segments + 1));
    return ring_start + row_offset + static_cast<uint32_t>(wrap_segment(segment, radial_segments));
}

std::vector<uint32_t> build_arc_indices(
    uint32_t ring_start,
    int radial_segments,
    int row_index,
    int segment_center,
    int segment_span,
    bool reverse)
{
    PERF_MEASURE();
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(segment_span * 2 + 1));
    if (!reverse)
    {
        for (int offset = -segment_span; offset <= segment_span; ++offset)
            indices.push_back(ring_vertex_index(ring_start, radial_segments, row_index, segment_center + offset));
    }
    else
    {
        for (int offset = segment_span; offset >= -segment_span; --offset)
            indices.push_back(ring_vertex_index(ring_start, radial_segments, row_index, segment_center + offset));
    }
    return indices;
}

void append_double_sided_bridge(
    GeometryMesh& mesh,
    const std::vector<uint32_t>& edge_a,
    const std::vector<uint32_t>& edge_b)
{
    PERF_MEASURE();
    if (edge_a.size() != edge_b.size() || edge_a.size() < 2)
        return;

    for (size_t i = 0; i + 1 < edge_a.size(); ++i)
    {
        const uint32_t a0 = edge_a[i];
        const uint32_t a1 = edge_a[i + 1];
        const uint32_t b0 = edge_b[i];
        const uint32_t b1 = edge_b[i + 1];

        mesh.indices.push_back(a0);
        mesh.indices.push_back(a1);
        mesh.indices.push_back(b1);
        mesh.indices.push_back(a0);
        mesh.indices.push_back(b1);
        mesh.indices.push_back(b0);

        mesh.indices.push_back(b1);
        mesh.indices.push_back(a1);
        mesh.indices.push_back(a0);
        mesh.indices.push_back(b0);
        mesh.indices.push_back(b1);
        mesh.indices.push_back(a0);
    }
}

void append_attachment_bridge(
    GeometryMesh& bark_mesh,
    const BranchAttachment& attachment,
    uint32_t child_ring_start,
    int radial_segments)
{
    PERF_MEASURE();
    if (!attachment.enabled || radial_segments < 3)
        return;

    const int parent_row = std::clamp(
        attachment.parent_frame_index,
        0,
        std::max(attachment.parent_row_count - 1, 0));
    const std::vector<uint32_t> parent_arc = build_arc_indices(
        attachment.parent_ring_start,
        radial_segments,
        parent_row,
        attachment.parent_segment_center,
        attachment.parent_segment_span,
        false);

    const glm::vec3 root_back = -attachment.surface_normal;
    const float child_angle = std::atan2(
        glm::dot(root_back, attachment.root_basis_v),
        glm::dot(root_back, attachment.root_basis_u));
    const int child_segment_center = static_cast<int>(std::lround(
        (child_angle / kTwoPi) * static_cast<float>(radial_segments)));
    const std::vector<uint32_t> child_arc = build_arc_indices(
        child_ring_start,
        radial_segments,
        0,
        child_segment_center,
        attachment.parent_segment_span,
        true);

    append_double_sided_bridge(bark_mesh, parent_arc, child_arc);
}

void prepend_attachment_ring(
    std::vector<BranchFrame>& frames,
    float length,
    const BranchAttachment& attachment)
{
    PERF_MEASURE();
    if (!attachment.enabled || frames.size() < 2)
        return;

    const glm::vec3 root_axis = frames.front().axis;
    const glm::vec3 surface_u = project_basis(attachment.surface_u, root_axis);
    const glm::vec3 surface_v = glm::normalize(glm::cross(root_axis, surface_u));
    const float collar_radius = std::max(attachment.collar_radius, 0.005f);

    BranchFrame root_frame;
    root_frame.center = attachment.surface_center;
    root_frame.axis = root_axis;
    root_frame.basis_u = surface_u;
    root_frame.basis_v = surface_v;
    root_frame.radius = collar_radius;
    root_frame.t = 0.0f;
    root_frame.distance = 0.0f;

    const float origin_distance = glm::length(frames.front().center - attachment.surface_center);
    const float fallback_distance = std::max(collar_radius * 0.55f, length * 0.05f);
    const float transition_distance = std::clamp(
        origin_distance > 1e-4f ? origin_distance : fallback_distance,
        0.001f,
        std::max(fallback_distance, length * 0.18f));

    frames.front().distance = transition_distance;
    frames.front().t = length > 1e-5f ? std::clamp(transition_distance / length, 0.0f, 1.0f) : 0.0f;
    frames.front().radius = std::max(frames.front().radius, collar_radius * 0.82f);

    frames.insert(frames.begin(), root_frame);
}

void append_double_sided_quad(
    GeometryMesh& mesh,
    const glm::vec3& center,
    const glm::vec3& right,
    const glm::vec3& up,
    const glm::vec3& normal,
    const glm::vec3& color,
    const glm::vec4& uv_rect)
{
    PERF_MEASURE();
    if (!can_append_vertices(mesh, 8U))
        return;

    const uint16_t base_index = static_cast<uint16_t>(mesh.vertices.size());
    const glm::vec3 p0 = center - right - up;
    const glm::vec3 p1 = center + right - up;
    const glm::vec3 p2 = center + right + up;
    const glm::vec3 p3 = center - right + up;

    auto push_vertex = [&](const glm::vec3& position, const glm::vec3& vertex_normal, const glm::vec2& uv, const glm::vec3& tangent_dir) {
        GeometryVertex vertex;
        vertex.position = position;
        vertex.normal = vertex_normal;
        vertex.color = color;
        vertex.uv = uv;
        vertex.tangent = glm::vec4(glm::normalize(tangent_dir), 1.0f);
        mesh.vertices.push_back(vertex);
    };

    const glm::vec2 uv00(uv_rect.x, uv_rect.y);
    const glm::vec2 uv10(uv_rect.z, uv_rect.y);
    const glm::vec2 uv11(uv_rect.z, uv_rect.w);
    const glm::vec2 uv01(uv_rect.x, uv_rect.w);

    push_vertex(p0, normal, uv00, right);
    push_vertex(p1, normal, uv10, right);
    push_vertex(p2, normal, uv11, right);
    push_vertex(p3, normal, uv01, right);
    push_vertex(p0, -normal, uv00, -right);
    push_vertex(p3, -normal, uv01, -right);
    push_vertex(p2, -normal, uv11, -right);
    push_vertex(p1, -normal, uv10, -right);

    const std::array<uint16_t, 12> quad_indices = {
        static_cast<uint16_t>(base_index + 0U),
        static_cast<uint16_t>(base_index + 1U),
        static_cast<uint16_t>(base_index + 2U),
        static_cast<uint16_t>(base_index + 0U),
        static_cast<uint16_t>(base_index + 2U),
        static_cast<uint16_t>(base_index + 3U),
        static_cast<uint16_t>(base_index + 4U),
        static_cast<uint16_t>(base_index + 5U),
        static_cast<uint16_t>(base_index + 6U),
        static_cast<uint16_t>(base_index + 4U),
        static_cast<uint16_t>(base_index + 6U),
        static_cast<uint16_t>(base_index + 7U),
    };
    mesh.indices.insert(mesh.indices.end(), quad_indices.begin(), quad_indices.end());
}

std::vector<float> build_ring_distances(const DraxulTreeParams& params, float length, float spacing_scale)
{
    PERF_MEASURE();
    std::vector<float> distances;
    distances.reserve(static_cast<size_t>(std::ceil(length / std::max(params.base_ring_spacing, 0.05f))) + 2U);
    distances.push_back(0.0f);

    float distance = 0.0f;
    while (distance < length)
    {
        const float t = length > 1e-5f ? std::clamp(distance / length, 0.0f, 1.0f) : 1.0f;
        const float spacing = params.base_ring_spacing
            * std::lerp(1.0f, params.tip_ring_spacing_scale, t)
            * std::max(spacing_scale, 0.2f)
            * params.overall_scale;
        distance = std::min(distance + std::max(spacing, 0.025f), length);
        if (distance > distances.back())
            distances.push_back(distance);
    }

    if (distances.back() < length)
        distances.push_back(length);
    return distances;
}

std::vector<BranchFrame> build_branch_frames(
    const DraxulTreeParams& params,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float length,
    float base_radius,
    float tip_radius,
    int depth,
    uint32_t branch_id)
{
    PERF_MEASURE();
    const float trunk_reference = std::max(params.trunk_length * params.overall_scale, 0.1f);
    const float spacing_scale = std::clamp(length / trunk_reference, 0.25f, 1.0f);
    const std::vector<float> distances = build_ring_distances(params, length, spacing_scale);

    std::vector<BranchFrame> frames;
    frames.reserve(distances.size());

    const glm::vec3 base_axis = glm::normalize(direction);
    glm::vec3 basis_u = orthogonal_unit(base_axis);
    const float bend_angle = hash_to_unit_float(params.seed + branch_id, branch_id, 1U) * kTwoPi;
    const glm::vec3 bend_seed_dir = glm::normalize(
        std::cos(bend_angle) * basis_u
        + std::sin(bend_angle) * glm::normalize(glm::cross(base_axis, basis_u)));
    const glm::vec3 world_up(0.0f, 1.0f, 0.0f);
    const glm::vec3 target_axis = glm::normalize(
        base_axis
        + bend_seed_dir * (params.curvature * (0.7f + 0.15f * static_cast<float>(depth)))
        + world_up * (params.upward_bias * (depth == 0 ? 0.18f : 0.42f))
        - world_up * (params.droop_bias * (depth > 0 ? 0.15f : 0.03f)));

    const float wander_strength = depth == 0 ? params.trunk_wander : params.branch_wander;
    const float wander_frequency = std::clamp(params.wander_frequency, 0.0f, 1.0f);
    const float wander_deviation = std::max(params.wander_deviation, 0.0f);
    const float max_lateral_offset = wander_strength * wander_deviation * length * 0.18f;

    glm::vec3 center = origin;
    glm::vec3 axis = base_axis;
    float previous_distance = 0.0f;
    glm::vec3 offset_target(0.0f);
    glm::vec3 offset(0.0f);
    glm::vec3 previous_offset(0.0f);

    for (size_t index = 0; index < distances.size(); ++index)
    {
        const float distance = distances[index];
        const float t = length > 1e-5f ? std::clamp(distance / length, 0.0f, 1.0f) : 1.0f;
        if (index > 0)
        {
            axis = glm::normalize(glm::mix(base_axis, target_axis, std::sin(t * kPi * 0.5f) * t));
            center += axis * (distance - previous_distance);
            basis_u = project_basis(basis_u, axis);
        }
        const glm::vec3 basis_v = glm::normalize(glm::cross(axis, basis_u));
        if (index > 0 && index + 1 < distances.size())
        {
            const float event_roll = hash_to_unit_float(params.seed + branch_id * 61U, branch_id, static_cast<uint32_t>(index) + 271U);
            if (event_roll < wander_frequency)
            {
                const float event_angle = hash_to_unit_float(
                                              params.seed + branch_id * 67U,
                                              branch_id,
                                              static_cast<uint32_t>(index) + 311U)
                    * kTwoPi;
                const float event_magnitude = std::lerp(
                    0.35f,
                    1.0f,
                    hash_to_unit_float(
                        params.seed + branch_id * 71U,
                        branch_id,
                        static_cast<uint32_t>(index) + 353U));
                offset_target = (std::cos(event_angle) * basis_u
                                    + std::sin(event_angle) * basis_v)
                    * (max_lateral_offset * event_magnitude * std::sin(t * kPi));
            }
            offset = glm::mix(offset, offset_target, 0.35f);
            center += offset - previous_offset;
            previous_offset = offset;
        }
        const float taper_t = std::pow(t, params.taper_power);
        const float radius = std::lerp(base_radius, tip_radius, taper_t);

        frames.push_back(BranchFrame{
            .center = center,
            .axis = axis,
            .basis_u = basis_u,
            .basis_v = basis_v,
            .radius = radius,
            .t = t,
            .distance = distance,
        });
        previous_distance = distance;
    }

    return frames;
}

bool append_branch_rings(
    GeometryMesh& bark_mesh,
    const DraxulTreeParams& params,
    const std::vector<BranchFrame>& frames,
    const BranchAttachment* attachment,
    uint32_t branch_id,
    uint32_t& out_ring_start,
    uint32_t& out_tip_ring_start,
    int radial_segments)
{
    PERF_MEASURE();
    const size_t branch_vertex_count = frames.size() * static_cast<size_t>(radial_segments + 1);
    if (!can_append_vertices(bark_mesh, branch_vertex_count))
        return false;

    out_ring_start = static_cast<uint32_t>(bark_mesh.vertices.size());
    std::vector<std::vector<uint32_t>> row_indices(
        frames.size(), std::vector<uint32_t>(static_cast<size_t>(radial_segments + 1), 0u));
    for (size_t ring_index = 0; ring_index < frames.size(); ++ring_index)
    {
        const BranchFrame& frame = frames[ring_index];
        const float color_t = std::clamp(
            (static_cast<float>(branch_id % 11U) * 0.04f) + frame.t * 0.75f,
            0.0f,
            1.0f);
        const bool use_shared_root = attachment != nullptr && attachment->enabled && ring_index == 0
            && !attachment->parent_arc_indices.empty();
        for (int segment = 0; segment < radial_segments; ++segment)
        {
            const float u = static_cast<float>(segment) / static_cast<float>(radial_segments);
            const float angle = u * kTwoPi;
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            if (use_shared_root)
            {
                const int delta = shortest_segment_delta(
                    segment,
                    attachment->child_segment_center,
                    radial_segments);
                if (std::abs(delta) <= attachment->child_segment_span)
                {
                    const size_t arc_index = static_cast<size_t>(delta + attachment->child_segment_span);
                    if (arc_index < attachment->parent_arc_indices.size())
                    {
                        row_indices[ring_index][segment] = attachment->parent_arc_indices[arc_index];
                        continue;
                    }
                }
            }

            const glm::vec3 radial = glm::normalize(c * frame.basis_u + s * frame.basis_v);
            const glm::vec3 tangent_dir = glm::normalize(-s * frame.basis_u + c * frame.basis_v);

            GeometryVertex vertex;
            vertex.position = frame.center + radial * frame.radius;
            vertex.normal = radial;
            vertex.tangent = glm::vec4(tangent_dir, 1.0f);
            vertex.color = bark_color_for_vertex(
                params,
                color_t,
                branch_id * 4096U + static_cast<uint32_t>(ring_index),
                static_cast<uint32_t>(segment));
            vertex.uv = { u, frame.t };
            row_indices[ring_index][segment] = static_cast<uint32_t>(bark_mesh.vertices.size());
            bark_mesh.vertices.push_back(vertex);
        }
        row_indices[ring_index][radial_segments] = row_indices[ring_index][0];
        if (!use_shared_root)
        {
            const float u = 1.0f;
            const float angle = u * kTwoPi;
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            const glm::vec3 radial = glm::normalize(c * frame.basis_u + s * frame.basis_v);
            const glm::vec3 tangent_dir = glm::normalize(-s * frame.basis_u + c * frame.basis_v);

            GeometryVertex vertex;
            vertex.position = frame.center + radial * frame.radius;
            vertex.normal = radial;
            vertex.tangent = glm::vec4(tangent_dir, 1.0f);
            vertex.color = bark_color_for_vertex(
                params,
                color_t,
                branch_id * 4096U + static_cast<uint32_t>(ring_index),
                static_cast<uint32_t>(radial_segments));
            vertex.uv = { 1.0f, frame.t };
            row_indices[ring_index][radial_segments] = static_cast<uint32_t>(bark_mesh.vertices.size());
            bark_mesh.vertices.push_back(vertex);
        }
    }

    for (size_t ring_index = 0; ring_index + 1 < frames.size(); ++ring_index)
    {
        for (int segment = 0; segment < radial_segments; ++segment)
        {
            const uint32_t a = row_indices[ring_index][segment];
            const uint32_t b = row_indices[ring_index][segment + 1];
            const uint32_t c = row_indices[ring_index + 1][segment + 1];
            const uint32_t d = row_indices[ring_index + 1][segment];

            bark_mesh.indices.push_back(a);
            bark_mesh.indices.push_back(b);
            bark_mesh.indices.push_back(c);
            bark_mesh.indices.push_back(a);
            bark_mesh.indices.push_back(c);
            bark_mesh.indices.push_back(d);
        }
    }
    out_tip_ring_start = row_indices.back()[0];
    return true;
}

void append_branch_leaves(
    GeometryMesh& leaf_mesh,
    const DraxulTreeParams& params,
    const std::vector<BranchFrame>& frames,
    int depth,
    uint32_t branch_id)
{
    PERF_MEASURE();
    if (params.leaf_density <= 0.0f || frames.size() < 3 || depth < params.leaf_start_depth)
        return;

    const float min_leaf_size = std::min(params.leaf_size_range.x, params.leaf_size_range.y);
    const float max_leaf_size = std::max(params.leaf_size_range.x, params.leaf_size_range.y);
    const float orientation_randomness = std::clamp(params.leaf_orientation_randomness, 0.0f, 1.0f);
    uint32_t leaf_index = 0;
    for (size_t frame_index = 1; frame_index + 1 < frames.size(); ++frame_index)
    {
        const BranchFrame& frame = frames[frame_index];
        const float canopy_t = depth == 0
            ? std::clamp((frame.t - 0.62f) / 0.38f, 0.0f, 1.0f)
            : std::clamp((frame.t - 0.18f) / 0.82f, 0.0f, 1.0f);
        if (canopy_t <= 0.0f)
            continue;

        const float base_probability = depth == 0 ? 0.16f : 0.34f;
        const float spawn_probability = std::min(
            base_probability * params.leaf_density * std::lerp(0.45f, 1.0f, canopy_t),
            0.95f);
        const float spawn_roll = hash_to_unit_float(
            params.seed + branch_id * 79U,
            branch_id,
            static_cast<uint32_t>(frame_index) + 431U);
        if (spawn_roll > spawn_probability)
            continue;

        const float azimuth = hash_to_unit_float(
                                  params.seed + branch_id * 83U,
                                  branch_id,
                                  static_cast<uint32_t>(frame_index) + 463U)
            * kTwoPi;
        const glm::vec3 radial_dir = glm::normalize(
            std::cos(azimuth) * frame.basis_u
            + std::sin(azimuth) * frame.basis_v);
        const glm::vec3 random_lateral = glm::normalize(
            std::cos(azimuth + kPi * 0.5f) * frame.basis_u
            + std::sin(azimuth + kPi * 0.5f) * frame.basis_v);
        glm::vec3 leaf_normal = glm::normalize(
            glm::mix(radial_dir, random_lateral, orientation_randomness * 0.65f)
            + frame.axis
                * ((hash_to_unit_float(
                        params.seed + branch_id * 89U,
                        branch_id,
                        static_cast<uint32_t>(frame_index) + 509U)
                           * 2.0f
                       - 1.0f)
                    * orientation_randomness
                    * 0.35f));
        glm::vec3 leaf_up_seed = glm::normalize(
            glm::mix(frame.axis, random_lateral, orientation_randomness * 0.45f)
            + radial_dir * (orientation_randomness * 0.12f));
        glm::vec3 leaf_right = glm::cross(leaf_up_seed, leaf_normal);
        if (glm::dot(leaf_right, leaf_right) < 1e-6f)
            leaf_right = orthogonal_unit(leaf_normal);
        else
            leaf_right = glm::normalize(leaf_right);
        if (glm::dot(leaf_right, radial_dir) < 0.0f)
            leaf_right = -leaf_right;
        const glm::vec3 leaf_up = glm::normalize(glm::cross(leaf_normal, leaf_right));

        const float leaf_width = std::max(
            frame.radius
                * std::lerp(
                    min_leaf_size,
                    max_leaf_size,
                    hash_to_unit_float(
                        params.seed + branch_id * 97U,
                        branch_id,
                        static_cast<uint32_t>(frame_index) + 541U)),
            0.09f * params.overall_scale);
        const float leaf_height = leaf_width
            * std::lerp(
                1.35f,
                1.85f,
                hash_to_unit_float(
                    params.seed + branch_id * 101U,
                    branch_id,
                    static_cast<uint32_t>(frame_index) + 577U));
        const glm::vec3 leaf_center = frame.center
            + radial_dir * (frame.radius + leaf_width * 0.18f)
            + frame.axis * (leaf_height * 0.10f);

        const uint32_t atlas_index = static_cast<uint32_t>(hash_to_int(
            params.seed + branch_id * 109U,
            branch_id,
            leaf_index + 607U,
            0,
            static_cast<int>(kLeafAtlasRects.size()) - 1));

        append_double_sided_quad(
            leaf_mesh,
            leaf_center,
            leaf_right * (leaf_width * 0.5f),
            leaf_up * (leaf_height * 0.5f),
            leaf_normal,
            leaf_color_for_quad(params, branch_id, leaf_index),
            kLeafAtlasRects[atlas_index]);
        ++leaf_index;
    }
}

bool append_cap(
    GeometryMesh& mesh,
    const DraxulTreeParams& params,
    uint32_t ring_start,
    int radial_segments,
    const glm::vec3& center,
    const glm::vec3& normal,
    float v_coord,
    bool flip_winding = false);

void append_branch(
    GeometryMesh& bark_mesh,
    GeometryMesh& leaf_mesh,
    const DraxulTreeParams& params,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float length,
    float base_radius,
    float tip_radius,
    int depth,
    uint32_t branch_id,
    bool close_base,
    const BranchAttachment* attachment = nullptr)
{
    PERF_MEASURE();
    if (depth > params.max_branch_depth)
        return;
    if (length <= 0.1f || base_radius <= 0.01f)
        return;

    std::vector<BranchFrame> frames = build_branch_frames(
        params,
        origin,
        direction,
        length,
        base_radius,
        tip_radius,
        depth,
        branch_id);
    if (frames.size() < 2)
        return;

    const int radial_segments = std::max(params.radial_segments, 3);
    uint32_t ring_start = 0;
    uint32_t tip_ring_start = 0;
    if (!append_branch_rings(
            bark_mesh,
            params,
            frames,
            attachment,
            branch_id,
            ring_start,
            tip_ring_start,
            radial_segments))
        return;

    if (close_base)
    {
        (void)append_cap(
            bark_mesh,
            params,
            ring_start,
            radial_segments,
            frames.front().center,
            -frames.front().axis,
            0.0f,
            true);
    }

    (void)append_cap(
        bark_mesh,
        params,
        tip_ring_start,
        radial_segments,
        frames.back().center,
        frames.back().axis,
        1.0f,
        false);

    append_branch_leaves(leaf_mesh, params, frames, depth, branch_id);

    if (depth >= params.max_branch_depth
        || params.child_branches_max <= 0
        || base_radius < 0.025f * params.overall_scale)
    {
        return;
    }

    const int branch_count = hash_to_int(
        params.seed + branch_id * 17U,
        branch_id,
        7U + static_cast<uint32_t>(depth),
        params.child_branches_min,
        params.child_branches_max);
    if (branch_count <= 0)
        return;

    for (int child_index = 0; child_index < branch_count; ++child_index)
    {
        const float spawn_unit = hash_to_unit_float(
            params.seed + branch_id * 29U,
            static_cast<uint32_t>(child_index),
            19U + static_cast<uint32_t>(depth));
        const float spawn_start = depth == 0
            ? std::max(params.branch_spawn_start, 0.48f)
            : params.branch_spawn_start;
        const float spawn_end = std::max(params.branch_spawn_end, spawn_start + 0.05f);
        const float spawn_t = std::lerp(spawn_start, spawn_end, spawn_unit);
        const size_t frame_index = std::clamp(
            static_cast<size_t>(std::lround(spawn_t * static_cast<float>(frames.size() - 1))),
            static_cast<size_t>(1),
            frames.size() - 2);
        const BranchFrame& frame = frames[frame_index];
        const float junction_t = glm::smoothstep(
            spawn_start,
            std::min(spawn_end, spawn_start + 0.30f),
            frame.t);

        const float azimuth = hash_to_unit_float(
                                  params.seed + branch_id * 43U,
                                  static_cast<uint32_t>(child_index),
                                  31U)
            * kTwoPi;
        const glm::vec3 radial_dir = glm::normalize(
            std::cos(azimuth) * frame.basis_u
            + std::sin(azimuth) * frame.basis_v);
        const float axis_weight = std::lerp(0.18f, 0.34f, hash_to_unit_float(params.seed, branch_id, static_cast<uint32_t>(child_index) + 101U));
        const float upward = params.upward_bias * std::lerp(0.75f, 1.2f, hash_to_unit_float(params.seed, branch_id, static_cast<uint32_t>(child_index) + 121U));
        const float outward = params.outward_bias * std::lerp(0.75f, 1.15f, hash_to_unit_float(params.seed, branch_id, static_cast<uint32_t>(child_index) + 151U));
        const float droop = params.droop_bias * std::lerp(0.5f, 1.2f, hash_to_unit_float(params.seed, branch_id, static_cast<uint32_t>(child_index) + 171U));
        glm::vec3 child_direction = glm::normalize(
            frame.axis * axis_weight
            + radial_dir * outward
            + glm::vec3(0.0f, 1.0f, 0.0f) * upward
            - glm::vec3(0.0f, 1.0f, 0.0f) * droop);

        const float child_length = length
            * params.branch_length_scale
            * std::lerp(0.82f, 1.0f, junction_t)
            * std::lerp(0.72f, 1.1f, hash_to_unit_float(params.seed, branch_id, static_cast<uint32_t>(child_index) + 191U));
        const float child_base_radius = frame.radius
            * params.branch_radius_scale
            * std::lerp(depth == 0 ? 0.46f : 0.60f, 1.0f, junction_t)
            * std::lerp(0.82f, 1.08f, hash_to_unit_float(params.seed, branch_id, static_cast<uint32_t>(child_index) + 211U));
        const float child_tip_radius = std::max(
            child_base_radius
                * std::lerp(0.28f, 0.42f, hash_to_unit_float(params.seed, branch_id, static_cast<uint32_t>(child_index) + 241U)),
            0.006f * params.overall_scale);
        const glm::vec3 attachment_center = frame.center + radial_dir * frame.radius;
        const float embed_depth = std::max(
            child_base_radius * std::lerp(1.35f, 0.95f, junction_t),
            frame.radius * std::lerp(0.28f, 0.16f, junction_t));
        const glm::vec3 child_origin = attachment_center
            - child_direction * embed_depth
            - radial_dir * std::min(child_base_radius * 0.24f, frame.radius * 0.18f);
        const float embedded_length = child_length + embed_depth * 0.85f;

        append_branch(
            bark_mesh,
            leaf_mesh,
            params,
            child_origin,
            child_direction,
            embedded_length,
            child_base_radius,
            child_tip_radius,
            depth + 1,
            branch_id * 7U + static_cast<uint32_t>(child_index) + 1U,
            false,
            nullptr);
    }
}

bool append_cap(
    GeometryMesh& mesh,
    const DraxulTreeParams& params,
    uint32_t ring_start,
    int radial_segments,
    const glm::vec3& center,
    const glm::vec3& normal,
    float v_coord,
    bool flip_winding)
{
    PERF_MEASURE();
    if (!can_append_vertices(mesh, 1U))
        return false;

    GeometryVertex center_vertex;
    center_vertex.position = center;
    center_vertex.normal = normal;
    center_vertex.tangent = glm::vec4(orthogonal_unit(normal), 1.0f);
    center_vertex.color = bark_color_for_vertex(params, v_coord, static_cast<uint32_t>(v_coord * 1024.0f), 0);
    center_vertex.uv = { 0.5f, v_coord };

    const uint32_t center_index = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(center_vertex);

    for (int segment = 0; segment < radial_segments; ++segment)
    {
        const uint32_t a = ring_start + static_cast<uint32_t>(segment);
        const uint32_t b = ring_start + static_cast<uint32_t>(segment + 1);
        if (flip_winding)
        {
            mesh.indices.push_back(center_index);
            mesh.indices.push_back(b);
            mesh.indices.push_back(a);
        }
        else
        {
            mesh.indices.push_back(center_index);
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
        }
    }
    return true;
}

} // namespace

DraxulTreeParams make_tree_params_from_age(float age_years)
{
    PERF_MEASURE();
    DraxulTreeParams params;
    const float clamped_age = std::clamp(age_years, 0.5f, 40.0f);
    const float t = (clamped_age - 0.5f) / (40.0f - 0.5f);

    params.age_years = clamped_age;
    params.overall_scale = std::lerp(0.35f, 1.0f, t);
    params.max_height = std::lerp(2.5f, 18.0f, t);
    params.max_canopy_radius = std::lerp(1.0f, 7.0f, t);
    params.radial_segments = std::max(6, static_cast<int>(std::lround(std::lerp(6.0f, 12.0f, t))));
    params.trunk_length = std::lerp(1.5f, 6.0f, t);
    params.trunk_base_radius = std::lerp(0.08f, 0.35f, t);
    params.trunk_tip_radius = std::lerp(0.03f, 0.18f, t);
    params.base_ring_spacing = std::lerp(0.25f, 0.45f, t);
    params.max_branch_depth = std::max(1, static_cast<int>(std::lround(std::lerp(1.0f, 3.0f, t))));
    params.child_branches_min = t < 0.2f ? 0 : 1;
    params.child_branches_max = std::max(params.child_branches_min, static_cast<int>(std::lround(std::lerp(1.0f, 3.0f, t))));
    params.trunk_wander = std::lerp(0.04f, 0.16f, t);
    params.branch_wander = std::lerp(0.12f, 0.34f, t);
    params.wander_frequency = std::lerp(0.12f, 0.30f, t);
    params.wander_deviation = std::lerp(0.18f, 0.55f, t);
    params.leaf_density = std::lerp(0.45f, 1.15f, t);
    params.leaf_orientation_randomness = std::lerp(0.18f, 0.45f, t);
    params.leaf_size_range = glm::vec2(std::lerp(2.8f, 3.6f, t), std::lerp(4.2f, 5.2f, t));
    params.leaf_start_depth = t < 0.18f ? 0 : 1;
    return params;
}

DraxulTreeMeshes generate_draxul_tree_meshes(const DraxulTreeParams& input_params)
{
    PERF_MEASURE();
    DraxulTreeMeshes meshes;

    DraxulTreeParams params = input_params;
    params.age_years = std::max(params.age_years, 0.5f);
    params.overall_scale = std::max(params.overall_scale, 0.01f);
    params.radial_segments = std::max(params.radial_segments, 3);
    params.trunk_length = std::max(params.trunk_length, 0.25f);
    params.trunk_base_radius = std::max(params.trunk_base_radius, 0.02f);
    params.trunk_tip_radius = std::clamp(params.trunk_tip_radius, 0.01f, params.trunk_base_radius);
    params.base_ring_spacing = std::max(params.base_ring_spacing, 0.05f);
    params.tip_ring_spacing_scale = std::clamp(params.tip_ring_spacing_scale, 0.1f, 1.0f);
    params.taper_power = std::max(params.taper_power, 0.1f);
    params.child_branches_min = std::max(params.child_branches_min, 0);
    params.child_branches_max = std::max(params.child_branches_max, params.child_branches_min);
    params.max_branch_depth = std::max(params.max_branch_depth, 0);
    params.branch_length_scale = std::clamp(params.branch_length_scale, 0.1f, 1.0f);
    params.branch_radius_scale = std::clamp(params.branch_radius_scale, 0.1f, 1.0f);
    params.branch_spawn_start = std::clamp(params.branch_spawn_start, 0.0f, 0.95f);
    params.branch_spawn_end = std::clamp(params.branch_spawn_end, params.branch_spawn_start, 1.0f);
    params.curvature = std::clamp(params.curvature, 0.0f, 1.0f);
    params.trunk_wander = std::clamp(params.trunk_wander, 0.0f, 2.0f);
    params.branch_wander = std::clamp(params.branch_wander, 0.0f, 2.0f);
    params.wander_frequency = std::clamp(params.wander_frequency, 0.0f, 1.0f);
    params.wander_deviation = std::clamp(params.wander_deviation, 0.0f, 2.0f);
    params.leaf_density = std::clamp(params.leaf_density, 0.0f, 10.0f);
    params.leaf_orientation_randomness = std::clamp(params.leaf_orientation_randomness, 0.0f, 1.0f);
    params.leaf_size_range = glm::clamp(params.leaf_size_range, glm::vec2(0.1f), glm::vec2(12.0f));
    params.leaf_start_depth = std::max(params.leaf_start_depth, 0);

    const float scale = params.overall_scale;
    const float trunk_length = std::min(params.trunk_length * scale, params.max_height);
    const float base_radius = params.trunk_base_radius * scale;
    const float tip_radius = params.trunk_tip_radius * scale;
    meshes.bark_mesh.vertices.reserve(8192);
    meshes.bark_mesh.indices.reserve(24576);
    meshes.leaf_mesh.vertices.reserve(4096);
    meshes.leaf_mesh.indices.reserve(6144);

    append_branch(
        meshes.bark_mesh,
        meshes.leaf_mesh,
        params,
        glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        trunk_length,
        base_radius,
        tip_radius,
        0,
        1U,
        true);

    return meshes;
}

GeometryMesh generate_draxul_tree(const DraxulTreeParams& params)
{
    PERF_MEASURE();
    DraxulTreeMeshes split = generate_draxul_tree_meshes(params);
    GeometryMesh merged = std::move(split.bark_mesh);
    if (split.leaf_mesh.vertices.empty())
        return merged;
    if (!can_append_vertices(merged, split.leaf_mesh.vertices.size()))
        return merged;

    const uint16_t vertex_offset = static_cast<uint16_t>(merged.vertices.size());
    merged.vertices.insert(
        merged.vertices.end(),
        split.leaf_mesh.vertices.begin(),
        split.leaf_mesh.vertices.end());
    merged.indices.reserve(merged.indices.size() + split.leaf_mesh.indices.size());
    for (uint16_t index : split.leaf_mesh.indices)
        merged.indices.push_back(static_cast<uint16_t>(vertex_offset + index));
    return merged;
}

} // namespace draxul
