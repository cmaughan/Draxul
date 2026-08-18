#pragma once

// Grid vertex push-constant ABI contract — C++ (writer) side.
//
// This is the backend-private C++ mirror of the CPU->GPU grid push-constant
// block. The DECLARATIVE single source of truth is
// shaders/contracts/grid_push_constants.toml; this header, shaders/grid.metal
// and shaders/grid_bg.vert / grid_fg.vert / grid_fg.frag must all agree with it.
//
// tests/shader_abi_parity_tests.cpp mechanically cross-checks every consumer
// against that manifest. A one-sided edit here breaks the build via the
// static_asserts below (layout) or the [shader_abi] integrity test (binding
// constants); a one-sided edit in the MSL/GLSL breaks the integrity test.
//
// Kept free of Metal/Vulkan/Objective-C headers so BOTH renderer backends and
// the plain-C++ unit tests can include it. Do NOT promote to a public
// include/draxul/ header — the card requires this contract stay backend-private
// (kanban/ice-box/19 shader-abi-parity -test.md).

#include <cstddef>
#include <cstdint>

namespace draxul::grid_contract
{

// Per-draw uniform consumed by the grid_bg / grid_fg vertex shaders. Written
// CPU-side once per pane and uploaded as Metal setVertexBytes(index = 1) or
// Vulkan vkCmdPushConstants(offset = 0). Field ORDER and OFFSETS are
// load-bearing: the shaders read this tightly packed 28-byte block positionally.
struct GridPushConstants
{
    float screen_w;
    float screen_h;
    float cell_w;
    float cell_h;
    float scroll_offset_px;
    float viewport_x;
    float viewport_y;
};

// ---- Layout guards: compiler-computed layout vs. the manifest ---------------
// These compare the compiler's actual struct layout against the byte values in
// grid_push_constants.toml. A one-sided field reorder, type change or insertion
// in the struct above trips one of these and fails the renderer build.
static_assert(sizeof(GridPushConstants) == 28,
    "GridPushConstants size drifted from shaders/contracts/grid_push_constants.toml");
static_assert(alignof(GridPushConstants) == 4,
    "GridPushConstants alignment drifted from shaders/contracts/grid_push_constants.toml");
static_assert(offsetof(GridPushConstants, screen_w) == 0, "grid contract: screen_w offset");
static_assert(offsetof(GridPushConstants, screen_h) == 4, "grid contract: screen_h offset");
static_assert(offsetof(GridPushConstants, cell_w) == 8, "grid contract: cell_w offset");
static_assert(offsetof(GridPushConstants, cell_h) == 12, "grid contract: cell_h offset");
static_assert(offsetof(GridPushConstants, scroll_offset_px) == 16, "grid contract: scroll_offset_px offset");
static_assert(offsetof(GridPushConstants, viewport_x) == 20, "grid contract: viewport_x offset");
static_assert(offsetof(GridPushConstants, viewport_y) == 24, "grid contract: viewport_y offset");

// ---- Resource binding / slot constants --------------------------------------
// The renderer backends use these named constants instead of bare literals so an
// index change is a single-point edit that the parity test cross-checks against
// the manifest (and hence against the shader attributes).

// Metal argument-table indices (see shaders/grid.metal).
inline constexpr std::uint32_t kMetalCellBufferIndex = 0; // [[buffer(0)]]
inline constexpr std::uint32_t kMetalPushConstantIndex = 1; // [[buffer(1)]]
inline constexpr std::uint32_t kMetalAtlasTextureIndex = 0; // [[texture(0)]]
inline constexpr std::uint32_t kMetalAtlasSamplerIndex = 0; // [[sampler(0)]]

// Vulkan/GLSL descriptor indices (see shaders/grid_bg.vert, grid_fg.frag).
// CI-only backend on this platform; kept here so the future SPIR-V-reflection
// check has a single C++ definition to compare against as well.
inline constexpr std::uint32_t kVulkanDescriptorSet = 0;
inline constexpr std::uint32_t kVulkanCellBufferBinding = 0; // set = 0, binding = 0
inline constexpr std::uint32_t kVulkanAtlasSamplerBinding = 1; // set = 0, binding = 1
inline constexpr std::uint32_t kVulkanPushConstantOffset = 0;

} // namespace draxul::grid_contract
