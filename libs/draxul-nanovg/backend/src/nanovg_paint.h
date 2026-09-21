#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

struct NVGpaint;
struct NVGscissor;

namespace draxul::nanovg_detail
{

// CPU-side fragment record shared by the Vulkan and Metal NanoVG backends.
// The field order and padding are the shader ABI: std140 on Vulkan and the
// matching 3 x float4 matrix representation on Metal.
struct PaintUniforms
{
    float scissorMat[12];
    float paintMat[12];
    float innerCol[4];
    float outerCol[4];
    float scissorExt[2];
    float scissorScale[2];
    float extent[2];
    float radius;
    float feather;
    float strokeMult;
    float strokeThr;
    int texType;
    int type;
    std::uint8_t padding[256 - (12 + 12 + 4 + 4 + 2 + 2 + 2 + 1 + 1 + 1 + 1 + 1 + 1) * 4];
};

static_assert(sizeof(PaintUniforms) == 256);
static_assert(offsetof(PaintUniforms, paintMat) == 48);
static_assert(offsetof(PaintUniforms, innerCol) == 96);
static_assert(offsetof(PaintUniforms, texType) == 168);
static_assert(offsetof(PaintUniforms, type) == 172);

enum class PaintShaderType : int
{
    FillGradient = 0,
    FillImage = 1,
    Simple = 2,
    Image = 3,
};

struct TextureMetadata
{
    int type = 0;
    int flags = 0;
};

enum class PaintConversionResult
{
    Complete,
    MissingTextureMetadata,
};

// Converts NanoVG's device-independent paint state into the native backends'
// shared 256-byte fragment record. A requested image with no metadata retains
// NanoVG's historical partial output: colors/scissor/stroke are written, while
// the paint matrix and image classification remain zero.
PaintConversionResult convert_paint(PaintUniforms& output,
    const NVGpaint& paint, const NVGscissor& scissor, float width,
    float fringe, float stroke_threshold,
    std::optional<TextureMetadata> texture);

} // namespace draxul::nanovg_detail
