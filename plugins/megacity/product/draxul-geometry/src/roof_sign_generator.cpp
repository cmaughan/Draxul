#include <draxul/perf_timing.h>
#include <draxul/roof_sign_generator.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <limits>
#include <vector>

namespace draxul
{

namespace
{

constexpr size_t kMaxMeshVertices = static_cast<size_t>(std::numeric_limits<uint16_t>::max());

struct SignContour
{
    std::vector<glm::vec2> points;
};

bool can_append_vertices(const GeometryMesh& mesh, size_t additional_vertices)
{
    return mesh.vertices.size() + additional_vertices <= kMaxMeshVertices;
}

SignContour make_sign_contour(int sides, float radius)
{
    PERF_MEASURE();
    SignContour contour;
    const int clamped_sides = std::max(sides, 3);
    const float clamped_radius = std::max(radius, 0.05f);

    contour.points.reserve(static_cast<size_t>(clamped_sides));
    if (clamped_sides == 4)
    {
        contour.points.push_back({ -clamped_radius, clamped_radius });
        contour.points.push_back({ clamped_radius, clamped_radius });
        contour.points.push_back({ clamped_radius, -clamped_radius });
        contour.points.push_back({ -clamped_radius, -clamped_radius });
    }
    else
    {
        for (int i = 0; i < clamped_sides; ++i)
        {
            const float angle = glm::half_pi<float>()
                - (glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(clamped_sides));
            contour.points.push_back(glm::vec2(std::cos(angle), std::sin(angle)) * clamped_radius);
        }
    }

    return contour;
}

void append_quad(GeometryMesh& mesh, const std::array<glm::vec3, 4>& positions, const glm::vec3& normal,
    const glm::vec3& tangent,
    const std::array<glm::vec2, 4>& uvs = { {
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 1.0f, 1.0f },
        { 0.0f, 1.0f },
    } },
    float tex_blend = 0.0f, const glm::vec3& color = glm::vec3(1.0f))
{
    PERF_MEASURE();
    if (!can_append_vertices(mesh, 4))
        return;

    const uint16_t base = static_cast<uint16_t>(mesh.vertices.size());
    for (size_t i = 0; i < positions.size(); ++i)
    {
        GeometryVertex vertex;
        vertex.position = positions[i];
        vertex.normal = normal;
        vertex.color = color;
        vertex.uv = uvs[i];
        vertex.tex_blend = tex_blend;
        vertex.tangent = glm::vec4(tangent, 1.0f);
        mesh.vertices.push_back(vertex);
    }

    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
}

} // namespace

GeometryMesh generate_draxul_roof_sign(const DraxulRoofSignParams& input_params)
{
    PERF_MEASURE();
    DraxulRoofSignParams params = input_params;
    params.sides = std::max(params.sides, 3);
    params.inner_radius = std::max(params.inner_radius, 0.05f);
    params.band_depth = std::max(params.band_depth, 0.02f);
    params.height = std::max(params.height, 0.05f);

    GeometryMesh mesh;
    mesh.vertices.reserve(static_cast<size_t>(params.sides) * 16);
    mesh.indices.reserve(static_cast<size_t>(params.sides) * 24);

    const SignContour outer = make_sign_contour(params.sides, params.inner_radius + params.band_depth);
    const SignContour inner = make_sign_contour(params.sides, params.inner_radius);
    const float half_height = params.height * 0.5f;

    for (size_t side = 0; side < outer.points.size(); ++side)
    {
        const size_t next = (side + 1) % outer.points.size();
        const glm::vec2 outer_a = outer.points[side];
        const glm::vec2 outer_b = outer.points[next];
        const glm::vec2 inner_a = inner.points[side];
        const glm::vec2 inner_b = inner.points[next];

        const glm::vec2 edge = outer_b - outer_a;
        if (glm::length(edge) <= 1e-6f)
            continue;

        const glm::vec2 outward_2d = glm::normalize(glm::vec2(-edge.y, edge.x));
        const glm::vec3 outward_normal(outward_2d.x, 0.0f, outward_2d.y);
        const glm::vec3 outward_tangent = glm::normalize(glm::vec3(edge.x, 0.0f, edge.y));

        append_quad(
            mesh,
            { {
                { outer_a.x, -half_height, outer_a.y },
                { outer_b.x, -half_height, outer_b.y },
                { outer_b.x, half_height, outer_b.y },
                { outer_a.x, half_height, outer_a.y },
            } },
            outward_normal,
            outward_tangent,
            { {
                { 0.0f, 1.0f },
                { 1.0f, 1.0f },
                { 1.0f, 0.0f },
                { 0.0f, 0.0f },
            } },
            1.0f,
            params.color);

        append_quad(
            mesh,
            { {
                { inner_b.x, -half_height, inner_b.y },
                { inner_a.x, -half_height, inner_a.y },
                { inner_a.x, half_height, inner_a.y },
                { inner_b.x, half_height, inner_b.y },
            } },
            -outward_normal,
            -outward_tangent,
            { {
                { 0.0f, 1.0f },
                { 1.0f, 1.0f },
                { 1.0f, 0.0f },
                { 0.0f, 0.0f },
            } },
            0.0f,
            params.color);

        append_quad(
            mesh,
            { {
                { outer_a.x, half_height, outer_a.y },
                { outer_b.x, half_height, outer_b.y },
                { inner_b.x, half_height, inner_b.y },
                { inner_a.x, half_height, inner_a.y },
            } },
            { 0.0f, 1.0f, 0.0f },
            outward_tangent,
            { {
                { 0.0f, 0.0f },
                { 1.0f, 0.0f },
                { 1.0f, 1.0f },
                { 0.0f, 1.0f },
            } },
            0.0f,
            params.color);

        append_quad(
            mesh,
            { {
                { inner_a.x, -half_height, inner_a.y },
                { inner_b.x, -half_height, inner_b.y },
                { outer_b.x, -half_height, outer_b.y },
                { outer_a.x, -half_height, outer_a.y },
            } },
            { 0.0f, -1.0f, 0.0f },
            outward_tangent,
            { {
                { 0.0f, 0.0f },
                { 1.0f, 0.0f },
                { 1.0f, 1.0f },
                { 0.0f, 1.0f },
            } },
            0.0f,
            params.color);
    }

    return mesh;
}

} // namespace draxul
