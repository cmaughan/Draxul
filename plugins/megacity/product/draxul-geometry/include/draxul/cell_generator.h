#pragma once

// Procedural biological geometry toolkit.
//
// A small library of organic mesh generators used by the BioView host to
// assemble a eukaryotic cell entirely from procedural geometry: a soft,
// blobby membrane; a DNA double helix; mitochondria with cristae; a Golgi
// stack; a folded endoplasmic reticulum; and swept tubes for RNA strands.
//
// Everything here produces plain GeometryMesh data (position/normal/color).
// The BioView builder places these meshes into the shared CodeViz scene, so
// the generators know nothing about the renderer, lighting, or the semantic
// model — they are pure geometry. Colors are baked per-vertex so the flat-color
// PBR material can shade them with soft gradients.

#include <draxul/geometry_mesh.h>

#include <cstdint>
#include <functional>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <vector>

namespace draxul
{

// --- Generic mesh operations -------------------------------------------------

// Append src into dst, offsetting src's indices. Colors/normals are copied.
void append_mesh(GeometryMesh& dst, const GeometryMesh& src);

// Return a copy of src with every position transformed by `transform` and every
// normal transformed by the inverse-transpose (so non-uniform scale stays
// correct). Per-vertex colors are preserved.
[[nodiscard]] GeometryMesh transform_mesh(const GeometryMesh& src, const glm::mat4& transform);

// Overwrite every vertex color.
void set_mesh_color(GeometryMesh& mesh, const glm::vec3& color);

// Recompute smooth (area-weighted) vertex normals from the triangle topology.
void recompute_smooth_normals(GeometryMesh& mesh);

// Smooth 3D value noise in [-1, 1] and fractal Brownian motion built from it.
// Deterministic given the seed; used to make surfaces look soft and living.
[[nodiscard]] float value_noise_3d(const glm::vec3& p, uint32_t seed);
[[nodiscard]] float fbm_noise_3d(const glm::vec3& p, int octaves, uint32_t seed);

// --- Blobby membranes / vesicles --------------------------------------------

struct BlobParams
{
    uint64_t seed = 1;
    // Base radius before noise. Keep at 0.5 to feed create_ellipsoid (which
    // scales a unit-diameter mesh by its metrics); use real world radii to bake
    // a finished blob for create_block.
    glm::vec3 radius{ 0.5f };
    int latitude_segments = 40;
    int longitude_segments = 72;
    float noise_amplitude = 0.14f; // fraction of radius the surface undulates
    float noise_frequency = 1.7f;
    int noise_octaves = 4;
    // Vertical color gradient (top -> bottom of the blob) plus a faint noise
    // tint so the surface reads as a living, mottled membrane.
    glm::vec3 color_top{ 0.45f, 0.85f, 0.95f };
    glm::vec3 color_bottom{ 0.16f, 0.45f, 0.78f };
    float color_noise = 0.06f;
};

// A soft, organic closed surface: a sphere pushed around by layered noise.
// Centered at the origin, correct smooth normals, per-vertex color baked in.
[[nodiscard]] GeometryMesh build_blob_mesh(const BlobParams& params);

// --- Swept tubes -------------------------------------------------------------

// Sweep a circular cross-section along a poly-line path using parallel-transport
// frames (no twisting). `color_along` maps a parameter t in [0,1] to a color.
[[nodiscard]] GeometryMesh build_tube(
    const std::vector<glm::vec3>& path,
    float radius,
    int sides,
    const std::function<glm::vec3(float)>& color_along);

// --- DNA double helix --------------------------------------------------------

struct DnaHelixParams
{
    uint64_t seed = 1;
    float length = 2.6f; // extent along +Y
    float helix_radius = 0.55f;
    float turns = 2.6f;
    float strand_radius = 0.09f; // backbone tube thickness
    float rung_radius = 0.055f; // base-pair tube thickness
    int rungs = 26;
    int tube_sides = 7;
    int segments_per_turn = 22;
    glm::vec3 backbone_a_color{ 0.98f, 0.30f, 0.55f };
    glm::vec3 backbone_b_color{ 0.30f, 0.85f, 0.95f };
    // Optional per-rung category (index into the 4-color base palette). When
    // non-empty, the rung count follows this vector so each declared member of
    // a class can become one color-coded base pair, in declaration order.
    std::vector<int> rung_categories;
};

// Two intertwined sugar-phosphate backbones plus alternating four-color
// base-pair rungs. Centered at the origin, long axis along +Y.
[[nodiscard]] GeometryMesh build_dna_double_helix(const DnaHelixParams& params);

// --- Mitochondrion -----------------------------------------------------------

struct MitochondrionParams
{
    uint64_t seed = 1;
    float length = 2.1f; // extent along +X
    float radius = 0.62f;
    int cristae = 7; // internal folds
    int longitude_segments = 48;
    int latitude_segments = 22;
    glm::vec3 outer_color_a{ 0.97f, 0.55f, 0.24f };
    glm::vec3 outer_color_b{ 0.86f, 0.28f, 0.17f };
    glm::vec3 cristae_color{ 0.99f, 0.74f, 0.42f };
};

// A bean-shaped organelle: a wavy elongated capsule (outer membrane) with an
// internal folded ribbon of cristae. Centered at the origin, long axis +X.
[[nodiscard]] GeometryMesh build_mitochondrion(const MitochondrionParams& params);

// --- Golgi apparatus ---------------------------------------------------------

struct GolgiParams
{
    uint64_t seed = 1;
    int cisternae = 5; // stacked curved discs
    float radius = 1.15f;
    float thickness = 0.12f;
    float spacing = 0.26f;
    float curvature = 0.42f; // how much each disc bows
    int radial_segments = 40;
    glm::vec3 color_a{ 0.22f, 0.82f, 0.72f };
    glm::vec3 color_b{ 0.16f, 0.60f, 0.68f };
};

// A stack of curved, flattened cisternae. Centered at the origin, stacked +Y.
[[nodiscard]] GeometryMesh build_golgi(const GolgiParams& params);

// --- Endoplasmic reticulum ---------------------------------------------------

struct ErParams
{
    uint64_t seed = 1;
    float extent = 2.6f; // rough footprint radius
    float tube_radius = 0.13f;
    int strands = 3;
    int tube_sides = 7;
    glm::vec3 color{ 0.26f, 0.76f, 0.46f };
};

struct ErResult
{
    GeometryMesh mesh;
    // Suggested ribosome stud positions along the ER surface (rough ER).
    std::vector<glm::vec3> ribosome_sites;
};

// A folded network of membrane tubes. Centered at the origin in the XZ plane.
[[nodiscard]] ErResult build_endoplasmic_reticulum(const ErParams& params);

} // namespace draxul
