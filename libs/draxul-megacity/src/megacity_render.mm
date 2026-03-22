#include "cube_render_pass.h"
#import <Metal/Metal.h>
#include <cmath>
#include <draxul/log.h>
#import <simd/simd.h>

// ---------------------------------------------------------------------------
// Matrix math helpers (column-major, Metal convention)
// ---------------------------------------------------------------------------
// clang-format off
static simd_float4x4 make_perspective(float fov_y_radians, float aspect, float near_z, float far_z)
{
    float ys = 1.0f / tanf(fov_y_radians * 0.5f);
    float xs = ys / aspect;
    float zs = far_z / (near_z - far_z);
    return (simd_float4x4){ .columns = {
        { xs,  0,  0,  0 },
        {  0, ys,  0,  0 },
        {  0,  0, zs, -1 },
        {  0,  0, zs * near_z, 0 }
    }};
}

static simd_float4x4 make_translation(float tx, float ty, float tz)
{
    simd_float4x4 m = matrix_identity_float4x4;
    m.columns[3] = (simd_float4){ tx, ty, tz, 1.0f };
    return m;
}

static simd_float4x4 make_rotation_y(float angle)
{
    float c = cosf(angle), s = sinf(angle);
    return (simd_float4x4){ .columns = {
        {  c, 0, s, 0 },
        {  0, 1, 0, 0 },
        { -s, 0, c, 0 },
        {  0, 0, 0, 1 }
    }};
}

static simd_float4x4 make_rotation_x(float angle)
{
    float c = cosf(angle), s = sinf(angle);
    return (simd_float4x4){ .columns = {
        { 1,  0,  0, 0 },
        { 0,  c,  s, 0 },
        { 0, -s,  c, 0 },
        { 0,  0,  0, 1 }
    }};
}
// clang-format on

// ---------------------------------------------------------------------------
// CubeRenderPass::State — Metal pipeline, lazily initialised on first record()
// ---------------------------------------------------------------------------
namespace draxul
{

struct CubeRenderPass::State
{
    id<MTLRenderPipelineState> pipeline = nil;
    bool initialized = false;

    bool init(id<MTLDevice> device)
    {
        if (initialized)
            return pipeline != nil;

        initialized = true;

        NSError* error = nil;
        NSString* exePath = [[NSBundle mainBundle] executablePath];
        NSString* exeDir = [exePath stringByDeletingLastPathComponent];
        NSString* libPath = [exeDir stringByAppendingPathComponent:@"shaders/megacity_cube.metallib"];
        NSURL* libURL = [NSURL fileURLWithPath:libPath];

        id<MTLLibrary> library = [device newLibraryWithURL:libURL error:&error];
        if (!library)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to load megacity_cube.metallib from %s: %s",
                [libPath UTF8String],
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }

        id<MTLFunction> vertFn = [library newFunctionWithName:@"cube_vertex"];
        id<MTLFunction> fragFn = [library newFunctionWithName:@"cube_fragment"];
        if (!vertFn || !fragFn)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: cube_vertex or cube_fragment not found in shader library");
            return false;
        }

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vertFn;
        desc.fragmentFunction = fragFn;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled = NO;

        pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pipeline)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to create cube pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }

        DRAXUL_LOG_INFO(LogCategory::App, "MegaCity: cube pipeline initialized");
        return true;
    }
};

CubeRenderPass::CubeRenderPass()
    : state_(std::make_unique<State>())
{
}

CubeRenderPass::~CubeRenderPass() = default;

void CubeRenderPass::record(IRenderContext& ctx)
{
    id<MTLCommandBuffer> cmdBuf = (__bridge id<MTLCommandBuffer>)ctx.native_command_buffer();
    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)ctx.native_render_encoder();

    if (!cmdBuf || !encoder)
        return;

    if (!state_->init(cmdBuf.device))
        return;

    int vx = ctx.viewport_x();
    int vy = ctx.viewport_y();
    int vw = ctx.viewport_w();
    int vh = ctx.viewport_h();
    float aspect = (vh > 0) ? (float)vw / (float)vh : 1.0f;
    simd_float4x4 proj = make_perspective(0.7854f /* 45 deg */, aspect, 0.1f, 100.0f);
    simd_float4x4 view = make_translation(0.0f, 0.0f, -3.0f);
    simd_float4x4 rot = simd_mul(make_rotation_y(angle_), make_rotation_x(angle_ * 0.4f));
    simd_float4x4 mvp = simd_mul(proj, simd_mul(view, rot));

    // Confine rendering to the pane region within the framebuffer.
    MTLViewport vp;
    vp.originX = vx;
    vp.originY = vy;
    vp.width = vw;
    vp.height = vh;
    vp.znear = 0.0;
    vp.zfar = 1.0;
    [encoder setViewport:vp];

    MTLScissorRect scissor;
    scissor.x = static_cast<NSUInteger>(vx);
    scissor.y = static_cast<NSUInteger>(vy);
    scissor.width = static_cast<NSUInteger>(vw);
    scissor.height = static_cast<NSUInteger>(vh);
    [encoder setScissorRect:scissor];

    [encoder setRenderPipelineState:state_->pipeline];
    [encoder setCullMode:MTLCullModeBack];
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    [encoder setVertexBytes:&mvp length:sizeof(mvp) atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:36];
}

} // namespace draxul
