// Repository-integrity + Metal-side parity test for the grid vertex
// push-constant ABI contract.  See kanban/pending/19 shader-abi-parity -test.md.
//
// SINGLE SOURCE OF TRUTH: shaders/contracts/grid_push_constants.toml.
// This test re-derives the field layout and the resource binding/slot indices
// from EACH language-specific consumer and asserts they equal the manifest:
//
//   * C++   libs/draxul-renderer/src/shared/grid_contract.h
//             sizeof / alignof / offsetof + the Metal & Vulkan binding constants.
//   * MSL   shaders/grid.metal
//             struct PushConstants fields + [[buffer/texture/sampler(N)]] indices.
//   * GLSL  shaders/grid_bg.vert, grid_fg.vert   (push_constant block, SSBO binding)
//           shaders/grid_fg.frag                 (atlas sampler binding)
//
// A one-sided edit in any consumer fails here; a one-sided C++ *layout* edit also
// fails earlier, at compile time, via the static_asserts in grid_contract.h.
//
// VULKAN SCOPE (honest): the GLSL checks below are a deterministic TEXT scan,
// which is all that is possible on macOS (no glslc / SPIR-V reflection tooling
// here).  The AUTHORITATIVE Vulkan check — SPIR-V reflection of the compiled
// modules — is CI-only and intentionally NOT implemented in this file.  The
// manifest's [bindings.vulkan] table is structured so that reflection check can
// be added later against the same source.

#include <catch2/catch_all.hpp>

#include <draxul/toml_support.h>

#include "shared/grid_contract.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{

namespace gc = draxul::grid_contract;

constexpr const char* kManifestRel = "shaders/contracts/grid_push_constants.toml";

std::string read_project_file(const std::string& relative)
{
    const std::filesystem::path path = std::filesystem::path(DRAXUL_PROJECT_ROOT) / relative;
    std::ifstream in(path, std::ios::binary);
    INFO("opening " << path.string());
    REQUIRE(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>{});
}

std::string strip_line_comment(std::string line)
{
    const auto pos = line.find("//");
    if (pos != std::string::npos)
        line.erase(pos);
    return line;
}

// Returns the text between the first balanced { } that follows `intro`.
std::string extract_block_body(const std::string& text, const std::string& intro)
{
    const auto start = text.find(intro);
    INFO("locating block '" << intro << "'");
    REQUIRE(start != std::string::npos);
    const auto open = text.find('{', start);
    REQUIRE(open != std::string::npos);

    int depth = 0;
    std::size_t i = open;
    for (; i < text.size(); ++i)
    {
        if (text[i] == '{')
            ++depth;
        else if (text[i] == '}' && --depth == 0)
            break;
    }
    INFO("unbalanced braces after '" << intro << "'");
    REQUIRE(depth == 0);
    return text.substr(open + 1, i - open - 1);
}

// Parses `type name;` statements from a struct/block body, preserving order.
std::vector<std::pair<std::string, std::string>> parse_fields(const std::string& body)
{
    std::vector<std::pair<std::string, std::string>> fields;
    std::stringstream statements(body);
    std::string statement;
    while (std::getline(statements, statement, ';'))
    {
        std::istringstream tokens(strip_line_comment(statement));
        std::vector<std::string> parts;
        std::string token;
        while (tokens >> token)
            parts.push_back(token);
        if (parts.size() < 2)
            continue; // blank line or trailing whitespace after the last ';'
        fields.emplace_back(parts.front(), parts.back());
    }
    return fields;
}

// Packed byte size of a scalar shader field type, used to derive a struct's
// total size from a parsed field list (Metal/std430 tight scalar packing).
// The grid contract is all `float`; extend this map as new contract types
// appear. Unknown types return 0 so the size assertion fails loudly rather
// than silently under-counting.
std::size_t scalar_type_size(const std::string& type)
{
    if (type == "float")
        return 4;
    return 0;
}

// Extracts N from `<var> [[<kind>(N)]]` in Metal shader source.
int msl_attribute_index(const std::string& text, const std::string& var, const std::string& kind)
{
    const std::regex re("\\b" + var + "\\b\\s*\\[\\[\\s*" + kind + "\\s*\\(\\s*(\\d+)\\s*\\)\\s*\\]\\]");
    std::smatch match;
    INFO("MSL " << kind << "() index for '" << var << "'");
    REQUIRE(std::regex_search(text, match, re));
    return std::stoi(match[1].str());
}

struct SetBinding
{
    int set = -1;
    int binding = -1;
};

// Extracts set/binding from `layout(set = S, binding = B) <declTail>` in GLSL.
SetBinding glsl_set_binding(const std::string& text, const std::string& decl_tail)
{
    const std::regex re(
        "layout\\s*\\(\\s*set\\s*=\\s*(\\d+)\\s*,\\s*binding\\s*=\\s*(\\d+)\\s*\\)\\s*" + decl_tail);
    std::smatch match;
    INFO("GLSL set/binding for '" << decl_tail << "'");
    REQUIRE(std::regex_search(text, match, re));
    return SetBinding{ std::stoi(match[1].str()), std::stoi(match[2].str()) };
}

struct FieldSpec
{
    std::string name;
    std::string type;
    int offset = -1;
    int size = -1;
};

struct Manifest
{
    std::string name;
    int size = -1;
    int align = -1;
    std::vector<FieldSpec> fields;

    int metal_cell_buffer = -1;
    int metal_push_constants = -1;
    int metal_atlas_texture = -1;
    int metal_atlas_sampler = -1;

    int vk_set = -1;
    int vk_cell_binding = -1;
    int vk_sampler_binding = -1;
    int vk_push_offset = -1;
};

int as_int(const toml::node_view<const toml::node>& node)
{
    const auto value = node.value<std::int64_t>();
    INFO("manifest integer missing");
    REQUIRE(value.has_value());
    return static_cast<int>(*value);
}

Manifest load_manifest()
{
    std::string error;
    const auto table = draxul::toml_support::parse_file(
        std::filesystem::path(DRAXUL_PROJECT_ROOT) / kManifestRel, &error);
    INFO("manifest parse error: " << error);
    REQUIRE(table.has_value());

    Manifest man;
    man.name = table->at_path("contract.name").value_or(std::string{});
    man.size = as_int((*table)["struct"]["size"]);
    man.align = as_int((*table)["struct"]["align"]);

    const toml::array* fields = (*table)["field"].as_array();
    INFO("manifest [[field]] array missing");
    REQUIRE(fields != nullptr);
    for (const auto& element : *fields)
    {
        const toml::table* field = element.as_table();
        REQUIRE(field != nullptr);
        FieldSpec spec;
        spec.name = (*field)["name"].value_or(std::string{});
        spec.type = (*field)["type"].value_or(std::string{});
        spec.offset = as_int((*field)["offset"]);
        spec.size = as_int((*field)["size"]);
        man.fields.push_back(spec);
    }

    man.metal_cell_buffer = as_int((*table)["bindings"]["metal"]["cell_buffer"]);
    man.metal_push_constants = as_int((*table)["bindings"]["metal"]["push_constants"]);
    man.metal_atlas_texture = as_int((*table)["bindings"]["metal"]["atlas_texture"]);
    man.metal_atlas_sampler = as_int((*table)["bindings"]["metal"]["atlas_sampler"]);

    man.vk_set = as_int((*table)["bindings"]["vulkan"]["descriptor_set"]);
    man.vk_cell_binding = as_int((*table)["bindings"]["vulkan"]["cell_buffer_binding"]);
    man.vk_sampler_binding = as_int((*table)["bindings"]["vulkan"]["atlas_sampler_binding"]);
    man.vk_push_offset = as_int((*table)["bindings"]["vulkan"]["push_constant_offset"]);
    return man;
}

} // namespace

TEST_CASE("grid push-constant manifest is internally consistent", "[shader_abi]")
{
    const Manifest man = load_manifest();

    REQUIRE(man.name == "GridPushConstants");
    REQUIRE(man.fields.size() == 7);

    // Offsets must be dense, in order, and sum to the declared struct size.
    int expected_offset = 0;
    for (const auto& field : man.fields)
    {
        INFO("field " << field.name);
        REQUIRE(field.type == "float");
        REQUIRE(field.size == 4);
        REQUIRE(field.offset == expected_offset);
        expected_offset += field.size;
    }
    REQUIRE(expected_offset == man.size);
}

TEST_CASE("grid push-constant C++ struct matches the manifest", "[shader_abi]")
{
    const Manifest man = load_manifest();

    REQUIRE(sizeof(gc::GridPushConstants) == static_cast<std::size_t>(man.size));
    REQUIRE(alignof(gc::GridPushConstants) == static_cast<std::size_t>(man.align));

    // Per-field offsetof of the ACTUAL C++ struct vs. the manifest. Indexed by
    // declaration order so a reorder on either side is caught.
    const std::vector<std::size_t> cpp_offsets = {
        offsetof(gc::GridPushConstants, screen_w),
        offsetof(gc::GridPushConstants, screen_h),
        offsetof(gc::GridPushConstants, cell_w),
        offsetof(gc::GridPushConstants, cell_h),
        offsetof(gc::GridPushConstants, scroll_offset_px),
        offsetof(gc::GridPushConstants, viewport_x),
        offsetof(gc::GridPushConstants, viewport_y),
    };
    REQUIRE(cpp_offsets.size() == man.fields.size());
    for (std::size_t i = 0; i < man.fields.size(); ++i)
    {
        INFO("C++ offset of field " << man.fields[i].name);
        REQUIRE(cpp_offsets[i] == static_cast<std::size_t>(man.fields[i].offset));
    }

    // Binding constants used by the renderer backends vs. the manifest. A
    // one-sided change to a binding or texture slot in grid_contract.h fails here.
    REQUIRE(static_cast<int>(gc::kMetalCellBufferIndex) == man.metal_cell_buffer);
    REQUIRE(static_cast<int>(gc::kMetalPushConstantIndex) == man.metal_push_constants);
    REQUIRE(static_cast<int>(gc::kMetalAtlasTextureIndex) == man.metal_atlas_texture);
    REQUIRE(static_cast<int>(gc::kMetalAtlasSamplerIndex) == man.metal_atlas_sampler);

    REQUIRE(static_cast<int>(gc::kVulkanDescriptorSet) == man.vk_set);
    REQUIRE(static_cast<int>(gc::kVulkanCellBufferBinding) == man.vk_cell_binding);
    REQUIRE(static_cast<int>(gc::kVulkanAtlasSamplerBinding) == man.vk_sampler_binding);
    REQUIRE(static_cast<int>(gc::kVulkanPushConstantOffset) == man.vk_push_offset);
}

TEST_CASE("grid.metal MSL declarations match the manifest", "[shader_abi]")
{
    const Manifest man = load_manifest();
    const std::string metal = read_project_file("shaders/grid.metal");

    // struct PushConstants { ... } fields, in order.
    const auto fields = parse_fields(extract_block_body(metal, "struct PushConstants"));
    REQUIRE(fields.size() == man.fields.size());
    for (std::size_t i = 0; i < man.fields.size(); ++i)
    {
        INFO("MSL field " << i << " expected " << man.fields[i].name);
        REQUIRE(fields[i].first == man.fields[i].type);
        REQUIRE(fields[i].second == man.fields[i].name);
    }

    // Metal-side struct SIZE, derived from grid.metal's own field types: the
    // analogue of the C++ sizeof() check (which the compiler already pins via
    // static_assert). A widened type or a stray field in the MSL struct changes
    // this sum and fails here. Text-derived because there is no MSL reflection
    // on this platform; the compiled-metallib reflection would be the stronger
    // form, in the same category as the CI-only SPIR-V check for Vulkan.
    std::size_t msl_struct_size = 0;
    for (const auto& field : fields)
    {
        const std::size_t field_size = scalar_type_size(field.first);
        INFO("MSL field type '" << field.first << "' has no known packed size");
        REQUIRE(field_size > 0);
        msl_struct_size += field_size;
    }
    REQUIRE(msl_struct_size == static_cast<std::size_t>(man.size));

    // Resource argument-table indices.
    REQUIRE(msl_attribute_index(metal, "cells", "buffer") == man.metal_cell_buffer);
    REQUIRE(msl_attribute_index(metal, "pc", "buffer") == man.metal_push_constants);
    REQUIRE(msl_attribute_index(metal, "atlas", "texture") == man.metal_atlas_texture);
    REQUIRE(msl_attribute_index(metal, "atlas_sampler", "sampler") == man.metal_atlas_sampler);
}

// GLSL text check (Vulkan reader). Deterministic on macOS; the authoritative
// SPIR-V-reflection check remains CI-only and unimplemented here (see file header).
TEST_CASE("grid GLSL declarations match the manifest (text scan; SPIR-V reflection is CI-only)",
    "[shader_abi]")
{
    const Manifest man = load_manifest();

    for (const char* vert : { "shaders/grid_bg.vert", "shaders/grid_fg.vert" })
    {
        const std::string source = read_project_file(vert);

        const auto fields = parse_fields(
            extract_block_body(source, "push_constant) uniform PushConstants"));
        INFO("GLSL push_constant block in " << vert);
        REQUIRE(fields.size() == man.fields.size());
        for (std::size_t i = 0; i < man.fields.size(); ++i)
        {
            INFO("GLSL field " << i << " in " << vert);
            REQUIRE(fields[i].first == man.fields[i].type);
            REQUIRE(fields[i].second == man.fields[i].name);
        }

        // SSBO cell buffer binding lives in the vertex shaders.
        const SetBinding ssbo = glsl_set_binding(source, "readonly\\s+buffer\\s+CellBuffer");
        REQUIRE(ssbo.set == man.vk_set);
        REQUIRE(ssbo.binding == man.vk_cell_binding);
    }

    // Atlas sampler binding lives in the foreground fragment shader.
    const std::string frag = read_project_file("shaders/grid_fg.frag");
    const SetBinding sampler = glsl_set_binding(frag, "uniform\\s+sampler2D\\s+atlas");
    REQUIRE(sampler.set == man.vk_set);
    REQUIRE(sampler.binding == man.vk_sampler_binding);
}
