#include "biology_builder.h"

#include <draxul/cell_generator.h>
#include <draxul/codeviz_scene_world.h>
#include <draxul/log.h>
#include <draxul/pastel_palette.h>
#include <draxul/perf_timing.h>
#include <draxul/primitive_meshes.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// BioView tissue — the whole codebase grown as a biological organism.
//
//   Repository -> organism
//   Module     -> a tissue territory (a soft, colored patch on the floor)
//   Type       -> a cell packed into its module's tissue
//   Type members -> organelles inside the cell (methods -> mitochondria,
//                   fields -> ribosomes, member roster -> DNA rungs,
//                   inheritance -> Golgi, dependencies -> vesicles,
//                   oversized methods -> lysosomes, organizer -> centrosome)
//   Cross-module dependencies -> blood vessels between tissues
//
// The most significant classes render as full detailed cells; the rest render
// as simpler cells so hundreds of types stay affordable. Every cell/organelle
// carries a CodeVizSemanticRef back to its semantic node. Everything is
// deterministic (placement seeded from stable hashes); an empty snapshot grows
// a single generic cell so `--host bio` always shows something.

namespace draxul
{

namespace
{

// --- Palette -----------------------------------------------------------------
constexpr glm::vec3 kMembraneHealthy{ 0.32f, 0.82f, 0.46f };
constexpr glm::vec3 kMembraneMid{ 0.96f, 0.74f, 0.28f };
constexpr glm::vec3 kMembraneSick{ 0.92f, 0.28f, 0.22f };
constexpr glm::vec3 kNucleusTop{ 0.78f, 0.56f, 0.98f };
constexpr glm::vec3 kNucleusBottom{ 0.46f, 0.30f, 0.80f };
constexpr glm::vec3 kNucleolus{ 0.86f, 0.22f, 0.56f };
constexpr glm::vec3 kMethodCool{ 0.98f, 0.74f, 0.36f };
constexpr glm::vec3 kMethodHot{ 0.88f, 0.18f, 0.14f };
constexpr glm::vec3 kRibosome{ 0.36f, 0.80f, 0.95f };
constexpr glm::vec3 kRibosomeLinked{ 0.42f, 0.92f, 0.62f };
constexpr glm::vec3 kEr{ 0.28f, 0.72f, 0.50f };
constexpr glm::vec3 kGolgiA{ 0.62f, 0.44f, 0.92f };
constexpr glm::vec3 kGolgiB{ 0.40f, 0.28f, 0.72f };
constexpr glm::vec4 kVesicle{ 0.46f, 0.90f, 0.62f, 0.62f };
constexpr glm::vec3 kLysosome{ 0.64f, 0.30f, 0.86f };
constexpr glm::vec3 kCentriole{ 0.86f, 0.86f, 0.42f };
constexpr glm::vec3 kMrna{ 0.98f, 0.86f, 0.28f };
constexpr glm::vec3 kVessel{ 0.78f, 0.12f, 0.18f };

constexpr float kMethodLineHot = 50.0f;
constexpr float kMethodLineSmell = 60.0f;
constexpr float kCouplingReference = 12.0f;
constexpr float kMemberReference = 30.0f;

constexpr size_t kMaxMitochondria = 40;
constexpr size_t kMaxDnaRungs = 60;
constexpr size_t kMaxRibosomes = 60;
constexpr size_t kMaxVesicles = 30;
constexpr size_t kMaxLysosomes = 12;
constexpr int kMaxGolgiCisternae = 6;

// Tissue-scale limits.
constexpr size_t kMaxFullCells = 10; // full-detail organelle cells
constexpr size_t kMaxCells = 640; // total cells (drop smallest beyond)
constexpr size_t kMaxVessels = 20;
constexpr int kMinVesselEdges = 3; // module coupling needed to grow a vessel

float sat(float x)
{
    return glm::clamp(x, 0.0f, 1.0f);
}

glm::vec3 health_ramp(float health)
{
    health = sat(health);
    return health < 0.5f
        ? glm::mix(kMembraneSick, kMembraneMid, health * 2.0f)
        : glm::mix(kMembraneMid, kMembraneHealthy, (health - 0.5f) * 2.0f);
}

const CodeSemanticNode* find_node(const CodeSemanticSnapshot& semantics, CodeSemanticNodeId id)
{
    const auto it = semantics.indexes.node_index_by_id.find(id);
    if (it == semantics.indexes.node_index_by_id.end() || it->second >= semantics.nodes.size())
        return nullptr;
    return &semantics.nodes[it->second];
}

std::string_view display_name(const CodeSemanticNode& node)
{
    return !node.qualified_name.empty() ? std::string_view(node.qualified_name) : std::string_view(node.name);
}

CodeVizSemanticRef ref_for(const CodeSemanticNode& node)
{
    return CodeVizSemanticRef{ node.source.file_path, std::string(display_name(node)), node.module_path, node.id, 0 };
}

long significance(const CodeSemanticNode& node)
{
    return 4L * node.metrics.method_count
        + std::min<long>(node.metrics.field_count, 24)
        + 2L * node.metrics.referenced_type_count;
}

// A cell placed somewhere in the tissue: world center, uniform scale, and the
// unscaled local geometry (cell-centered at the origin, y=0 at mid-height).
struct CellFrame
{
    glm::vec3 center{ 0.0f, 0.0f, 0.0f };
    float scale = 1.0f;
    float cx = 6.4f;
    float cy = 2.7f;
    float cz = 4.7f;
    glm::vec3 nucleus_center{ -1.9f, 0.25f, -0.9f };
    glm::vec3 nucleus_radii{ 2.5f, 1.85f, 2.2f };
};

glm::vec3 sample_at(uint64_t hash, const CellFrame& f, float shrink, float nucleus_clearance)
{
    std::mt19937_64 rng(hash);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (int attempt = 0; attempt < 96; ++attempt)
    {
        const glm::vec3 u(dist(rng), dist(rng), dist(rng));
        if (glm::dot(u, u) > 1.0f)
            continue;
        const glm::vec3 p(u.x * f.cx * shrink, u.y * f.cy * shrink, u.z * f.cz * shrink);
        const glm::vec3 rel = (p - f.nucleus_center) / f.nucleus_radii;
        if (glm::dot(rel, rel) < nucleus_clearance * nucleus_clearance)
            continue;
        return p;
    }
    return glm::vec3(f.cx * 0.6f, 0.0f, f.cz * 0.4f);
}

glm::mat4 orientation_at(uint64_t hash)
{
    std::mt19937_64 rng(hash);
    std::uniform_real_distribution<float> dist(0.0f, glm::two_pi<float>());
    return glm::rotate(glm::mat4(1.0f), dist(rng), glm::vec3(0.0f, 1.0f, 0.0f))
        * glm::rotate(glm::mat4(1.0f), dist(rng), glm::vec3(1.0f, 0.0f, 0.0f))
        * glm::rotate(glm::mat4(1.0f), dist(rng), glm::vec3(0.0f, 0.0f, 1.0f));
}

// --- Frame-aware placement ---------------------------------------------------

void place_sphere(CodeVizSceneWorld& world, const CellFrame& f, const glm::vec3& local_center,
    const glm::vec3& local_radii, const glm::vec4& color, const std::shared_ptr<const GeometryMesh>& mesh,
    bool double_sided, const CodeVizSemanticRef& ref)
{
    const glm::vec3 radii = f.scale * local_radii;
    world.create_ellipsoid(
        f.center.x + f.scale * local_center.x,
        f.center.z + f.scale * local_center.z,
        f.center.y + f.scale * local_center.y - radii.y,
        EllipsoidMetrics{ radii.x, radii.y, radii.z }, color, ref, mesh, double_sided);
}

// Place a mesh authored in the cell-local frame (centered at origin, y=0 mid).
void place_baked(CodeVizSceneWorld& world, const CellFrame& f, const GeometryMesh& local_mesh,
    float footprint, const glm::vec4& color, const CodeVizSemanticRef& ref)
{
    GeometryMesh scaled = transform_mesh(local_mesh, glm::scale(glm::mat4(1.0f), glm::vec3(f.scale)));
    world.create_block(
        f.center.x, f.center.z, f.center.y,
        BlockMetrics{ footprint * f.scale, footprint * f.scale, 0.0f, 0.0f }, color, ref,
        CodeVizMaterialPreset::FlatColor,
        std::make_shared<const GeometryMesh>(std::move(scaled)), 0.0f, CustomMeshTransformMode::Baked);
}

// --- Organelles (full cell) --------------------------------------------------

void add_membrane(CodeVizSceneWorld& world, const CellFrame& f, uint64_t seed,
    float health, float coupling, bool is_abstract, const CodeVizSemanticRef& ref)
{
    const glm::vec3 hue = health_ramp(health);
    BlobParams membrane;
    membrane.seed = seed ^ 0x1u;
    membrane.radius = glm::vec3(0.5f);
    membrane.latitude_segments = 52;
    membrane.longitude_segments = 96;
    membrane.noise_amplitude = glm::mix(0.07f, 0.22f, sat(coupling));
    membrane.noise_frequency = 1.9f;
    membrane.color_top = glm::mix(hue, glm::vec3(1.0f), 0.15f);
    membrane.color_bottom = hue * 0.7f;
    membrane.color_noise = 0.05f;
    const auto mesh = std::make_shared<const GeometryMesh>(build_blob_mesh(membrane));
    place_sphere(world, f, glm::vec3(0.0f), glm::vec3(f.cx, f.cy, f.cz),
        glm::vec4(1.0f, 1.0f, 1.0f, is_abstract ? 0.15f : 0.24f), mesh, true, ref);

    BlobParams cytosol;
    cytosol.seed = seed ^ 0x55u;
    cytosol.radius = glm::vec3(0.5f);
    cytosol.latitude_segments = 36;
    cytosol.longitude_segments = 64;
    cytosol.noise_amplitude = 0.13f;
    cytosol.color_top = glm::mix(hue, glm::vec3(0.35f, 0.70f, 0.90f), 0.5f);
    cytosol.color_bottom = hue * 0.5f;
    const auto cytosol_mesh = std::make_shared<const GeometryMesh>(build_blob_mesh(cytosol));
    place_sphere(world, f, glm::vec3(0.0f), glm::vec3(f.cx * 0.82f, f.cy * 0.82f, f.cz * 0.82f),
        glm::vec4(1.0f, 1.0f, 1.0f, 0.11f), cytosol_mesh, true, {});
}

void add_nucleus(CodeVizSceneWorld& world, const CellFrame& f, uint64_t seed,
    bool is_abstract, size_t ancestor_count, const CodeVizSemanticRef& ref)
{
    BlobParams nucleus;
    nucleus.seed = seed ^ 0x2Bu;
    nucleus.radius = glm::vec3(0.5f);
    nucleus.latitude_segments = 40;
    nucleus.longitude_segments = 72;
    nucleus.noise_amplitude = 0.11f;
    nucleus.color_top = kNucleusTop;
    nucleus.color_bottom = kNucleusBottom;
    const auto nucleus_mesh = std::make_shared<const GeometryMesh>(build_blob_mesh(nucleus));
    place_sphere(world, f, f.nucleus_center, f.nucleus_radii,
        glm::vec4(1.0f, 1.0f, 1.0f, is_abstract ? 0.22f : 0.35f), nucleus_mesh, true, ref);

    BlobParams nucleolus;
    nucleolus.seed = seed ^ 0x9A1u;
    nucleolus.radius = glm::vec3(0.5f);
    nucleolus.noise_amplitude = 0.18f;
    const glm::vec3 core = is_abstract ? kNucleolus * 0.4f : kNucleolus;
    nucleolus.color_top = core * 1.1f;
    nucleolus.color_bottom = core * 0.7f;
    const auto nucleolus_mesh = std::make_shared<const GeometryMesh>(build_blob_mesh(nucleolus));
    const float core_radius = glm::clamp(0.55f + static_cast<float>(ancestor_count) * 0.12f, 0.55f, 1.1f);
    place_sphere(world, f, f.nucleus_center + glm::vec3(0.4f, -0.15f, 0.35f),
        glm::vec3(core_radius), glm::vec4(1.0f), nucleolus_mesh, false, ref);
}

void add_dna(CodeVizSceneWorld& world, const CellFrame& f, uint64_t seed,
    const std::vector<int>& rung_categories, float health, const CodeVizSemanticRef& ref)
{
    DnaHelixParams dna;
    dna.seed = seed;
    const int member_count = std::max<int>(6, static_cast<int>(rung_categories.size()));
    dna.length = glm::clamp(0.9f + static_cast<float>(member_count) * 0.05f, 1.6f, f.nucleus_radii.y * 1.7f);
    dna.helix_radius = 0.55f;
    dna.turns = glm::clamp(static_cast<float>(member_count) * 0.16f, 2.2f, 5.0f);
    dna.strand_radius = 0.1f;
    dna.rung_radius = 0.06f;
    dna.rung_categories = rung_categories;
    const glm::vec3 hue = health_ramp(health);
    dna.backbone_a_color = glm::mix(glm::vec3(0.98f, 0.30f, 0.55f), hue, 0.25f);
    dna.backbone_b_color = glm::mix(glm::vec3(0.30f, 0.85f, 0.95f), hue, 0.25f);
    GeometryMesh mesh = build_dna_double_helix(dna);
    const glm::mat4 xform = glm::translate(glm::mat4(1.0f), f.nucleus_center + glm::vec3(-0.2f, 0.05f, -0.15f))
        * glm::rotate(glm::mat4(1.0f), 0.5f, glm::vec3(0.0f, 0.0f, 1.0f))
        * glm::rotate(glm::mat4(1.0f), 0.7f, glm::vec3(0.0f, 1.0f, 0.0f));
    place_baked(world, f, transform_mesh(mesh, xform), 2.5f, glm::vec4(1.0f), ref);
}

void add_mitochondrion(CodeVizSceneWorld& world, const CellFrame& f, uint64_t seed,
    const glm::vec3& pos, uint32_t line_count, uint32_t ref_types, const CodeVizSemanticRef& ref)
{
    const float heat = sat(static_cast<float>(line_count) / kMethodLineHot);
    const glm::vec3 hot = glm::mix(kMethodCool, kMethodHot, heat);
    MitochondrionParams mito;
    mito.seed = seed;
    mito.length = glm::clamp(1.1f + std::sqrt(static_cast<float>(line_count)) * 0.14f, 1.3f, 3.0f);
    mito.radius = glm::clamp(0.42f + std::sqrt(static_cast<float>(line_count)) * 0.018f, 0.44f, 0.74f);
    mito.cristae = glm::clamp(static_cast<int>(ref_types), 4, 10);
    mito.outer_color_a = glm::clamp(hot * 1.05f, glm::vec3(0.0f), glm::vec3(1.0f));
    mito.outer_color_b = hot * 0.68f;
    mito.cristae_color = glm::clamp(hot * 1.25f + glm::vec3(0.06f), glm::vec3(0.0f), glm::vec3(1.0f));
    GeometryMesh mesh = build_mitochondrion(mito);
    const glm::mat4 xform = glm::translate(glm::mat4(1.0f), pos) * orientation_at(seed ^ 0x51u);
    place_baked(world, f, transform_mesh(mesh, xform), mito.length, glm::vec4(1.0f), ref);
}

void add_golgi(CodeVizSceneWorld& world, const CellFrame& f, uint64_t seed, int cisternae, const CodeVizSemanticRef& ref)
{
    GolgiParams golgi;
    golgi.seed = seed ^ 0x901D1u;
    golgi.cisternae = glm::clamp(cisternae, 1, kMaxGolgiCisternae);
    golgi.radius = 1.2f;
    golgi.thickness = 0.13f;
    golgi.spacing = 0.28f;
    golgi.curvature = 0.5f;
    golgi.color_a = kGolgiA;
    golgi.color_b = kGolgiB;
    GeometryMesh mesh = build_golgi(golgi);
    const glm::mat4 xform = glm::translate(glm::mat4(1.0f), glm::vec3(2.8f, -0.2f, 1.9f))
        * glm::rotate(glm::mat4(1.0f), 0.9f, glm::vec3(0.2f, 0.4f, 1.0f));
    place_baked(world, f, transform_mesh(mesh, xform), 2.5f, glm::vec4(1.0f), ref);
}

void add_endoplasmic_reticulum(CodeVizSceneWorld& world, const CellFrame& f, uint64_t seed, float extent)
{
    ErParams er;
    er.seed = seed ^ 0xE21Au;
    er.extent = glm::clamp(extent, 1.5f, 3.2f);
    er.tube_radius = 0.13f;
    er.strands = 3;
    er.color = kEr;
    ErResult result = build_endoplasmic_reticulum(er);
    const glm::mat4 xform = glm::translate(glm::mat4(1.0f), f.nucleus_center + glm::vec3(1.9f, 0.2f, 0.7f))
        * glm::rotate(glm::mat4(1.0f), 0.4f, glm::vec3(1.0f, 0.0f, 0.2f));
    place_baked(world, f, transform_mesh(result.mesh, xform), 3.0f, glm::vec4(1.0f), {});
}

void add_centrosome(CodeVizSceneWorld& world, const CellFrame& f, const CodeVizSemanticRef& ref)
{
    const glm::vec3 base = f.nucleus_center + glm::vec3(2.3f, 0.7f, -1.3f);
    auto make_centriole = [&](const glm::vec3& axis) {
        std::vector<glm::vec3> path = { base - axis * 0.35f, base, base + axis * 0.35f };
        return build_tube(path, 0.13f, 10, [&](float) { return kCentriole; });
    };
    place_baked(world, f, make_centriole(glm::vec3(1.0f, 0.2f, 0.0f)), 0.8f, glm::vec4(1.0f), ref);
    place_baked(world, f, make_centriole(glm::vec3(0.0f, 0.2f, 1.0f)), 0.8f, glm::vec4(1.0f), ref);
}

// --- Subject gathering (full cell) -------------------------------------------

struct SubjectData
{
    const CodeSemanticNode* type = nullptr;
    std::vector<const CodeSemanticNode*> methods;
    std::vector<const CodeSemanticNode*> fields;
    std::vector<const CodeSemanticNode*> ancestors;
    std::vector<const CodeSemanticNode*> referenced;
};

SubjectData gather_subject(const CodeSemanticSnapshot& semantics, const CodeSemanticNode& type)
{
    SubjectData data;
    data.type = &type;
    if (const auto children = semantics.indexes.nodes_by_parent.find(type.id);
        children != semantics.indexes.nodes_by_parent.end())
    {
        for (const CodeSemanticNodeId child_id : children->second)
        {
            const CodeSemanticNode* child = find_node(semantics, child_id);
            if (!child)
                continue;
            if (child->kind == CodeSemanticNodeKind::Method)
                data.methods.push_back(child);
            else if (child->kind == CodeSemanticNodeKind::Field)
                data.fields.push_back(child);
        }
    }

    std::unordered_set<CodeSemanticNodeId> member_ids;
    member_ids.insert(type.id);
    for (const CodeSemanticNode* field : data.fields)
        member_ids.insert(field->id);
    for (const CodeSemanticNode* method : data.methods)
        member_ids.insert(method->id);

    std::unordered_set<CodeSemanticNodeId> referenced_seen;
    std::unordered_map<CodeSemanticNodeId, CodeSemanticNodeId> inherit_target;
    for (const CodeSemanticEdge& edge : semantics.edges)
    {
        if (edge.kind == CodeSemanticEdgeKind::Inherits)
        {
            inherit_target.emplace(edge.source_id, edge.target_id);
        }
        else if (edge.kind == CodeSemanticEdgeKind::ReferencesType && member_ids.contains(edge.source_id))
        {
            if (edge.target_id == type.id || !referenced_seen.insert(edge.target_id).second)
                continue;
            if (const CodeSemanticNode* target = find_node(semantics, edge.target_id))
                data.referenced.push_back(target);
        }
    }

    std::unordered_set<CodeSemanticNodeId> ancestor_seen;
    CodeSemanticNodeId cursor = type.id;
    for (int depth = 0; depth < 8; ++depth)
    {
        const auto it = inherit_target.find(cursor);
        if (it == inherit_target.end() || !ancestor_seen.insert(it->second).second)
            break;
        const CodeSemanticNode* ancestor = find_node(semantics, it->second);
        if (!ancestor)
            break;
        data.ancestors.push_back(ancestor);
        cursor = it->second;
    }
    return data;
}

bool is_constructor_like(const CodeSemanticNode& method, const CodeSemanticNode& type)
{
    return method.name == type.name || (!method.name.empty() && method.name.front() == '~');
}

float cell_health(const SubjectData& subject)
{
    float avg_method_lines = 0.0f;
    for (const CodeSemanticNode* method : subject.methods)
        avg_method_lines += static_cast<float>(method->metrics.line_count);
    if (!subject.methods.empty())
        avg_method_lines /= static_cast<float>(subject.methods.size());
    const float members = static_cast<float>(subject.methods.size() + subject.fields.size());
    const float penalty = 0.45f * sat(avg_method_lines / 45.0f)
        + 0.30f * sat(static_cast<float>(subject.referenced.size()) / kCouplingReference)
        + 0.25f * sat(members / kMemberReference);
    return 1.0f - sat(penalty);
}

// Cheap health proxy for a simple cell from the Type node metrics alone.
float type_health(const CodeSemanticNode& type)
{
    const float penalty = 0.45f * sat(static_cast<float>(type.metrics.line_count) / 260.0f)
        + 0.30f * sat(static_cast<float>(type.metrics.referenced_type_count) / kCouplingReference)
        + 0.25f * sat(static_cast<float>(type.metrics.method_count + type.metrics.field_count) / kMemberReference);
    return 1.0f - sat(penalty);
}

void build_full_cell(CodeVizSceneWorld& world, const CellFrame& frame, const SubjectData& subject,
    BiologyBuildResult& result)
{
    const CodeSemanticNode& type = *subject.type;
    const uint64_t seed = stable_color_hash(std::string(display_name(type)));
    const CodeVizSemanticRef type_ref = ref_for(type);
    const size_t member_count = subject.methods.size() + subject.fields.size();
    const float health = cell_health(subject);
    const float coupling = sat(static_cast<float>(subject.referenced.size()) / kCouplingReference);

    add_membrane(world, frame, seed, health, coupling, type.is_abstract, type_ref);
    add_nucleus(world, frame, seed, type.is_abstract, subject.ancestors.size(), type_ref);

    // DNA rungs: all members in declaration order, 4-color coded.
    struct Member
    {
        uint32_t start_line;
        int category;
    };
    std::vector<Member> members;
    members.reserve(member_count);
    for (const CodeSemanticNode* field : subject.fields)
        members.push_back({ field->source.start_line, 0 });
    for (const CodeSemanticNode* method : subject.methods)
    {
        int category = method->metrics.referenced_type_count > 0 ? 2 : 1;
        if (is_constructor_like(*method, type) || method->is_abstract)
            category = 3;
        members.push_back({ method->source.start_line, category });
    }
    std::sort(members.begin(), members.end(), [](const Member& a, const Member& b) { return a.start_line < b.start_line; });
    if (members.size() > kMaxDnaRungs)
        members.resize(kMaxDnaRungs);
    std::vector<int> rung_categories;
    rung_categories.reserve(members.size());
    for (const Member& member : members)
        rung_categories.push_back(member.category);
    add_dna(world, frame, seed, rung_categories, health, type_ref);

    // Mitochondria (methods, top-N by line count) + lysosomes for smells.
    std::vector<const CodeSemanticNode*> methods_by_size = subject.methods;
    std::sort(methods_by_size.begin(), methods_by_size.end(),
        [](const CodeSemanticNode* a, const CodeSemanticNode* b) { return a->metrics.line_count > b->metrics.line_count; });
    const size_t shown_methods = std::min(methods_by_size.size(), kMaxMitochondria);
    size_t lysosomes = 0;
    const auto tiny_sphere = std::make_shared<const GeometryMesh>(build_unit_uv_sphere_geometry(8, 12));
    const auto lyso_sphere = std::make_shared<const GeometryMesh>(build_unit_uv_sphere_geometry(10, 16));
    for (size_t i = 0; i < shown_methods; ++i)
    {
        const CodeSemanticNode& method = *methods_by_size[i];
        const uint64_t method_seed = stable_color_hash(std::string(display_name(method)));
        const glm::vec3 pos = sample_at(method_seed, frame, 0.78f, 1.25f);
        add_mitochondrion(world, frame, method_seed, pos,
            method.metrics.line_count, method.metrics.referenced_type_count, ref_for(method));
        ++result.stats.mitochondria_count;
        if (method.metrics.line_count > static_cast<uint32_t>(kMethodLineSmell) && lysosomes < kMaxLysosomes)
        {
            place_sphere(world, frame, pos + glm::vec3(0.0f, method_seed % 3 == 0 ? 0.7f : -0.7f, 0.0f),
                glm::vec3(0.34f), glm::vec4(kLysosome, 1.0f), lyso_sphere, false, ref_for(method));
            ++lysosomes;
        }
    }

    // Ribosomes (fields) on the nuclear envelope.
    const size_t shown_fields = std::min(subject.fields.size(), kMaxRibosomes);
    for (size_t i = 0; i < shown_fields; ++i)
    {
        const CodeSemanticNode& field = *subject.fields[i];
        const uint64_t field_seed = stable_color_hash(std::string(display_name(field)));
        std::mt19937_64 rng(field_seed);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        glm::vec3 dir(dist(rng), dist(rng), dist(rng));
        if (glm::dot(dir, dir) < 1e-4f)
            dir = glm::vec3(0.0f, 1.0f, 0.0f);
        dir = glm::normalize(dir);
        const glm::vec3 pos = frame.nucleus_center + dir * frame.nucleus_radii * 1.04f;
        const glm::vec3 color = field.metrics.referenced_type_count > 0 ? kRibosomeLinked : kRibosome;
        place_sphere(world, frame, pos, glm::vec3(0.14f), glm::vec4(color, 1.0f), tiny_sphere, false, ref_for(field));
        ++result.stats.ribosome_count;
    }

    add_endoplasmic_reticulum(world, frame, seed, std::sqrt(static_cast<float>(subject.fields.size()) + 1.0f) * 0.9f);

    if (!subject.ancestors.empty())
        add_golgi(world, frame, seed, static_cast<int>(subject.ancestors.size()), ref_for(*subject.ancestors.front()));

    const size_t shown_refs = std::min(subject.referenced.size(), kMaxVesicles);
    for (size_t i = 0; i < shown_refs; ++i)
    {
        const CodeSemanticNode& target = *subject.referenced[i];
        const uint64_t vseed = stable_color_hash(std::string(display_name(target))) ^ 0x7e51u;
        const glm::vec3 pos = sample_at(vseed, frame, 0.9f, 1.08f);
        place_sphere(world, frame, pos, glm::vec3(0.24f + (vseed % 5) * 0.02f), kVesicle, tiny_sphere, false, ref_for(target));
    }

    const CodeSemanticNode* organizer = nullptr;
    for (const CodeSemanticNode* method : subject.methods)
    {
        if (is_constructor_like(*method, type))
        {
            organizer = method;
            break;
        }
        if (!organizer || method->metrics.referenced_type_count > organizer->metrics.referenced_type_count)
            organizer = method;
    }
    add_centrosome(world, frame, organizer ? ref_for(*organizer) : type_ref);

    const size_t strand_count = std::min<size_t>(5, shown_methods);
    for (size_t i = 0; i < strand_count; ++i)
    {
        const CodeSemanticNode& method = *methods_by_size[i];
        const uint64_t mseed = stable_color_hash(std::string(display_name(method)));
        const glm::vec3 target = sample_at(mseed, frame, 0.78f, 1.25f);
        const glm::vec3 start = frame.nucleus_center + glm::vec3(0.0f, 0.4f, 0.0f);
        const glm::vec3 mid = glm::mix(start, target, 0.5f) + glm::vec3(0.0f, 0.6f, 0.0f);
        GeometryMesh mesh = build_tube({ start, mid, target }, 0.05f, 6,
            [&](float t) { return kMrna * (0.8f + 0.2f * std::sin(t * 8.0f)); });
        place_baked(world, frame, mesh, 1.5f, glm::vec4(1.0f), ref_for(method));
    }
}

// A simple, undifferentiated cell: membrane + small nucleus, shared meshes,
// tinted by module identity and health. Cheap enough for hundreds.
void build_simple_cell(CodeVizSceneWorld& world, const CellFrame& frame, const CodeSemanticNode& type,
    const glm::vec3& module_hue, const std::shared_ptr<const GeometryMesh>& membrane_mesh,
    const std::shared_ptr<const GeometryMesh>& nucleus_mesh)
{
    const float health = type_health(type);
    const glm::vec3 hue = glm::mix(module_hue, kMembraneSick, (1.0f - health) * 0.55f);
    const CodeVizSemanticRef ref = ref_for(type);
    place_sphere(world, frame, glm::vec3(0.0f), glm::vec3(frame.cx, frame.cy, frame.cz),
        glm::vec4(hue, type.is_abstract ? 0.55f : 0.92f), membrane_mesh, type.is_abstract, ref);
    place_sphere(world, frame, frame.nucleus_center * 0.4f, glm::vec3(frame.cx * 0.34f),
        glm::vec4(glm::mix(kNucleusBottom, hue, 0.4f), 1.0f), nucleus_mesh, false, ref);
}

// --- Tissue layout -----------------------------------------------------------

struct CellEntry
{
    const CodeSemanticNode* type = nullptr;
    long sig = 0;
    float scale = 0.25f;
    bool full = false;
    CellFrame frame;
};

struct ModuleTissue
{
    std::string module_path;
    std::vector<CellEntry> cells;
    glm::vec2 center{ 0.0f };
    float radius = 4.0f;
};

// Phyllotaxis (sunflower) placement: significant cells cluster toward the center.
void place_cells_in_disc(ModuleTissue& tissue)
{
    const float golden = 2.399963f;
    const size_t n = tissue.cells.size();
    for (size_t i = 0; i < n; ++i)
    {
        const float r = tissue.radius * std::sqrt((static_cast<float>(i) + 0.5f) / static_cast<float>(n));
        const float theta = static_cast<float>(i) * golden;
        tissue.cells[i].frame.center = glm::vec3(
            tissue.center.x + r * std::cos(theta),
            0.35f + tissue.cells[i].scale * tissue.cells[i].frame.cy,
            tissue.center.y + r * std::sin(theta));
    }
}

void build_generic_cell(CodeVizSceneWorld& world, BiologyBuildResult& result);

} // namespace

BiologyBuildResult build_biology_view(
    CodeVizSceneWorld& world,
    const CodeSemanticSnapshot& semantics,
    const MegaCityCodeConfig& /*config*/)
{
    PERF_MEASURE();
    BiologyBuildResult result;
    world.clear();

    // Collect all types and their significance.
    std::vector<const CodeSemanticNode*> types;
    for (const CodeSemanticNode& node : semantics.nodes)
        if (node.kind == CodeSemanticNodeKind::Type)
            types.push_back(&node);

    if (types.empty())
    {
        build_generic_cell(world, result);
        return result;
    }

    // Cap total cells, keeping the most significant.
    std::sort(types.begin(), types.end(),
        [](const CodeSemanticNode* a, const CodeSemanticNode* b) {
            const long sa = significance(*a);
            const long sb = significance(*b);
            if (sa != sb)
                return sa > sb;
            if (a->metrics.line_count != b->metrics.line_count)
                return a->metrics.line_count > b->metrics.line_count;
            return a->qualified_name < b->qualified_name;
        });
    const size_t dropped = types.size() > kMaxCells ? types.size() - kMaxCells : 0;
    if (types.size() > kMaxCells)
        types.resize(kMaxCells);

    const long max_sig = std::max<long>(1, significance(*types.front()));
    std::unordered_set<CodeSemanticNodeId> full_ids;
    for (size_t i = 0; i < types.size() && i < kMaxFullCells; ++i)
        full_ids.insert(types[i]->id);
    result.subject_label = std::string(display_name(*types.front()));
    result.mapped_from_semantics = true;

    // Group cells by module tissue.
    std::map<std::string, ModuleTissue> tissues;
    for (const CodeSemanticNode* type : types)
    {
        ModuleTissue& tissue = tissues[type->module_path];
        tissue.module_path = type->module_path;
        CellEntry entry;
        entry.type = type;
        entry.sig = significance(*type);
        entry.full = full_ids.contains(type->id);
        const float norm = std::sqrt(static_cast<float>(entry.sig) / static_cast<float>(max_sig));
        entry.scale = entry.full ? glm::clamp(0.45f + 0.45f * norm, 0.45f, 0.9f)
                                 : glm::clamp(0.16f + 0.32f * norm, 0.16f, 0.5f);
        tissue.cells.push_back(entry);
    }

    // Order + size each module tissue.
    std::map<std::string, size_t> module_index;
    std::vector<ModuleTissue*> ordered;
    for (auto& [path, tissue] : tissues)
    {
        std::sort(tissue.cells.begin(), tissue.cells.end(),
            [](const CellEntry& a, const CellEntry& b) {
                if (a.type->source.file_path != b.type->source.file_path)
                    return a.type->source.file_path < b.type->source.file_path;
                return a.sig > b.sig;
            });
        float area = 0.0f;
        for (const CellEntry& cell : tissue.cells)
            area += cell.scale * cell.frame.cx * cell.scale * cell.frame.cx;
        tissue.radius = std::max(6.0f, std::sqrt(area / 0.55f) + tissue.cells.front().scale * tissue.cells.front().frame.cx);
        module_index[path] = ordered.size();
        ordered.push_back(&tissue);
    }

    // Shelf-pack module tissues on the floor plane.
    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(ordered.size())))));
    constexpr float gap = 10.0f;
    float x_cursor = 0.0f;
    float z_cursor = 0.0f;
    float row_max = 0.0f;
    int col = 0;
    for (ModuleTissue* tissue : ordered)
    {
        tissue->center = glm::vec2(x_cursor + tissue->radius, z_cursor + tissue->radius);
        x_cursor += 2.0f * tissue->radius + gap;
        row_max = std::max(row_max, 2.0f * tissue->radius);
        if (++col >= cols)
        {
            z_cursor += row_max + gap;
            x_cursor = 0.0f;
            row_max = 0.0f;
            col = 0;
        }
    }

    // Recenter the organism at the origin and finalize per-cell frames.
    float min_x = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();
    for (ModuleTissue* tissue : ordered)
    {
        min_x = std::min(min_x, tissue->center.x - tissue->radius);
        max_x = std::max(max_x, tissue->center.x + tissue->radius);
        min_z = std::min(min_z, tissue->center.y - tissue->radius);
        max_z = std::max(max_z, tissue->center.y + tissue->radius);
    }
    const glm::vec2 shift(-(min_x + max_x) * 0.5f, -(min_z + max_z) * 0.5f);
    for (ModuleTissue* tissue : ordered)
    {
        tissue->center += shift;
        place_cells_in_disc(*tissue);
    }

    // Shared meshes for simple cells + tissue patches.
    BlobParams simple_blob;
    simple_blob.seed = 0x51E9Fu;
    simple_blob.radius = glm::vec3(0.5f);
    simple_blob.latitude_segments = 28;
    simple_blob.longitude_segments = 48;
    simple_blob.noise_amplitude = 0.12f;
    simple_blob.color_top = glm::vec3(1.0f);
    simple_blob.color_bottom = glm::vec3(0.82f);
    const auto simple_membrane = std::make_shared<const GeometryMesh>(build_blob_mesh(simple_blob));
    const auto simple_nucleus = std::make_shared<const GeometryMesh>(build_unit_uv_sphere_geometry(10, 16));

    // Emit tissue patches (soft module-colored territory under the cells).
    for (ModuleTissue* tissue : ordered)
    {
        const glm::vec4 hue = stable_pastel_color(tissue->module_path, 1.0f);
        world.create_ellipsoid(
            tissue->center.x, tissue->center.y, -0.05f,
            EllipsoidMetrics{ tissue->radius, 0.22f, tissue->radius },
            glm::vec4(glm::vec3(hue) * 0.85f, 0.5f),
            CodeVizSemanticRef{ {}, tissue->module_path, tissue->module_path, 0, 0 },
            simple_membrane, false);
        ++result.stats.module_tissue_count;
    }

    // Emit cells.
    for (ModuleTissue* tissue : ordered)
    {
        const glm::vec3 module_hue = glm::vec3(stable_pastel_color(tissue->module_path, 1.0f));
        for (CellEntry& cell : tissue->cells)
        {
            cell.frame.scale = cell.scale;
            if (cell.full)
            {
                const SubjectData subject = gather_subject(semantics, *cell.type);
                build_full_cell(world, cell.frame, subject, result);
                ++result.stats.full_cell_count;
            }
            else
            {
                build_simple_cell(world, cell.frame, *cell.type, module_hue, simple_membrane, simple_nucleus);
            }
            ++result.stats.cell_count;
        }
    }

    // Blood vessels: strong inter-module dependency coupling.
    std::map<std::pair<size_t, size_t>, int> module_edges;
    for (const CodeSemanticEdge& edge : semantics.edges)
    {
        if (edge.kind != CodeSemanticEdgeKind::ReferencesType && edge.kind != CodeSemanticEdgeKind::Inherits)
            continue;
        const CodeSemanticNode* src = find_node(semantics, edge.source_id);
        const CodeSemanticNode* dst = find_node(semantics, edge.target_id);
        if (!src || !dst || src->module_path == dst->module_path)
            continue;
        const auto a = module_index.find(src->module_path);
        const auto b = module_index.find(dst->module_path);
        if (a == module_index.end() || b == module_index.end())
            continue;
        auto key = std::minmax(a->second, b->second);
        ++module_edges[{ key.first, key.second }];
    }
    std::vector<std::pair<std::pair<size_t, size_t>, int>> vessels(module_edges.begin(), module_edges.end());
    std::sort(vessels.begin(), vessels.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& [pair, count] : vessels)
    {
        if (result.stats.vessel_count >= kMaxVessels || count < kMinVesselEdges)
            break;
        const glm::vec2 pa = ordered[pair.first]->center;
        const glm::vec2 pb = ordered[pair.second]->center;
        const glm::vec3 a3(pa.x, 0.12f, pa.y);
        const glm::vec3 b3(pb.x, 0.12f, pb.y);
        const glm::vec3 mid = glm::mix(a3, b3, 0.5f) + glm::vec3(0.0f, 0.4f + 0.05f * static_cast<float>(count), 0.0f);
        const float thickness = glm::clamp(0.12f + 0.03f * std::sqrt(static_cast<float>(count)), 0.12f, 0.45f);
        GeometryMesh vessel = build_tube({ a3, mid, b3 }, thickness, 8,
            [&](float t) { return kVessel * (0.7f + 0.4f * std::sin(t * 3.14159f)); });
        world.create_block(
            0.0f, 0.0f, 0.0f, BlockMetrics{ thickness, thickness, 0.0f, 0.0f }, glm::vec4(1.0f), {},
            CodeVizMaterialPreset::FlatColor,
            std::make_shared<const GeometryMesh>(std::move(vessel)), 0.0f, CustomMeshTransformMode::Baked);
        ++result.stats.vessel_count;
    }

    // Bounds + framing light for the whole organism.
    result.bounds_valid = true;
    result.min_x = min_x + shift.x - 4.0f;
    result.max_x = max_x + shift.x + 4.0f;
    result.min_z = min_z + shift.y - 4.0f;
    result.max_z = max_z + shift.y + 4.0f;
    const float span = std::max(result.max_x - result.min_x, result.max_z - result.min_z);
    result.computed_default_light = true;
    result.default_light_x = result.min_x;
    result.default_light_y = std::max(20.0f, span * 0.45f);
    result.default_light_z = result.min_z;
    result.default_light_radius = std::max(40.0f, span * 1.1f);

    DRAXUL_LOG_INFO(LogCategory::App,
        "BioView tissue: %zu cells (%zu full) across %zu module tissues, %zu vessels; hero '%s'; dropped %zu; "
        "%zu mitochondria, %zu ribosomes; span %.0f",
        result.stats.cell_count, result.stats.full_cell_count, result.stats.module_tissue_count,
        result.stats.vessel_count, result.subject_label.c_str(), dropped,
        result.stats.mitochondria_count, result.stats.ribosome_count, span);

    return result;
}

namespace
{

void build_generic_cell(CodeVizSceneWorld& world, BiologyBuildResult& result)
{
    CellFrame frame;
    frame.center = glm::vec3(0.0f, frame.cy + 1.8f, 0.0f);
    const uint64_t seed = 0xC0FFEEu;
    add_membrane(world, frame, seed, 0.85f, 0.2f, false, {});
    add_nucleus(world, frame, seed, false, 1, {});

    std::vector<int> rungs(24);
    for (size_t i = 0; i < rungs.size(); ++i)
        rungs[i] = static_cast<int>(i % 4);
    add_dna(world, frame, seed, rungs, 0.85f, {});

    const auto tiny_sphere = std::make_shared<const GeometryMesh>(build_unit_uv_sphere_geometry(8, 12));
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<float> lines(12.0f, 46.0f);
    for (int i = 0; i < 6; ++i)
    {
        const uint64_t s = seed + static_cast<uint64_t>(i) * 101u;
        const glm::vec3 pos = sample_at(s, frame, 0.78f, 1.25f);
        add_mitochondrion(world, frame, s, pos, static_cast<uint32_t>(lines(rng)), 5u, {});
        ++result.stats.mitochondria_count;
    }
    for (int i = 0; i < 40; ++i)
    {
        const glm::vec3 pos = sample_at(seed + static_cast<uint64_t>(i) * 17u + 3u, frame, 0.9f, 1.05f);
        place_sphere(world, frame, pos, glm::vec3(0.11f), glm::vec4(kRibosome, 1.0f), tiny_sphere, false, {});
        ++result.stats.ribosome_count;
    }
    add_endoplasmic_reticulum(world, frame, seed, 2.2f);
    add_golgi(world, frame, seed, 5, {});
    add_centrosome(world, frame, {});

    result.stats.cell_count = 1;
    result.bounds_valid = true;
    result.min_x = -frame.cx * 1.18f;
    result.max_x = frame.cx * 1.18f;
    result.min_z = -frame.cz * 1.18f;
    result.max_z = frame.cz * 1.18f;
    result.computed_default_light = true;
    result.default_light_x = frame.cx * 0.9f;
    result.default_light_y = frame.center.y + frame.cy * 2.6f;
    result.default_light_z = frame.cz * 1.2f;
    result.default_light_radius = std::max(frame.cx, frame.cz) * 4.5f;
    DRAXUL_LOG_INFO(LogCategory::App, "BioView: no types in snapshot; grew a generic cell");
}

} // namespace

} // namespace draxul
