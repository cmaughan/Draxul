#import <Metal/Metal.h>
#import <simd/simd.h>

// nanovg.h must come before our backend header so NVGparams etc. are defined.
#include "nanovg.h"
#import "nanovg_mtl_shaders.h"
#import <draxul/nanovg_mtl.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace draxul
{

// ---------------------------------------------------------------------------
// Internal types matching the NanoVG GL reference backend
// ---------------------------------------------------------------------------

enum MtlNVGshaderType
{
    MNVG_SHADER_FILLGRAD = 0,
    MNVG_SHADER_FILLIMG = 1,
    MNVG_SHADER_SIMPLE = 2,
    MNVG_SHADER_IMG = 3,
};

enum MtlNVGcallType
{
    MNVG_NONE = 0,
    MNVG_FILL,
    MNVG_CONVEXFILL,
    MNVG_STROKE,
    MNVG_TRIANGLES,
};

struct MtlNVGblend
{
    MTLBlendFactor srcRGB;
    MTLBlendFactor dstRGB;
    MTLBlendFactor srcAlpha;
    MTLBlendFactor dstAlpha;
};

struct MtlNVGcall
{
    int type = MNVG_NONE;
    int image = 0;
    int pathOffset = 0;
    int pathCount = 0;
    int triangleOffset = 0;
    int triangleCount = 0;
    int uniformOffset = 0; // byte offset into uniforms
    MtlNVGblend blendFunc{};
};

struct MtlNVGpath
{
    int fillOffset = 0;
    int fillCount = 0;
    int strokeOffset = 0;
    int strokeCount = 0;
};

// Per-draw-call uniform block (matches shader FragUniforms).
// Padded to 256-byte alignment for Metal argument buffer offsets.
struct MtlNVGfragUniforms
{
    // mat3 stored as 3 float4 columns (column-major for Metal float3x3)
    float scissorMat[12]; // 3 x float4
    float paintMat[12]; // 3 x float4
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
    // Pad to 256 bytes for Metal uniform buffer offset alignment.
    uint8_t _pad[256 - (12 + 12 + 4 + 4 + 2 + 2 + 2 + 1 + 1 + 1 + 1 + 1 + 1) * 4];
};
static_assert(sizeof(MtlNVGfragUniforms) == 256);

struct MtlNVGtexture
{
    int texId = 0;
    id<MTLTexture> tex = nil;
    int width = 0;
    int height = 0;
    int type = 0;
    int flags = 0;
};

// ---------------------------------------------------------------------------
// Metal NanoVG context — the userPtr for NVGparams
// ---------------------------------------------------------------------------
struct MtlNVGcontext
{
    // Non-owning: the device belongs to the hosting renderer (or, for plugin
    // dylibs, to the host process). Never retain host-owned Metal objects in
    // this context — the context can outlive its last frame but must not
    // extend the lifetime of objects it does not own.
    __unsafe_unretained id<MTLDevice> device = nil;
    id<MTLLibrary> library = nil;
    id<MTLRenderPipelineState> pipelineFillAA = nil;
    id<MTLRenderPipelineState> pipelineFillNoAA = nil;
    id<MTLRenderPipelineState> pipelineStencil = nil; // no color write
    id<MTLDepthStencilState> stencilFillDSS = nil; // stencil fill pass
    id<MTLDepthStencilState> stencilCoverDSS = nil; // stencil cover pass
    id<MTLDepthStencilState> stencilDefaultDSS = nil; // no stencil
    id<MTLDepthStencilState> stencilStrokeDSS = nil;
    id<MTLDepthStencilState> stencilStrokeAA_DSS = nil;
    id<MTLDepthStencilState> stencilStrokeClearDSS = nil;
    id<MTLSamplerState> sampler = nil;
    id<MTLTexture> dummyTex = nil;

    // Stencil texture — owned, recreated on size change
    id<MTLTexture> stencilTex = nil;
    int stencilW = 0;
    int stencilH = 0;

    // Per-frame state (set before nvgEndFrame). Non-owning: valid only for
    // the frame the host handed them over for.
    __unsafe_unretained id<MTLCommandBuffer> commandBuffer = nil;
    __unsafe_unretained id<MTLTexture> drawableTexture = nil;
    uint32_t frameIndex = 0;

    float viewWidth = 0;
    float viewHeight = 0;
    float devicePixelRatio = 1.0f;
    int flags = 0;

    // Deferred draw state
    std::vector<MtlNVGcall> calls;
    std::vector<MtlNVGpath> paths;
    std::vector<NVGvertex> verts;
    std::vector<uint8_t> uniforms;

    // Texture management
    std::vector<MtlNVGtexture> textures;
    int textureIdCounter = 0;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static MTLBlendFactor mtlnvg__blendFactor(int factor)
{
    switch (factor)
    {
    case NVG_ZERO:
        return MTLBlendFactorZero;
    case NVG_ONE:
        return MTLBlendFactorOne;
    case NVG_SRC_COLOR:
        return MTLBlendFactorSourceColor;
    case NVG_ONE_MINUS_SRC_COLOR:
        return MTLBlendFactorOneMinusSourceColor;
    case NVG_DST_COLOR:
        return MTLBlendFactorDestinationColor;
    case NVG_ONE_MINUS_DST_COLOR:
        return MTLBlendFactorOneMinusDestinationColor;
    case NVG_SRC_ALPHA:
        return MTLBlendFactorSourceAlpha;
    case NVG_ONE_MINUS_SRC_ALPHA:
        return MTLBlendFactorOneMinusSourceAlpha;
    case NVG_DST_ALPHA:
        return MTLBlendFactorDestinationAlpha;
    case NVG_ONE_MINUS_DST_ALPHA:
        return MTLBlendFactorOneMinusDestinationAlpha;
    case NVG_SRC_ALPHA_SATURATE:
        return MTLBlendFactorSourceAlphaSaturated;
    default:
        return MTLBlendFactorOne;
    }
}

static MtlNVGblend mtlnvg__blendCompositeOperation(NVGcompositeOperationState op)
{
    MtlNVGblend blend;
    blend.srcRGB = mtlnvg__blendFactor(op.srcRGB);
    blend.dstRGB = mtlnvg__blendFactor(op.dstRGB);
    blend.srcAlpha = mtlnvg__blendFactor(op.srcAlpha);
    blend.dstAlpha = mtlnvg__blendFactor(op.dstAlpha);
    return blend;
}

static MtlNVGtexture* mtlnvg__findTexture(MtlNVGcontext* mtl, int texId)
{
    for (auto& tex : mtl->textures)
    {
        if (tex.texId == texId)
            return &tex;
    }
    return nullptr;
}

static int mtlnvg__maxVertCount(const NVGpath* paths, int npaths)
{
    int count = 0;
    for (int i = 0; i < npaths; i++)
    {
        // Fan vertices convert to (nfill - 2) * 3 triangle verts
        count += (paths[i].nfill > 2) ? (paths[i].nfill - 2) * 3 : 0;
        count += paths[i].nstroke;
    }
    return count;
}

// Convert mat3 stored as float[12] (3 x float4, column major with padding)
static void mtlnvg__xformToMat3x4(float* m3, const float* t)
{
    m3[0] = t[0];
    m3[1] = t[1];
    m3[2] = 0.0f;
    m3[3] = 0.0f;
    m3[4] = t[2];
    m3[5] = t[3];
    m3[6] = 0.0f;
    m3[7] = 0.0f;
    m3[8] = t[4];
    m3[9] = t[5];
    m3[10] = 1.0f;
    m3[11] = 0.0f;
}

static MtlNVGfragUniforms* mtlnvg__fragUniformPtr(MtlNVGcontext* mtl, int offset)
{
    return reinterpret_cast<MtlNVGfragUniforms*>(mtl->uniforms.data() + offset);
}

static int mtlnvg__allocFragUniforms(MtlNVGcontext* mtl, int n)
{
    const int structSize = sizeof(MtlNVGfragUniforms);
    int offset = static_cast<int>(mtl->uniforms.size());
    mtl->uniforms.resize(offset + n * structSize, 0);
    return offset;
}

static void mtlnvg__convertPaint(MtlNVGcontext* mtl, MtlNVGfragUniforms* frag,
    NVGpaint* paint, NVGscissor* scissor, float width, float fringe, float strokeThr)
{
    memset(frag, 0, sizeof(*frag));

    frag->innerCol[0] = paint->innerColor.r * paint->innerColor.a;
    frag->innerCol[1] = paint->innerColor.g * paint->innerColor.a;
    frag->innerCol[2] = paint->innerColor.b * paint->innerColor.a;
    frag->innerCol[3] = paint->innerColor.a;
    frag->outerCol[0] = paint->outerColor.r * paint->outerColor.a;
    frag->outerCol[1] = paint->outerColor.g * paint->outerColor.a;
    frag->outerCol[2] = paint->outerColor.b * paint->outerColor.a;
    frag->outerCol[3] = paint->outerColor.a;

    if (scissor->extent[0] < -0.5f || scissor->extent[1] < -0.5f)
    {
        memset(frag->scissorMat, 0, sizeof(frag->scissorMat));
        frag->scissorExt[0] = 1.0f;
        frag->scissorExt[1] = 1.0f;
        frag->scissorScale[0] = 1.0f;
        frag->scissorScale[1] = 1.0f;
    }
    else
    {
        float invxform[6];
        nvgTransformInverse(invxform, scissor->xform);
        mtlnvg__xformToMat3x4(frag->scissorMat, invxform);
        frag->scissorExt[0] = scissor->extent[0];
        frag->scissorExt[1] = scissor->extent[1];
        frag->scissorScale[0] = sqrtf(scissor->xform[0] * scissor->xform[0] + scissor->xform[2] * scissor->xform[2]) / fringe;
        frag->scissorScale[1] = sqrtf(scissor->xform[1] * scissor->xform[1] + scissor->xform[3] * scissor->xform[3]) / fringe;
    }

    frag->extent[0] = paint->extent[0];
    frag->extent[1] = paint->extent[1];
    frag->strokeMult = (width * 0.5f + fringe * 0.5f) / fringe;
    frag->strokeThr = strokeThr;

    if (paint->image != 0)
    {
        MtlNVGtexture* tex = mtlnvg__findTexture(mtl, paint->image);
        if (tex == nullptr)
            return;
        if ((tex->flags & NVG_IMAGE_FLIPY) != 0)
        {
            float m1[6], m2[6];
            nvgTransformTranslate(m1, 0.0f, frag->extent[1] * 0.5f);
            nvgTransformMultiply(m1, paint->xform);
            nvgTransformScale(m2, 1.0f, -1.0f);
            nvgTransformMultiply(m2, m1);
            nvgTransformTranslate(m1, 0.0f, -frag->extent[1] * 0.5f);
            nvgTransformMultiply(m1, m2);
            float invxform[6];
            nvgTransformInverse(invxform, m1);
            mtlnvg__xformToMat3x4(frag->paintMat, invxform);
        }
        else
        {
            float invxform[6];
            nvgTransformInverse(invxform, paint->xform);
            mtlnvg__xformToMat3x4(frag->paintMat, invxform);
        }
        frag->type = MNVG_SHADER_FILLIMG;
        if (tex->type == NVG_TEXTURE_RGBA)
            frag->texType = (tex->flags & NVG_IMAGE_PREMULTIPLIED) ? 0 : 1;
        else
            frag->texType = 2; // alpha
    }
    else
    {
        frag->type = MNVG_SHADER_FILLGRAD;
        frag->radius = paint->radius;
        frag->feather = paint->feather;
        float invxform[6];
        nvgTransformInverse(invxform, paint->xform);
        mtlnvg__xformToMat3x4(frag->paintMat, invxform);
    }
}

// ---------------------------------------------------------------------------
// Stencil texture management
// ---------------------------------------------------------------------------
static void mtlnvg__ensureStencilTexture(MtlNVGcontext* mtl, int w, int h)
{
    if (mtl->stencilTex != nil && mtl->stencilW == w && mtl->stencilH == h)
        return;
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatStencil8
                                                                                    width:w
                                                                                   height:h
                                                                                mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget;
    desc.storageMode = MTLStorageModePrivate;
    mtl->stencilTex = [mtl->device newTextureWithDescriptor:desc];
    mtl->stencilW = w;
    mtl->stencilH = h;
}

// ---------------------------------------------------------------------------
// NVGparams callbacks
// ---------------------------------------------------------------------------

static int mtlnvg__renderCreate(void* uptr)
{
    MtlNVGcontext* mtl = static_cast<MtlNVGcontext*>(uptr);

    // Compile shaders from source
    NSError* error = nil;
    NSString* source = [NSString stringWithUTF8String:kNanoVGMetalShaderSource];
    mtl->library = [mtl->device newLibraryWithSource:source options:nil error:&error];
    if (!mtl->library)
    {
        NSLog(@"NanoVG Metal shader compilation failed: %@", error);
        return 0;
    }

    id<MTLFunction> vertexFunc = [mtl->library newFunctionWithName:@"nanovg_vertex"];
    id<MTLFunction> fragmentAAFunc = [mtl->library newFunctionWithName:@"nanovg_fragment_aa"];
    id<MTLFunction> fragmentFunc = [mtl->library newFunctionWithName:@"nanovg_fragment"];

    // Vertex descriptor
    MTLVertexDescriptor* vdesc = [[MTLVertexDescriptor alloc] init];
    vdesc.attributes[0].format = MTLVertexFormatFloat2;
    vdesc.attributes[0].offset = 0;
    vdesc.attributes[0].bufferIndex = 0;
    vdesc.attributes[1].format = MTLVertexFormatFloat2;
    vdesc.attributes[1].offset = sizeof(float) * 2;
    vdesc.attributes[1].bufferIndex = 0;
    vdesc.layouts[0].stride = sizeof(NVGvertex);
    vdesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    // Fill pipeline (AA) — with blending, color writes
    {
        MTLRenderPipelineDescriptor* pdesc = [[MTLRenderPipelineDescriptor alloc] init];
        pdesc.vertexFunction = vertexFunc;
        pdesc.fragmentFunction = fragmentAAFunc;
        pdesc.vertexDescriptor = vdesc;
        pdesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pdesc.colorAttachments[0].blendingEnabled = YES;
        pdesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        pdesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        pdesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
        pdesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pdesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        pdesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pdesc.stencilAttachmentPixelFormat = MTLPixelFormatStencil8;
        mtl->pipelineFillAA = [mtl->device newRenderPipelineStateWithDescriptor:pdesc error:&error];
        if (!mtl->pipelineFillAA)
        {
            NSLog(@"NanoVG: fill AA pipeline failed: %@", error);
            return 0;
        }
    }

    // Fill pipeline (no AA)
    {
        MTLRenderPipelineDescriptor* pdesc = [[MTLRenderPipelineDescriptor alloc] init];
        pdesc.vertexFunction = vertexFunc;
        pdesc.fragmentFunction = fragmentFunc;
        pdesc.vertexDescriptor = vdesc;
        pdesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pdesc.colorAttachments[0].blendingEnabled = YES;
        pdesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        pdesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        pdesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
        pdesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pdesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        pdesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        pdesc.stencilAttachmentPixelFormat = MTLPixelFormatStencil8;
        mtl->pipelineFillNoAA = [mtl->device newRenderPipelineStateWithDescriptor:pdesc error:&error];
        if (!mtl->pipelineFillNoAA)
        {
            NSLog(@"NanoVG: fill pipeline failed: %@", error);
            return 0;
        }
    }

    // Stencil-only pipeline (no color writes)
    {
        MTLRenderPipelineDescriptor* pdesc = [[MTLRenderPipelineDescriptor alloc] init];
        pdesc.vertexFunction = vertexFunc;
        pdesc.fragmentFunction = fragmentFunc;
        pdesc.vertexDescriptor = vdesc;
        pdesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        pdesc.colorAttachments[0].writeMask = MTLColorWriteMaskNone;
        pdesc.stencilAttachmentPixelFormat = MTLPixelFormatStencil8;
        mtl->pipelineStencil = [mtl->device newRenderPipelineStateWithDescriptor:pdesc error:&error];
        if (!mtl->pipelineStencil)
        {
            NSLog(@"NanoVG: stencil pipeline failed: %@", error);
            return 0;
        }
    }

    // Depth-stencil states
    {
        MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
        desc.depthCompareFunction = MTLCompareFunctionAlways;
        desc.depthWriteEnabled = NO;
        mtl->stencilDefaultDSS = [mtl->device newDepthStencilStateWithDescriptor:desc];
    }
    {
        // Stencil fill: front increments, back decrements
        MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
        desc.depthCompareFunction = MTLCompareFunctionAlways;
        desc.depthWriteEnabled = NO;

        MTLStencilDescriptor* front = [[MTLStencilDescriptor alloc] init];
        front.stencilCompareFunction = MTLCompareFunctionAlways;
        front.stencilFailureOperation = MTLStencilOperationKeep;
        front.depthFailureOperation = MTLStencilOperationKeep;
        front.depthStencilPassOperation = MTLStencilOperationIncrementWrap;
        front.readMask = 0xFF;
        front.writeMask = 0xFF;
        desc.frontFaceStencil = front;

        MTLStencilDescriptor* back = [[MTLStencilDescriptor alloc] init];
        back.stencilCompareFunction = MTLCompareFunctionAlways;
        back.stencilFailureOperation = MTLStencilOperationKeep;
        back.depthFailureOperation = MTLStencilOperationKeep;
        back.depthStencilPassOperation = MTLStencilOperationDecrementWrap;
        back.readMask = 0xFF;
        back.writeMask = 0xFF;
        desc.backFaceStencil = back;

        mtl->stencilFillDSS = [mtl->device newDepthStencilStateWithDescriptor:desc];
    }
    {
        // Stencil cover: draw where stencil != 0, zero it
        MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
        desc.depthCompareFunction = MTLCompareFunctionAlways;
        desc.depthWriteEnabled = NO;

        MTLStencilDescriptor* stencil = [[MTLStencilDescriptor alloc] init];
        stencil.stencilCompareFunction = MTLCompareFunctionNotEqual;
        stencil.stencilFailureOperation = MTLStencilOperationZero;
        stencil.depthFailureOperation = MTLStencilOperationZero;
        stencil.depthStencilPassOperation = MTLStencilOperationZero;
        stencil.readMask = 0xFF;
        stencil.writeMask = 0xFF;
        desc.frontFaceStencil = stencil;
        desc.backFaceStencil = stencil;

        mtl->stencilCoverDSS = [mtl->device newDepthStencilStateWithDescriptor:desc];
    }
    {
        // Stroke stencil: only where stencil == 0, increment
        MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
        desc.depthCompareFunction = MTLCompareFunctionAlways;
        desc.depthWriteEnabled = NO;
        MTLStencilDescriptor* stencil = [[MTLStencilDescriptor alloc] init];
        stencil.stencilCompareFunction = MTLCompareFunctionEqual;
        stencil.stencilFailureOperation = MTLStencilOperationKeep;
        stencil.depthFailureOperation = MTLStencilOperationKeep;
        stencil.depthStencilPassOperation = MTLStencilOperationIncrementClamp;
        stencil.readMask = 0xFF;
        stencil.writeMask = 0xFF;
        desc.frontFaceStencil = stencil;
        desc.backFaceStencil = stencil;
        mtl->stencilStrokeDSS = [mtl->device newDepthStencilStateWithDescriptor:desc];
    }
    {
        // Stroke AA: where stencil == 0, keep
        MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
        desc.depthCompareFunction = MTLCompareFunctionAlways;
        desc.depthWriteEnabled = NO;
        MTLStencilDescriptor* stencil = [[MTLStencilDescriptor alloc] init];
        stencil.stencilCompareFunction = MTLCompareFunctionEqual;
        stencil.stencilFailureOperation = MTLStencilOperationKeep;
        stencil.depthFailureOperation = MTLStencilOperationKeep;
        stencil.depthStencilPassOperation = MTLStencilOperationKeep;
        stencil.readMask = 0xFF;
        stencil.writeMask = 0xFF;
        desc.frontFaceStencil = stencil;
        desc.backFaceStencil = stencil;
        mtl->stencilStrokeAA_DSS = [mtl->device newDepthStencilStateWithDescriptor:desc];
    }
    {
        // Stroke clear: always, zero stencil
        MTLDepthStencilDescriptor* desc = [[MTLDepthStencilDescriptor alloc] init];
        desc.depthCompareFunction = MTLCompareFunctionAlways;
        desc.depthWriteEnabled = NO;
        MTLStencilDescriptor* stencil = [[MTLStencilDescriptor alloc] init];
        stencil.stencilCompareFunction = MTLCompareFunctionAlways;
        stencil.stencilFailureOperation = MTLStencilOperationZero;
        stencil.depthFailureOperation = MTLStencilOperationZero;
        stencil.depthStencilPassOperation = MTLStencilOperationZero;
        stencil.readMask = 0xFF;
        stencil.writeMask = 0xFF;
        desc.frontFaceStencil = stencil;
        desc.backFaceStencil = stencil;
        mtl->stencilStrokeClearDSS = [mtl->device newDepthStencilStateWithDescriptor:desc];
    }

    // Sampler
    {
        MTLSamplerDescriptor* sdesc = [[MTLSamplerDescriptor alloc] init];
        sdesc.minFilter = MTLSamplerMinMagFilterLinear;
        sdesc.magFilter = MTLSamplerMinMagFilterLinear;
        sdesc.mipFilter = MTLSamplerMipFilterLinear;
        sdesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sdesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        mtl->sampler = [mtl->device newSamplerStateWithDescriptor:sdesc];
    }

    // Dummy 1x1 white texture
    {
        MTLTextureDescriptor* tdesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                         width:1
                                                                                        height:1
                                                                                     mipmapped:NO];
        tdesc.usage = MTLTextureUsageShaderRead;
        mtl->dummyTex = [mtl->device newTextureWithDescriptor:tdesc];
        uint32_t white = 0xFFFFFFFF;
        [mtl->dummyTex replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                         mipmapLevel:0
                           withBytes:&white
                         bytesPerRow:4];
    }

    return 1;
}

static int mtlnvg__renderCreateTexture(void* uptr, int type, int w, int h, int imageFlags, const unsigned char* data)
{
    MtlNVGcontext* mtl = static_cast<MtlNVGcontext*>(uptr);

    MTLPixelFormat format = (type == NVG_TEXTURE_RGBA) ? MTLPixelFormatRGBA8Unorm : MTLPixelFormatR8Unorm;
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                                    width:w
                                                                                   height:h
                                                                                mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> tex = [mtl->device newTextureWithDescriptor:desc];
    if (!tex)
        return 0;

    if (data)
    {
        int bpp = (type == NVG_TEXTURE_RGBA) ? 4 : 1;
        [tex replaceRegion:MTLRegionMake2D(0, 0, w, h)
               mipmapLevel:0
                 withBytes:data
               bytesPerRow:w * bpp];
    }

    MtlNVGtexture entry;
    entry.texId = ++mtl->textureIdCounter;
    entry.tex = tex;
    entry.width = w;
    entry.height = h;
    entry.type = type;
    entry.flags = imageFlags;
    mtl->textures.push_back(entry);
    return entry.texId;
}

static int mtlnvg__renderDeleteTexture(void* uptr, int image)
{
    MtlNVGcontext* mtl = static_cast<MtlNVGcontext*>(uptr);
    for (auto it = mtl->textures.begin(); it != mtl->textures.end(); ++it)
    {
        if (it->texId == image)
        {
            mtl->textures.erase(it);
            return 1;
        }
    }
    return 0;
}

static int mtlnvg__renderUpdateTexture(void* uptr, int image, int x, int y, int w, int h, const unsigned char* data)
{
    MtlNVGcontext* mtl = static_cast<MtlNVGcontext*>(uptr);
    MtlNVGtexture* tex = mtlnvg__findTexture(mtl, image);
    if (!tex)
        return 0;

    int bpp = (tex->type == NVG_TEXTURE_RGBA) ? 4 : 1;
    const unsigned char* src = data + (y * tex->width + x) * bpp;
    [tex->tex replaceRegion:MTLRegionMake2D(x, y, w, h)
                mipmapLevel:0
                  withBytes:src
                bytesPerRow:tex->width * bpp];
    return 1;
}

static int mtlnvg__renderGetTextureSize(void* uptr, int image, int* w, int* h)
{
    MtlNVGcontext* mtl = static_cast<MtlNVGcontext*>(uptr);
    MtlNVGtexture* tex = mtlnvg__findTexture(mtl, image);
    if (!tex)
        return 0;
    if (w)
        *w = tex->width;
    if (h)
        *h = tex->height;
    return 1;
}

static void mtlnvg__renderViewport(void* uptr, float width, float height, float devicePixelRatio)
{
    MtlNVGcontext* mtl = static_cast<MtlNVGcontext*>(uptr);
    mtl->viewWidth = width;
    mtl->viewHeight = height;
    mtl->devicePixelRatio = devicePixelRatio;
}

static void mtlnvg__renderCancel(void* uptr)
{
    MtlNVGcontext* mtl = static_cast<MtlNVGcontext*>(uptr);
    mtl->calls.clear();
    mtl->paths.clear();
    mtl->verts.clear();
    mtl->uniforms.clear();
}

// Convert triangle fan to triangle list (Metal doesn't support triangle fan)
static int mtlnvg__fanToTriangles(MtlNVGcontext* mtl, const NVGvertex* fan, int nfan)
{
    if (nfan < 3)
        return 0;
    int offset = static_cast<int>(mtl->verts.size());
    int ntris = nfan - 2;
    mtl->verts.resize(offset + ntris * 3);
    for (int i = 0; i < ntris; i++)
    {
        mtl->verts[offset + i * 3 + 0] = fan[0];
        mtl->verts[offset + i * 3 + 1] = fan[i + 1];
        mtl->verts[offset + i * 3 + 2] = fan[i + 2];
    }
    return ntris * 3;
}

static void mtlnvg__renderFill(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation,
    NVGscissor* scissor, float fringe, const float* bounds,
    const NVGpath* paths, int npaths)
{
    MtlNVGcontext* mtl = static_cast<MtlNVGcontext*>(uptr);

    MtlNVGcall call;
    call.type = MNVG_FILL;
    call.pathOffset = static_cast<int>(mtl->paths.size());
    call.pathCount = npaths;
    call.image = paint->image;
    call.blendFunc = mtlnvg__blendCompositeOperation(compositeOperation);

    if (npaths == 1 && paths[0].convex)
    {
        call.type = MNVG_CONVEXFILL;
    }

    // Allocate paths
    for (int i = 0; i < npaths; i++)
    {
        MtlNVGpath p;
        const NVGpath& src = paths[i];

        // Fill verts — convert fan to triangles
        if (src.nfill > 0)
        {
            p.fillOffset = static_cast<int>(mtl->verts.size());
            p.fillCount = mtlnvg__fanToTriangles(mtl, src.fill, src.nfill);
        }

        // Stroke verts (AA fringe)
        if (src.nstroke > 0)
        {
            p.strokeOffset = static_cast<int>(mtl->verts.size());
            p.strokeCount = src.nstroke;
            mtl->verts.insert(mtl->verts.end(), src.stroke, src.stroke + src.nstroke);
        }

        mtl->paths.push_back(p);
    }

    // Bounding box quad for stencil cover (only for non-convex)
    if (call.type == MNVG_FILL)
    {
        call.triangleOffset = static_cast<int>(mtl->verts.size());
        NVGvertex quad[4];
        quad[0] = { bounds[2], bounds[3], 0.5f, 1.0f };
        quad[1] = { bounds[2], bounds[1], 0.5f, 1.0f };
        quad[2] = { bounds[0], bounds[3], 0.5f, 1.0f };
        quad[3] = { bounds[0], bounds[1], 0.5f, 1.0f };
        mtl->verts.insert(mtl->verts.end(), quad, quad + 4);
        call.triangleCount = 4;

        // Two uniform blocks: simple (stencil) + real paint
        call.uniformOffset = mtlnvg__allocFragUniforms(mtl, 2);
        auto* frag = mtlnvg__fragUniformPtr(mtl, call.uniformOffset);
        memset(frag, 0, sizeof(*frag));
        frag->strokeThr = -1.0f;
        frag->type = MNVG_SHADER_SIMPLE;

        mtlnvg__convertPaint(mtl, mtlnvg__fragUniformPtr(mtl, call.uniformOffset + sizeof(MtlNVGfragUniforms)),
            paint, scissor, fringe, fringe, -1.0f);
    }
    else
    {
        // Convex: one uniform block
        call.uniformOffset = mtlnvg__allocFragUniforms(mtl, 1);
        mtlnvg__convertPaint(mtl, mtlnvg__fragUniformPtr(mtl, call.uniformOffset),
            paint, scissor, fringe, fringe, -1.0f);
    }

    mtl->calls.push_back(call);
}

static void mtlnvg__renderStroke(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation,
    NVGscissor* scissor, float fringe, float strokeWidth,
    const NVGpath* paths, int npaths)
{
    MtlNVGcontext* mtl = static_cast<MtlNVGcontext*>(uptr);

    MtlNVGcall call;
    call.type = MNVG_STROKE;
    call.pathOffset = static_cast<int>(mtl->paths.size());
    call.pathCount = npaths;
    call.image = paint->image;
    call.blendFunc = mtlnvg__blendCompositeOperation(compositeOperation);

    for (int i = 0; i < npaths; i++)
    {
        MtlNVGpath p;
        const NVGpath& src = paths[i];
        if (src.nstroke > 0)
        {
            p.strokeOffset = static_cast<int>(mtl->verts.size());
            p.strokeCount = src.nstroke;
            mtl->verts.insert(mtl->verts.end(), src.stroke, src.stroke + src.nstroke);
        }
        mtl->paths.push_back(p);
    }

    if (mtl->flags & NVG_STENCIL_STROKES)
    {
        call.uniformOffset = mtlnvg__allocFragUniforms(mtl, 2);
        mtlnvg__convertPaint(mtl, mtlnvg__fragUniformPtr(mtl, call.uniformOffset),
            paint, scissor, strokeWidth, fringe, -1.0f);
        mtlnvg__convertPaint(mtl, mtlnvg__fragUniformPtr(mtl, call.uniformOffset + sizeof(MtlNVGfragUniforms)),
            paint, scissor, strokeWidth, fringe, 1.0f - 0.5f / 255.0f);
    }
    else
    {
        call.uniformOffset = mtlnvg__allocFragUniforms(mtl, 1);
        mtlnvg__convertPaint(mtl, mtlnvg__fragUniformPtr(mtl, call.uniformOffset),
            paint, scissor, strokeWidth, fringe, -1.0f);
    }

    mtl->calls.push_back(call);
}

static void mtlnvg__renderTriangles(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation,
    NVGscissor* scissor, const NVGvertex* verts, int nverts, float fringe)
{
    MtlNVGcontext* mtl = static_cast<MtlNVGcontext*>(uptr);

    MtlNVGcall call;
    call.type = MNVG_TRIANGLES;
    call.image = paint->image;
    call.blendFunc = mtlnvg__blendCompositeOperation(compositeOperation);
    call.triangleOffset = static_cast<int>(mtl->verts.size());
    call.triangleCount = nverts;
    mtl->verts.insert(mtl->verts.end(), verts, verts + nverts);

    call.uniformOffset = mtlnvg__allocFragUniforms(mtl, 1);
    auto* frag = mtlnvg__fragUniformPtr(mtl, call.uniformOffset);
    mtlnvg__convertPaint(mtl, frag, paint, scissor, 1.0f, fringe, -1.0f);
    frag->type = MNVG_SHADER_IMG;

    mtl->calls.push_back(call);
}

// ---------------------------------------------------------------------------
// renderFlush — the main GPU work
// ---------------------------------------------------------------------------

static void mtlnvg__setBlendMode(id<MTLRenderCommandEncoder> enc,
    id<MTLRenderPipelineState> basePipeline,
    MtlNVGcontext* mtl, const MtlNVGblend& blend)
{
    // Metal sets blend state on the pipeline, not dynamically.
    // For simplicity we use the pre-created pipeline which uses premultiplied
    // alpha blending (srcRGB=One, dstRGB=OneMinusSrcAlpha). This covers the
    // default NanoVG composite operation. Custom blend modes would need
    // additional pipeline variants — deferred to a future enhancement.
    (void)blend;
    bool useAA = (mtl->flags & NVG_ANTIALIAS) != 0;
    [enc setRenderPipelineState:useAA ? mtl->pipelineFillAA : mtl->pipelineFillNoAA];
}

static void mtlnvg__renderFlush(void* uptr)
{
    MtlNVGcontext* mtl = static_cast<MtlNVGcontext*>(uptr);

    if (mtl->calls.empty() || !mtl->commandBuffer || !mtl->drawableTexture)
    {
        mtlnvg__renderCancel(uptr);
        return;
    }

    int texW = static_cast<int>(mtl->drawableTexture.width);
    int texH = static_cast<int>(mtl->drawableTexture.height);
    mtlnvg__ensureStencilTexture(mtl, texW, texH);

    // Create vertex buffer from accumulated vertices
    id<MTLBuffer> vertBuf = [mtl->device newBufferWithBytes:mtl->verts.data()
                                                     length:mtl->verts.size() * sizeof(NVGvertex)
                                                    options:MTLResourceStorageModeShared];

    // Create uniform buffer
    id<MTLBuffer> uniformBuf = [mtl->device newBufferWithBytes:mtl->uniforms.data()
                                                        length:mtl->uniforms.size()
                                                       options:MTLResourceStorageModeShared];

    // Create render pass descriptor
    MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    rpDesc.colorAttachments[0].texture = mtl->drawableTexture;
    rpDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
    rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpDesc.stencilAttachment.texture = mtl->stencilTex;
    rpDesc.stencilAttachment.loadAction = MTLLoadActionClear;
    rpDesc.stencilAttachment.storeAction = MTLStoreActionDontCare;
    rpDesc.stencilAttachment.clearStencil = 0;

    id<MTLRenderCommandEncoder> enc = [mtl->commandBuffer renderCommandEncoderWithDescriptor:rpDesc];
    [enc setLabel:@"NanoVG"];

    // Set viewport
    MTLViewport viewport;
    viewport.originX = 0;
    viewport.originY = 0;
    viewport.width = mtl->viewWidth;
    viewport.height = mtl->viewHeight;
    viewport.znear = 0;
    viewport.zfar = 1;
    [enc setViewport:viewport];

    // Bind vertex buffer
    [enc setVertexBuffer:vertBuf offset:0 atIndex:0];

    // View size uniform
    float viewSize[2] = { mtl->viewWidth, mtl->viewHeight };
    [enc setVertexBytes:viewSize length:sizeof(viewSize) atIndex:1];

    // Bind sampler and dummy texture
    [enc setFragmentSamplerState:mtl->sampler atIndex:0];
    [enc setFragmentTexture:mtl->dummyTex atIndex:0];

    // Default stencil state
    [enc setDepthStencilState:mtl->stencilDefaultDSS];
    [enc setStencilReferenceValue:0];

    // Cull mode
    [enc setCullMode:MTLCullModeBack];
    [enc setFrontFacingWinding:MTLWindingCounterClockwise];

    // Dispatch calls
    for (const auto& call : mtl->calls)
    {
        // Bind texture
        id<MTLTexture> callTex = mtl->dummyTex;
        if (call.image != 0)
        {
            MtlNVGtexture* t = mtlnvg__findTexture(mtl, call.image);
            if (t && t->tex)
                callTex = t->tex;
        }
        [enc setFragmentTexture:callTex atIndex:0];

        switch (call.type)
        {
        case MNVG_CONVEXFILL:
        {
            mtlnvg__setBlendMode(enc, nil, mtl, call.blendFunc);
            [enc setDepthStencilState:mtl->stencilDefaultDSS];
            [enc setFragmentBuffer:uniformBuf offset:call.uniformOffset atIndex:0];

            for (int i = 0; i < call.pathCount; i++)
            {
                const auto& p = mtl->paths[call.pathOffset + i];
                if (p.fillCount > 0)
                    [enc drawPrimitives:MTLPrimitiveTypeTriangle
                            vertexStart:p.fillOffset
                            vertexCount:p.fillCount];
                if (p.strokeCount > 0)
                    [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip
                            vertexStart:p.strokeOffset
                            vertexCount:p.strokeCount];
            }
            break;
        }
        case MNVG_FILL:
        {
            // Pass 1: stencil fill (no color, both faces)
            [enc setRenderPipelineState:mtl->pipelineStencil];
            [enc setDepthStencilState:mtl->stencilFillDSS];
            [enc setCullMode:MTLCullModeNone];
            [enc setFragmentBuffer:uniformBuf offset:call.uniformOffset atIndex:0];

            for (int i = 0; i < call.pathCount; i++)
            {
                const auto& p = mtl->paths[call.pathOffset + i];
                if (p.fillCount > 0)
                    [enc drawPrimitives:MTLPrimitiveTypeTriangle
                            vertexStart:p.fillOffset
                            vertexCount:p.fillCount];
            }
            [enc setCullMode:MTLCullModeBack];

            // Pass 2: AA fringe (where stencil == 0)
            int paintOffset = call.uniformOffset + static_cast<int>(sizeof(MtlNVGfragUniforms));
            mtlnvg__setBlendMode(enc, nil, mtl, call.blendFunc);
            if (mtl->flags & NVG_ANTIALIAS)
            {
                [enc setDepthStencilState:mtl->stencilDefaultDSS];
                [enc setFragmentBuffer:uniformBuf offset:paintOffset atIndex:0];
                for (int i = 0; i < call.pathCount; i++)
                {
                    const auto& p = mtl->paths[call.pathOffset + i];
                    if (p.strokeCount > 0)
                        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                vertexStart:p.strokeOffset
                                vertexCount:p.strokeCount];
                }
            }

            // Pass 3: cover (where stencil != 0, zero stencil)
            [enc setDepthStencilState:mtl->stencilCoverDSS];
            [enc setStencilReferenceValue:0];
            [enc setFragmentBuffer:uniformBuf offset:paintOffset atIndex:0];
            if (call.triangleCount > 0)
                [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip
                        vertexStart:call.triangleOffset
                        vertexCount:call.triangleCount];

            [enc setDepthStencilState:mtl->stencilDefaultDSS];
            break;
        }
        case MNVG_STROKE:
        {
            if (mtl->flags & NVG_STENCIL_STROKES)
            {
                // Pass 1: draw stroke, mark stencil
                mtlnvg__setBlendMode(enc, nil, mtl, call.blendFunc);
                [enc setDepthStencilState:mtl->stencilStrokeDSS];
                [enc setStencilReferenceValue:0];
                int strokeOffset = call.uniformOffset + static_cast<int>(sizeof(MtlNVGfragUniforms));
                [enc setFragmentBuffer:uniformBuf offset:strokeOffset atIndex:0];

                for (int i = 0; i < call.pathCount; i++)
                {
                    const auto& p = mtl->paths[call.pathOffset + i];
                    if (p.strokeCount > 0)
                        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                vertexStart:p.strokeOffset
                                vertexCount:p.strokeCount];
                }

                // Pass 2: AA pass
                [enc setDepthStencilState:mtl->stencilStrokeAA_DSS];
                [enc setFragmentBuffer:uniformBuf offset:call.uniformOffset atIndex:0];
                for (int i = 0; i < call.pathCount; i++)
                {
                    const auto& p = mtl->paths[call.pathOffset + i];
                    if (p.strokeCount > 0)
                        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                vertexStart:p.strokeOffset
                                vertexCount:p.strokeCount];
                }

                // Pass 3: clear stencil
                [enc setDepthStencilState:mtl->stencilStrokeClearDSS];
                for (int i = 0; i < call.pathCount; i++)
                {
                    const auto& p = mtl->paths[call.pathOffset + i];
                    if (p.strokeCount > 0)
                        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                vertexStart:p.strokeOffset
                                vertexCount:p.strokeCount];
                }
                [enc setDepthStencilState:mtl->stencilDefaultDSS];
            }
            else
            {
                mtlnvg__setBlendMode(enc, nil, mtl, call.blendFunc);
                [enc setDepthStencilState:mtl->stencilDefaultDSS];
                [enc setFragmentBuffer:uniformBuf offset:call.uniformOffset atIndex:0];
                for (int i = 0; i < call.pathCount; i++)
                {
                    const auto& p = mtl->paths[call.pathOffset + i];
                    if (p.strokeCount > 0)
                        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip
                                vertexStart:p.strokeOffset
                                vertexCount:p.strokeCount];
                }
            }
            break;
        }
        case MNVG_TRIANGLES:
        {
            mtlnvg__setBlendMode(enc, nil, mtl, call.blendFunc);
            [enc setDepthStencilState:mtl->stencilDefaultDSS];
            [enc setFragmentBuffer:uniformBuf offset:call.uniformOffset atIndex:0];
            if (call.triangleCount > 0)
                [enc drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:call.triangleOffset
                        vertexCount:call.triangleCount];
            break;
        }
        default:
            break;
        }
    }

    [enc endEncoding];

    // Reset deferred state
    mtl->calls.clear();
    mtl->paths.clear();
    mtl->verts.clear();
    mtl->uniforms.clear();
}

static void mtlnvg__renderDelete(void* uptr)
{
    MtlNVGcontext* mtl = static_cast<MtlNVGcontext*>(uptr);
    delete mtl;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

NVGcontext* nvgCreateMtl(id<MTLDevice> device, int flags)
{
    auto* mtl = new MtlNVGcontext();
    mtl->device = device;
    mtl->flags = flags;

    NVGparams params;
    memset(&params, 0, sizeof(params));
    params.renderCreate = mtlnvg__renderCreate;
    params.renderCreateTexture = mtlnvg__renderCreateTexture;
    params.renderDeleteTexture = mtlnvg__renderDeleteTexture;
    params.renderUpdateTexture = mtlnvg__renderUpdateTexture;
    params.renderGetTextureSize = mtlnvg__renderGetTextureSize;
    params.renderViewport = mtlnvg__renderViewport;
    params.renderCancel = mtlnvg__renderCancel;
    params.renderFlush = mtlnvg__renderFlush;
    params.renderFill = mtlnvg__renderFill;
    params.renderStroke = mtlnvg__renderStroke;
    params.renderTriangles = mtlnvg__renderTriangles;
    params.renderDelete = mtlnvg__renderDelete;
    params.userPtr = mtl;
    params.edgeAntiAlias = (flags & NVG_ANTIALIAS) ? 1 : 0;

    NVGcontext* ctx = nvgCreateInternal(&params);
    if (!ctx)
    {
        delete mtl;
        return nullptr;
    }
    return ctx;
}

void nvgDeleteMtl(NVGcontext* ctx)
{
    nvgDeleteInternal(ctx);
}

void nvgMtlSetFrameState(NVGcontext* ctx,
    id<MTLCommandBuffer> commandBuffer,
    id<MTLTexture> drawableTexture,
    uint32_t frameIndex)
{
    NVGparams* params = nvgInternalParams(ctx);
    MtlNVGcontext* mtl = static_cast<MtlNVGcontext*>(params->userPtr);
    mtl->commandBuffer = commandBuffer;
    mtl->drawableTexture = drawableTexture;
    mtl->frameIndex = frameIndex;
}

} // namespace draxul
