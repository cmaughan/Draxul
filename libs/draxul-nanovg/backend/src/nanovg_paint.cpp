#include "nanovg_paint.h"

#include "nanovg.h"

#include <cmath>
#include <cstring>

namespace draxul::nanovg_detail
{
namespace
{

void transform_to_padded_mat3(float* output, const float* transform)
{
    output[0] = transform[0];
    output[1] = transform[1];
    output[2] = 0.0f;
    output[3] = 0.0f;
    output[4] = transform[2];
    output[5] = transform[3];
    output[6] = 0.0f;
    output[7] = 0.0f;
    output[8] = transform[4];
    output[9] = transform[5];
    output[10] = 1.0f;
    output[11] = 0.0f;
}

} // namespace

PaintConversionResult convert_paint(PaintUniforms& output,
    const NVGpaint& paint, const NVGscissor& scissor, float width,
    float fringe, float stroke_threshold,
    std::optional<TextureMetadata> texture)
{
    std::memset(&output, 0, sizeof(output));

    output.innerCol[0] = paint.innerColor.r * paint.innerColor.a;
    output.innerCol[1] = paint.innerColor.g * paint.innerColor.a;
    output.innerCol[2] = paint.innerColor.b * paint.innerColor.a;
    output.innerCol[3] = paint.innerColor.a;
    output.outerCol[0] = paint.outerColor.r * paint.outerColor.a;
    output.outerCol[1] = paint.outerColor.g * paint.outerColor.a;
    output.outerCol[2] = paint.outerColor.b * paint.outerColor.a;
    output.outerCol[3] = paint.outerColor.a;

    if (scissor.extent[0] < -0.5f || scissor.extent[1] < -0.5f)
    {
        std::memset(output.scissorMat, 0, sizeof(output.scissorMat));
        output.scissorExt[0] = 1.0f;
        output.scissorExt[1] = 1.0f;
        output.scissorScale[0] = 1.0f;
        output.scissorScale[1] = 1.0f;
    }
    else
    {
        float inverse[6];
        nvgTransformInverse(inverse, scissor.xform);
        transform_to_padded_mat3(output.scissorMat, inverse);
        output.scissorExt[0] = scissor.extent[0];
        output.scissorExt[1] = scissor.extent[1];
        output.scissorScale[0]
            = std::sqrt(scissor.xform[0] * scissor.xform[0]
                  + scissor.xform[2] * scissor.xform[2])
            / fringe;
        output.scissorScale[1]
            = std::sqrt(scissor.xform[1] * scissor.xform[1]
                  + scissor.xform[3] * scissor.xform[3])
            / fringe;
    }

    output.extent[0] = paint.extent[0];
    output.extent[1] = paint.extent[1];
    output.strokeMult = (width * 0.5f + fringe * 0.5f) / fringe;
    output.strokeThr = stroke_threshold;

    if (paint.image != 0)
    {
        if (!texture)
            return PaintConversionResult::MissingTextureMetadata;

        if ((texture->flags & NVG_IMAGE_FLIPY) != 0)
        {
            float first[6], second[6];
            nvgTransformTranslate(first, 0.0f, output.extent[1] * 0.5f);
            nvgTransformMultiply(first, paint.xform);
            nvgTransformScale(second, 1.0f, -1.0f);
            nvgTransformMultiply(second, first);
            nvgTransformTranslate(first, 0.0f, -output.extent[1] * 0.5f);
            nvgTransformMultiply(first, second);
            float inverse[6];
            nvgTransformInverse(inverse, first);
            transform_to_padded_mat3(output.paintMat, inverse);
        }
        else
        {
            float inverse[6];
            nvgTransformInverse(inverse, paint.xform);
            transform_to_padded_mat3(output.paintMat, inverse);
        }
        output.type = static_cast<int>(PaintShaderType::FillImage);
        if (texture->type == NVG_TEXTURE_RGBA)
            output.texType
                = (texture->flags & NVG_IMAGE_PREMULTIPLIED) ? 0 : 1;
        else
            output.texType = 2;
    }
    else
    {
        output.type = static_cast<int>(PaintShaderType::FillGradient);
        output.radius = paint.radius;
        output.feather = paint.feather;
        float inverse[6];
        nvgTransformInverse(inverse, paint.xform);
        transform_to_padded_mat3(output.paintMat, inverse);
    }

    return PaintConversionResult::Complete;
}

} // namespace draxul::nanovg_detail
