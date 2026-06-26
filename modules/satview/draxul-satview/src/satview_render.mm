#include "satview_scene_pass.h"

#include <draxul/log.h>
#include <draxul/metal/metal_render_context.h>
#include <draxul/metal/objc_ref.h>
#include <draxul/perf_timing.h>

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

namespace draxul::satview
{

struct SatViewScenePass::State
{
    ObjCRef<id<MTLDevice>> device;
    ObjCRef<id<MTLRenderPipelineState>> earth_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> orbit_pipeline;
    ObjCRef<id<MTLDepthStencilState>> depth_write_state;
    ObjCRef<id<MTLDepthStencilState>> depth_read_state;

    bool ensure(id<MTLDevice> new_device)
    {
        if (earth_pipeline.get() && orbit_pipeline.get()
            && depth_write_state.get() && depth_read_state.get()
            && device.get() == new_device)
            return true;

        device.reset(new_device);
        earth_pipeline.reset();
        orbit_pipeline.reset();
        depth_write_state.reset();
        depth_read_state.reset();
        if (!new_device)
            return false;

        NSError* error = nil;
        NSString* exe_path = [[NSBundle mainBundle] executablePath];
        NSString* exe_dir = [exe_path stringByDeletingLastPathComponent];
        NSString* lib_path = [exe_dir stringByAppendingPathComponent:@"shaders/satview_scene.metallib"];
        id<MTLLibrary> library = [new_device newLibraryWithFile:lib_path error:&error];
        if (!library)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer,
                "SatView: failed to load satview_scene.metallib from %s: %s",
                [lib_path UTF8String],
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }

        id<MTLFunction> vertex = [library newFunctionWithName:@"satview_earth_vertex"];
        id<MTLFunction> fragment = [library newFunctionWithName:@"satview_earth_fragment"];
        id<MTLFunction> orbit_vertex = [library newFunctionWithName:@"satview_orbit_vertex"];
        id<MTLFunction> orbit_fragment = [library newFunctionWithName:@"satview_orbit_fragment"];
        if (!vertex || !fragment || !orbit_vertex || !orbit_fragment)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer,
                "SatView: Metal shader functions missing from satview_scene.metallib");
            return false;
        }

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vertex;
        desc.fragmentFunction = fragment;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        id<MTLRenderPipelineState> created = [new_device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal Earth pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        earth_pipeline.reset(created);

        desc.vertexFunction = orbit_vertex;
        desc.fragmentFunction = orbit_fragment;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        created = [new_device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal orbit pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            earth_pipeline.reset();
            return false;
        }
        orbit_pipeline.reset(created);

        MTLDepthStencilDescriptor* depth_desc = [[MTLDepthStencilDescriptor alloc] init];
        depth_desc.depthCompareFunction = MTLCompareFunctionLessEqual;
        depth_desc.depthWriteEnabled = YES;
        id<MTLDepthStencilState> depth = [new_device newDepthStencilStateWithDescriptor:depth_desc];
        if (!depth)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal depth-write state");
            earth_pipeline.reset();
            orbit_pipeline.reset();
            return false;
        }
        depth_write_state.reset(depth);

        depth_desc.depthWriteEnabled = NO;
        depth = [new_device newDepthStencilStateWithDescriptor:depth_desc];
        if (!depth)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal depth-read state");
            earth_pipeline.reset();
            orbit_pipeline.reset();
            depth_write_state.reset();
            return false;
        }
        depth_read_state.reset(depth);
        return true;
    }
};

SatViewScenePass::SatViewScenePass()
    : state_(std::make_unique<State>())
{
}

SatViewScenePass::~SatViewScenePass() = default;

void SatViewScenePass::record(IRenderContext& ctx)
{
    PERF_MEASURE();
    auto* metal_ctx = static_cast<MetalRenderContext*>(&ctx);
    id<MTLRenderCommandEncoder> encoder = metal_ctx->encoder();
    if (!encoder || !state_->ensure(metal_ctx->device()))
        return;

    [encoder setRenderPipelineState:state_->earth_pipeline.get()];
    [encoder setDepthStencilState:state_->depth_write_state.get()];
    [encoder setVertexBytes:&frame_ length:sizeof(frame_) atIndex:0];
    [encoder setFragmentBytes:&frame_ length:sizeof(frame_) atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:kSatViewSphereVertexCount];

    [encoder setRenderPipelineState:state_->orbit_pipeline.get()];
    [encoder setDepthStencilState:state_->depth_read_state.get()];
    [encoder drawPrimitives:MTLPrimitiveTypeLine
                vertexStart:0
                vertexCount:kSatViewOrbitVertexCount];
}

} // namespace draxul::satview
