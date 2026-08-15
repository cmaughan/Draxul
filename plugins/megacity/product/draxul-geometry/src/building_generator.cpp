#include <draxul/building_generator.h>
#include <draxul/perf_timing.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/norm.hpp>
#include <limits>
#include <vector>

namespace draxul
{

namespace
{

constexpr size_t kMaxMeshVertices = static_cast<size_t>(std::numeric_limits<uint16_t>::max());

struct RingContour
{
    std::vector<glm::vec2> points;
    std::vector<float> perimeter_prefix;
    float perimeter = 1.0f;
};

bool can_append_vertices(const GeometryMesh& mesh, size_t additional_vertices)
{
    return mesh.vertices.size() + additional_vertices <= kMaxMeshVertices;
}

RingContour make_ring_contour(int sides, float footprint, float scale)
{
    PERF_MEASURE();
    RingContour contour;
    const int clamped_sides = std::max(sides, 3);
    const float clamped_scale = std::max(scale, 0.05f);
    const float half = std::max(footprint, 0.1f) * 0.5f;

    contour.points.reserve(static_cast<size_t>(clamped_sides));
    if (clamped_sides == 4)
    {
        contour.points.push_back({ -half * clamped_scale, half * clamped_scale });
        contour.points.push_back({ half * clamped_scale, half * clamped_scale });
        contour.points.push_back({ half * clamped_scale, -half * clamped_scale });
        contour.points.push_back({ -half * clamped_scale, -half * clamped_scale });
    }
    else
    {
        for (int i = 0; i < clamped_sides; ++i)
        {
            const float angle
                = glm::half_pi<float>() - (glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(clamped_sides));
            contour.points.push_back(
                glm::vec2(std::cos(angle), std::sin(angle)) * half * clamped_scale);
        }
    }

    contour.perimeter_prefix.resize(contour.points.size() + 1, 0.0f);
    for (size_t i = 0; i < contour.points.size(); ++i)
    {
        const glm::vec2 a = contour.points[i];
        const glm::vec2 b = contour.points[(i + 1) % contour.points.size()];
        contour.perimeter_prefix[i + 1] = contour.perimeter_prefix[i] + glm::distance(a, b);
    }
    contour.perimeter = std::max(contour.perimeter_prefix.back(), 1e-4f);
    return contour;
}

void append_side_strip(GeometryMesh& mesh, const RingContour& lower_contour, const RingContour& upper_contour,
    float lower_y, float upper_y, float total_height, const glm::vec3& color, uint32_t layer_id)
{
    PERF_MEASURE();
    if (lower_contour.points.size() != upper_contour.points.size() || lower_contour.points.size() < 3)
        return;

    const size_t side_count = lower_contour.points.size();
    for (size_t side = 0; side < side_count; ++side)
    {
        if (!can_append_vertices(mesh, 4))
            return;

        const size_t next = (side + 1) % side_count;
        const glm::vec2 lower_a = lower_contour.points[side];
        const glm::vec2 lower_b = lower_contour.points[next];
        const glm::vec2 upper_a = upper_contour.points[side];
        const glm::vec2 upper_b = upper_contour.points[next];

        const glm::vec3 p0(lower_a.x, lower_y, lower_a.y);
        const glm::vec3 p1(lower_b.x, lower_y, lower_b.y);
        const glm::vec3 p2(upper_b.x, upper_y, upper_b.y);
        const glm::vec3 p3(upper_a.x, upper_y, upper_a.y);

        const glm::vec3 tangent = glm::normalize(p1 - p0);
        glm::vec3 normal = glm::cross(p1 - p0, p3 - p0);
        if (glm::length2(normal) <= 1e-8f)
            normal = glm::cross(p2 - p1, p0 - p1);
        normal = glm::normalize(normal);

        const float u0 = lower_contour.perimeter_prefix[side] / lower_contour.perimeter;
        const float u1 = lower_contour.perimeter_prefix[side + 1] / lower_contour.perimeter;
        const float v0 = lower_y / total_height;
        const float v1 = upper_y / total_height;

        const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
        for (const auto& [position, uv] : std::array<std::pair<glm::vec3, glm::vec2>, 4>{ {
                 { p0, { u0, v0 } },
                 { p1, { u1, v0 } },
                 { p2, { u1, v1 } },
                 { p3, { u0, v1 } },
             } })
        {
            GeometryVertex vertex;
            vertex.position = position;
            vertex.normal = normal;
            vertex.color = color;
            vertex.uv = uv;
            vertex.tangent = glm::vec4(tangent, 1.0f);
            vertex.layer_id = static_cast<float>(layer_id);
            mesh.vertices.push_back(vertex);
        }

        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }
}

void append_cap(GeometryMesh& mesh, const RingContour& contour, float y, bool top, const glm::vec3& color, float footprint,
    uint32_t layer_id)
{
    PERF_MEASURE();
    if (contour.points.size() < 3 || !can_append_vertices(mesh, contour.points.size() + 1))
        return;

    const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
    GeometryVertex center;
    center.position = { 0.0f, y, 0.0f };
    center.normal = top ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, -1.0f, 0.0f);
    center.color = color;
    center.uv = { 0.5f, 0.5f };
    center.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    center.layer_id = static_cast<float>(layer_id);
    mesh.vertices.push_back(center);

    const float inv_footprint = 1.0f / std::max(footprint, 0.1f);
    for (const glm::vec2& point : contour.points)
    {
        GeometryVertex vertex;
        vertex.position = { point.x, y, point.y };
        vertex.normal = center.normal;
        vertex.color = color;
        vertex.uv = glm::vec2(point.x * inv_footprint + 0.5f, point.y * inv_footprint + 0.5f);
        vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        vertex.layer_id = static_cast<float>(layer_id);
        mesh.vertices.push_back(vertex);
    }

    const uint16_t side_count = static_cast<uint16_t>(contour.points.size());
    for (uint16_t i = 0; i < side_count; ++i)
    {
        const uint16_t current = base + 1 + i;
        const uint16_t next = base + 1 + static_cast<uint16_t>((i + 1) % side_count);
        if (top)
        {
            mesh.indices.push_back(base);
            mesh.indices.push_back(current);
            mesh.indices.push_back(next);
        }
        else
        {
            mesh.indices.push_back(base);
            mesh.indices.push_back(next);
            mesh.indices.push_back(current);
        }
    }
}

} // namespace

GeometryMesh generate_draxul_building(const DraxulBuildingParams& input_params)
{
    PERF_MEASURE();
    DraxulBuildingParams params = input_params;
    params.footprint = std::max(params.footprint, 0.1f);
    params.sides = std::max(params.sides, 3);
    params.middle_strip_scale = std::max(params.middle_strip_scale, 0.05f);

    params.levels.erase(
        std::remove_if(
            params.levels.begin(),
            params.levels.end(),
            [](const DraxulBuildingLevel& level) { return level.height <= 1e-4f; }),
        params.levels.end());
    if (params.levels.empty())
        params.levels.push_back({ 1.0f, glm::vec3(1.0f), 0u });

    float total_height = 0.0f;
    for (const DraxulBuildingLevel& level : params.levels)
        total_height += level.height;
    if (params.level_gap > 0.0f && params.levels.size() > 1)
        total_height += params.level_gap * static_cast<float>(params.levels.size() - 1);
    total_height = std::max(total_height, 1e-4f);

    GeometryMesh mesh;
    mesh.vertices.reserve(params.levels.size() * static_cast<size_t>(params.sides) * 12 + static_cast<size_t>(params.sides) * 4);
    mesh.indices.reserve(params.levels.size() * static_cast<size_t>(params.sides) * 18 + static_cast<size_t>(params.sides) * 6);

    const RingContour outer_contour = make_ring_contour(params.sides, params.footprint, 1.0f);
    const RingContour middle_contour = make_ring_contour(params.sides, params.footprint, params.middle_strip_scale);

    const bool has_gaps = params.level_gap > 0.0f && params.levels.size() > 1;

    float current_y = 0.0f;
    for (size_t li = 0; li < params.levels.size(); ++li)
    {
        const DraxulBuildingLevel& level = params.levels[li];
        const float strip_height = level.height / 3.0f;
        append_side_strip(
            mesh,
            outer_contour,
            middle_contour,
            current_y,
            current_y + strip_height,
            total_height,
            level.color,
            level.layer_id);
        append_side_strip(
            mesh,
            middle_contour,
            middle_contour,
            current_y + strip_height,
            current_y + strip_height * 2.0f,
            total_height,
            level.color,
            level.layer_id);
        append_side_strip(
            mesh,
            middle_contour,
            outer_contour,
            current_y + strip_height * 2.0f,
            current_y + level.height,
            total_height,
            level.color,
            level.layer_id);

        if (has_gaps)
        {
            // Bottom cap for this plate (except first plate which gets the global bottom cap).
            if (li > 0)
                append_cap(mesh, outer_contour, current_y, false, level.color, params.footprint, level.layer_id);
            // Top cap for this plate (except last plate which gets the global top cap).
            if (li + 1 < params.levels.size())
                append_cap(mesh, outer_contour, current_y + level.height, true, level.color, params.footprint, level.layer_id);
        }

        current_y += level.height;
        if (has_gaps && li + 1 < params.levels.size())
            current_y += params.level_gap;
    }

    append_cap(
        mesh,
        outer_contour,
        0.0f,
        false,
        params.levels.front().color,
        params.footprint,
        params.levels.front().layer_id);
    append_cap(
        mesh,
        outer_contour,
        total_height,
        true,
        params.levels.back().color,
        params.footprint,
        params.levels.back().layer_id);
    return mesh;
}

GeometryMesh generate_sidewalk_ring(
    int sides, float inner_radius, float outer_radius, float y, float height,
    const glm::vec3& color)
{
    PERF_MEASURE();
    GeometryMesh mesh;
    const RingContour inner = make_ring_contour(sides, inner_radius * 2.0f, 1.0f);
    const RingContour outer = make_ring_contour(sides, outer_radius * 2.0f, 1.0f);
    if (inner.points.size() < 3 || inner.points.size() != outer.points.size())
        return mesh;

    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);
    const size_t n = inner.points.size();
    // Top face: ring between inner and outer contours.
    const float top_y = y + height;
    for (size_t i = 0; i < n; ++i)
    {
        const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
        const size_t next = (i + 1) % n;

        GeometryVertex vi0, vo0, vi1, vo1;
        vi0.position = { inner.points[i].x, top_y, inner.points[i].y };
        vo0.position = { outer.points[i].x, top_y, outer.points[i].y };
        vi1.position = { inner.points[next].x, top_y, inner.points[next].y };
        vo1.position = { outer.points[next].x, top_y, outer.points[next].y };

        for (auto* v : { &vi0, &vo0, &vi1, &vo1 })
        {
            v->normal = up;
            v->color = color;
            v->tangent = tangent;
            v->uv = glm::vec2(v->position.x, v->position.z);
        }

        mesh.vertices.push_back(vi0);
        mesh.vertices.push_back(vo0);
        mesh.vertices.push_back(vi1);
        mesh.vertices.push_back(vo1);

        // Two triangles: vi0-vo0-vo1, vi0-vo1-vi1
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 3);
        mesh.indices.push_back(base);
        mesh.indices.push_back(base + 3);
        mesh.indices.push_back(base + 2);
    }

    return mesh;
}

namespace
{

void append_box_face(
    GeometryMesh& mesh,
    const glm::vec3& p0,
    const glm::vec3& p1,
    const glm::vec3& p2,
    const glm::vec3& p3,
    const glm::vec3& normal,
    float total_height,
    const glm::vec3& color,
    uint32_t layer_id)
{
    if (!can_append_vertices(mesh, 4))
        return;
    const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
    const glm::vec3 tangent_dir = glm::normalize(p1 - p0);
    for (const auto& pos : { p0, p1, p2, p3 })
    {
        GeometryVertex vertex;
        vertex.position = pos;
        vertex.normal = normal;
        vertex.color = color;
        vertex.uv = { 0.0f, total_height > 1e-4f ? pos.y / total_height : 0.0f };
        vertex.tangent = glm::vec4(tangent_dir, 1.0f);
        vertex.layer_id = static_cast<float>(layer_id);
        mesh.vertices.push_back(vertex);
    }
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
}

void append_box(
    GeometryMesh& mesh,
    float x_min, float z_min, float x_max, float z_max,
    float y_min, float y_max,
    float total_height,
    const glm::vec3& color,
    uint32_t layer_id)
{
    // Front face (+Z)
    append_box_face(mesh,
        { x_min, y_min, z_max }, { x_max, y_min, z_max },
        { x_max, y_max, z_max }, { x_min, y_max, z_max },
        { 0.0f, 0.0f, 1.0f }, total_height, color, layer_id);
    // Back face (-Z)
    append_box_face(mesh,
        { x_max, y_min, z_min }, { x_min, y_min, z_min },
        { x_min, y_max, z_min }, { x_max, y_max, z_min },
        { 0.0f, 0.0f, -1.0f }, total_height, color, layer_id);
    // Right face (+X)
    append_box_face(mesh,
        { x_max, y_min, z_max }, { x_max, y_min, z_min },
        { x_max, y_max, z_min }, { x_max, y_max, z_max },
        { 1.0f, 0.0f, 0.0f }, total_height, color, layer_id);
    // Left face (-X)
    append_box_face(mesh,
        { x_min, y_min, z_min }, { x_min, y_min, z_max },
        { x_min, y_max, z_max }, { x_min, y_max, z_min },
        { -1.0f, 0.0f, 0.0f }, total_height, color, layer_id);
    // Top face (+Y)
    append_box_face(mesh,
        { x_min, y_max, z_min }, { x_min, y_max, z_max },
        { x_max, y_max, z_max }, { x_max, y_max, z_min },
        { 0.0f, 1.0f, 0.0f }, total_height, color, layer_id);
    // Bottom face (-Y)
    append_box_face(mesh,
        { x_min, y_min, z_max }, { x_min, y_min, z_min },
        { x_max, y_min, z_min }, { x_max, y_min, z_max },
        { 0.0f, -1.0f, 0.0f }, total_height, color, layer_id);
}

} // anonymous namespace

GeometryMesh generate_draxul_brick_building(const DraxulBrickBuildingParams& input_params)
{
    PERF_MEASURE();
    const int grid_size = std::max(input_params.grid_size, 1);
    const float footprint = std::max(input_params.footprint, 0.1f);
    const float brick_gap = std::max(input_params.brick_gap, 0.0f);
    const float floor_gap = std::max(input_params.floor_gap, 0.0f);

    // Hollow ring: for N>=3 only the outer perimeter is used.
    auto slots_per_floor = [](int n) { return n <= 2 ? n * n : 4 * (n - 1); };
    auto slot_pos = [](int idx, int n) -> std::pair<int, int> {
        if (n <= 2)
            return { idx % n, idx / n };
        const int perim = 4 * (n - 1);
        const int i = idx % perim;
        if (i < n)
            return { i, 0 };
        if (i < n + n - 2)
            return { n - 1, i - (n - 1) };
        if (i < 2 * n + n - 2)
            return { n - 1 - (i - (n + n - 2)), n - 1 };
        return { 0, n - 1 - (i - (2 * n + n - 3)) };
    };
    const int bricks_per_floor = slots_per_floor(grid_size);

    if (input_params.bricks.empty())
        return GeometryMesh{};

    const int num_bricks = static_cast<int>(input_params.bricks.size());
    const int num_floors = (num_bricks + bricks_per_floor - 1) / bricks_per_floor;

    // Compute per-floor height (max of all bricks on that floor).
    std::vector<float> floor_heights(static_cast<size_t>(num_floors), 0.0f);
    for (int i = 0; i < num_bricks; ++i)
    {
        const int floor = i / bricks_per_floor;
        floor_heights[floor] = std::max(floor_heights[floor], input_params.bricks[i].height);
    }

    float total_height = 0.0f;
    for (float h : floor_heights)
        total_height += h;
    if (num_floors > 1)
        total_height += floor_gap * static_cast<float>(num_floors - 1);
    total_height = std::max(total_height, 1e-4f);

    // Brick dimensions within the footprint.
    const float half = footprint * 0.5f;
    const float total_gap = static_cast<float>(grid_size - 1) * brick_gap;
    const float brick_size = std::max((footprint - total_gap) / static_cast<float>(grid_size), 0.01f);

    GeometryMesh mesh;
    mesh.vertices.reserve(static_cast<size_t>(num_bricks) * 24);
    mesh.indices.reserve(static_cast<size_t>(num_bricks) * 36);

    float current_y = 0.0f;
    for (int floor = 0; floor < num_floors; ++floor)
    {
        const float floor_height = floor_heights[floor];
        const int start = floor * bricks_per_floor;
        const int end = std::min(start + bricks_per_floor, num_bricks);

        for (int bi = start; bi < end; ++bi)
        {
            const int local = bi - start;
            const auto [col, row] = slot_pos(local, grid_size);

            const float x_min = -half + static_cast<float>(col) * (brick_size + brick_gap);
            const float z_min = -half + static_cast<float>(row) * (brick_size + brick_gap);

            append_box(mesh,
                x_min, z_min,
                x_min + brick_size, z_min + brick_size,
                current_y, current_y + floor_height,
                total_height,
                input_params.bricks[bi].color,
                input_params.bricks[bi].layer_id);
        }

        current_y += floor_height;
        if (floor + 1 < num_floors)
            current_y += floor_gap;
    }

    return mesh;
}

} // namespace draxul
