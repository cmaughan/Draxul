#include "satview_scene_pass.h"
#include "satview_texture_assets.h"

#include <draxul/log.h>
#include <draxul/metal/metal_render_context.h>
#include <draxul/metal/objc_ref.h>
#include <draxul/perf_timing.h>

#include <algorithm>
#include <cstring>
#include <limits>
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
    ObjCRef<id<MTLTexture>> earth_day_texture;
    ObjCRef<id<MTLTexture>> earth_night_texture;
    ObjCRef<id<MTLTexture>> earth_cloud_texture;
    ObjCRef<id<MTLSamplerState>> earth_sampler;
    ObjCRef<id<MTLBuffer>> scene_vertex_buffer;
    NSUInteger scene_vertex_count = 0;
    uint64_t uploaded_scene_revision = 0;

    id<MTLTexture> create_texture(id<MTLDevice> metal_device, const LoadedTextureImage& image)
    {
        if (!image.valid())
            return nil;

        MTLTextureDescriptor* desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:static_cast<NSUInteger>(image.width)
                                        height:static_cast<NSUInteger>(image.height)
                                     mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [metal_device newTextureWithDescriptor:desc];
        if (!texture)
            return nil;

        MTLRegion region = MTLRegionMake2D(0, 0,
            static_cast<NSUInteger>(image.width),
            static_cast<NSUInteger>(image.height));
        [texture replaceRegion:region
                    mipmapLevel:0
                      withBytes:image.rgba.data()
                    bytesPerRow:static_cast<NSUInteger>(image.width * 4)];
        return texture;
    }

    bool ensure_textures(id<MTLDevice> metal_device)
    {
        if (earth_day_texture.get() && earth_night_texture.get()
            && earth_cloud_texture.get() && earth_sampler.get())
            return true;

        EarthTextureImages images = load_earth_texture_images();
        earth_day_texture.reset(create_texture(metal_device, images.day));
        earth_night_texture.reset(create_texture(metal_device, images.night));
        earth_cloud_texture.reset(create_texture(metal_device, images.clouds));
        if (!earth_day_texture.get() || !earth_night_texture.get() || !earth_cloud_texture.get())
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal Earth textures");
            earth_day_texture.reset();
            earth_night_texture.reset();
            earth_cloud_texture.reset();
            return false;
        }

        MTLSamplerDescriptor* sampler_desc = [[MTLSamplerDescriptor alloc] init];
        sampler_desc.minFilter = MTLSamplerMinMagFilterLinear;
        sampler_desc.magFilter = MTLSamplerMinMagFilterLinear;
        sampler_desc.mipFilter = MTLSamplerMipFilterNotMipmapped;
        sampler_desc.sAddressMode = MTLSamplerAddressModeRepeat;
        sampler_desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        earth_sampler.reset([metal_device newSamplerStateWithDescriptor:sampler_desc]);
        if (!earth_sampler.get())
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal Earth sampler");
            earth_day_texture.reset();
            earth_night_texture.reset();
            earth_cloud_texture.reset();
            return false;
        }
        return true;
    }

    bool ensure(id<MTLDevice> new_device)
    {
        if (earth_pipeline.get() && orbit_pipeline.get()
            && depth_write_state.get() && depth_read_state.get()
            && earth_day_texture.get() && earth_night_texture.get()
            && earth_cloud_texture.get() && earth_sampler.get()
            && device.get() == new_device)
            return true;

        device.reset(new_device);
        earth_pipeline.reset();
        orbit_pipeline.reset();
        depth_write_state.reset();
        depth_read_state.reset();
        earth_day_texture.reset();
        earth_night_texture.reset();
        earth_cloud_texture.reset();
        earth_sampler.reset();
        scene_vertex_buffer.reset();
        scene_vertex_count = 0;
        uploaded_scene_revision = 0;
        if (!new_device)
            return false;
        if (!ensure_textures(new_device))
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

    bool ensure_scene_vertex_buffer(
        id<MTLDevice> metal_device,
        const std::vector<SatViewSceneVertex>& vertices,
        uint64_t revision)
    {
        if (revision == uploaded_scene_revision)
            return true;

        scene_vertex_count = static_cast<NSUInteger>(
            std::min<std::size_t>(vertices.size(), std::numeric_limits<NSUInteger>::max()));
        if (scene_vertex_count == 0)
        {
            uploaded_scene_revision = revision;
            return true;
        }

        const NSUInteger byte_size = scene_vertex_count * sizeof(SatViewSceneVertex);
        if (!scene_vertex_buffer.get() || [scene_vertex_buffer.get() length] < byte_size)
        {
            const NSUInteger current_size = scene_vertex_buffer.get() ? [scene_vertex_buffer.get() length] : 0;
            const NSUInteger new_size = std::max(byte_size, std::max<NSUInteger>(current_size * 2, 4096));
            id<MTLBuffer> replacement = [metal_device newBufferWithLength:new_size
                                                                   options:MTLResourceStorageModeShared];
            if (!replacement)
            {
                scene_vertex_count = 0;
                return false;
            }
            scene_vertex_buffer.reset(replacement);
        }

        void* dst = [scene_vertex_buffer.get() contents];
        if (!dst)
        {
            scene_vertex_count = 0;
            return false;
        }
        std::memcpy(dst, vertices.data(), byte_size);
        uploaded_scene_revision = revision;
        return true;
    }
};

SatViewScenePass::SatViewScenePass()
    : state_(std::make_unique<State>())
{
}

SatViewScenePass::~SatViewScenePass() = default;

void SatViewScenePass::record_prepass(IRenderContext& ctx)
{
    (void)ctx;
}

void SatViewScenePass::record(IRenderContext& ctx)
{
    PERF_MEASURE();
    auto* metal_ctx = static_cast<MetalRenderContext*>(&ctx);
    id<MTLRenderCommandEncoder> encoder = metal_ctx->encoder();
    if (!encoder || !state_->ensure(metal_ctx->device()))
        return;
    state_->ensure_scene_vertex_buffer(metal_ctx->device(), scene_vertices_, scene_revision_);

    [encoder setRenderPipelineState:state_->earth_pipeline.get()];
    [encoder setDepthStencilState:state_->depth_write_state.get()];
    [encoder setVertexBytes:&frame_ length:sizeof(frame_) atIndex:0];
    [encoder setFragmentBytes:&frame_ length:sizeof(frame_) atIndex:0];
    [encoder setFragmentTexture:state_->earth_day_texture.get() atIndex:0];
    [encoder setFragmentTexture:state_->earth_night_texture.get() atIndex:1];
    [encoder setFragmentTexture:state_->earth_cloud_texture.get() atIndex:2];
    [encoder setFragmentSamplerState:state_->earth_sampler.get() atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:kSatViewSphereVertexCount];

    if (state_->scene_vertex_count != 0 && state_->scene_vertex_buffer.get())
    {
        [encoder setRenderPipelineState:state_->orbit_pipeline.get()];
        [encoder setDepthStencilState:state_->depth_read_state.get()];
        [encoder setVertexBytes:&frame_ length:sizeof(frame_) atIndex:0];
        [encoder setVertexBuffer:state_->scene_vertex_buffer.get() offset:0 atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeLine
                    vertexStart:0
                    vertexCount:state_->scene_vertex_count];
    }
}

} // namespace draxul::satview
