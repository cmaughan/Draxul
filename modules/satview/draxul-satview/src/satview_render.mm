#include "satview_scene_pass.h"
#include "satview_texture_assets.h"

#include <draxul/log.h>
#include <draxul/metal/metal_render_context.h>
#include <draxul/metal/objc_ref.h>
#include <draxul/perf_timing.h>

#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <limits>
#include <vector>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

namespace draxul::satview
{

struct SatViewScenePass::State
{
    struct HdrTargets
    {
        ObjCRef<id<MTLTexture>> scene_msaa;
        ObjCRef<id<MTLTexture>> scene_depth;
        ObjCRef<id<MTLTexture>> scene_hdr;
        ObjCRef<id<MTLTexture>> scene_final_srgb;
        ObjCRef<id<MTLTexture>> scene_final_unorm;
        ObjCRef<id<MTLTexture>> msaa_difference;
        int width = 0;
        int height = 0;
        bool debug_ready = false;
    };

    ObjCRef<id<MTLDevice>> device;
    ObjCRef<id<MTLRenderPipelineState>> earth_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> moon_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> sun_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> cloud_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> atmosphere_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> ground_atmosphere_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> ground_surface_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> star_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> orbit_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> marker_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> tone_map_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> present_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> msaa_debug_pipeline;
    ObjCRef<id<MTLDepthStencilState>> depth_write_state;
    ObjCRef<id<MTLDepthStencilState>> depth_read_state;
    ObjCRef<id<MTLDepthStencilState>> depth_disabled_state;
    ObjCRef<id<MTLTexture>> earth_day_texture;
    ObjCRef<id<MTLTexture>> earth_night_texture;
    ObjCRef<id<MTLTexture>> earth_cloud_texture;
    ObjCRef<id<MTLTexture>> moon_texture;
    ObjCRef<id<MTLTexture>> sun_texture;
    ObjCRef<id<MTLTexture>> live_cloud_texture;
    ObjCRef<id<MTLSamplerState>> earth_sampler;
    ObjCRef<id<MTLSamplerState>> hdr_sampler;
    ObjCRef<id<MTLBuffer>> track_vertex_buffer;
    ObjCRef<id<MTLBuffer>> earth_track_vertex_buffer;
    ObjCRef<id<MTLBuffer>> marker_buffer;
    ObjCRef<id<MTLBuffer>> star_buffer;
    NSUInteger track_vertex_count = 0;
    NSUInteger earth_track_vertex_count = 0;
    NSUInteger marker_count = 0;
    NSUInteger star_count = 0;
    uint64_t uploaded_track_revision = 0;
    uint64_t uploaded_earth_track_revision = 0;
    uint64_t uploaded_marker_revision = 0;
    uint64_t uploaded_star_revision = 0;
    uint64_t uploaded_cloud_revision = 0;
    NSUInteger scene_sample_count = 1;
    std::vector<HdrTargets> hdr_targets;
    uint32_t last_prepass_frame = 0;

    id<MTLTexture> create_texture(id<MTLDevice> metal_device, const LoadedTextureImage& image)
    {
        if (!image.valid())
            return nil;

        MTLTextureDescriptor* desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB
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
            && earth_cloud_texture.get() && moon_texture.get() && sun_texture.get()
            && earth_sampler.get())
            return true;

        EarthTextureImages images = load_earth_texture_images();
        earth_day_texture.reset(create_texture(metal_device, images.day));
        earth_night_texture.reset(create_texture(metal_device, images.night));
        earth_cloud_texture.reset(create_texture(metal_device, images.clouds));
        moon_texture.reset(create_texture(metal_device, load_moon_texture_image()));
        sun_texture.reset(create_texture(metal_device, load_sun_texture_image()));
        if (!earth_day_texture.get() || !earth_night_texture.get()
            || !earth_cloud_texture.get() || !moon_texture.get() || !sun_texture.get())
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal scene textures");
            earth_day_texture.reset();
            earth_night_texture.reset();
            earth_cloud_texture.reset();
            moon_texture.reset();
            sun_texture.reset();
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
            moon_texture.reset();
            sun_texture.reset();
            return false;
        }
        return true;
    }

    bool ensure_cloud_texture(const LoadedTextureImage& image, uint64_t revision)
    {
        if (revision == uploaded_cloud_revision)
            return true;
        id<MTLTexture> texture = live_cloud_texture.get();
        if (!texture
            || static_cast<NSUInteger>(image.width) != texture.width
            || static_cast<NSUInteger>(image.height) != texture.height)
        {
            live_cloud_texture.reset(create_texture(device.get(), image));
            if (!live_cloud_texture.get())
                return false;
            uploaded_cloud_revision = revision;
            return true;
        }

        const MTLRegion region = MTLRegionMake2D(0, 0, texture.width, texture.height);
        [texture replaceRegion:region
                  mipmapLevel:0
                    withBytes:image.rgba.data()
                  bytesPerRow:static_cast<NSUInteger>(image.width * 4)];
        uploaded_cloud_revision = revision;
        return true;
    }

    bool ensure(id<MTLDevice> new_device)
    {
        if (earth_pipeline.get() && moon_pipeline.get() && sun_pipeline.get() && cloud_pipeline.get()
            && atmosphere_pipeline.get() && ground_atmosphere_pipeline.get() && orbit_pipeline.get()
            && ground_surface_pipeline.get() && star_pipeline.get()
            && tone_map_pipeline.get() && present_pipeline.get()
            && (scene_sample_count == 1 || msaa_debug_pipeline.get())
            && depth_write_state.get() && depth_read_state.get() && depth_disabled_state.get()
            && earth_day_texture.get() && earth_night_texture.get()
            && earth_cloud_texture.get() && moon_texture.get() && sun_texture.get()
            && earth_sampler.get() && hdr_sampler.get()
            && device.get() == new_device)
            return true;

        device.reset(new_device);
        earth_pipeline.reset();
        moon_pipeline.reset();
        sun_pipeline.reset();
        cloud_pipeline.reset();
        atmosphere_pipeline.reset();
        ground_atmosphere_pipeline.reset();
        ground_surface_pipeline.reset();
        star_pipeline.reset();
        orbit_pipeline.reset();
        marker_pipeline.reset();
        tone_map_pipeline.reset();
        present_pipeline.reset();
        msaa_debug_pipeline.reset();
        depth_write_state.reset();
        depth_read_state.reset();
        depth_disabled_state.reset();
        earth_day_texture.reset();
        earth_night_texture.reset();
        earth_cloud_texture.reset();
        moon_texture.reset();
        sun_texture.reset();
        live_cloud_texture.reset();
        earth_sampler.reset();
        hdr_sampler.reset();
        hdr_targets.clear();
        track_vertex_buffer.reset();
        earth_track_vertex_buffer.reset();
        marker_buffer.reset();
        star_buffer.reset();
        track_vertex_count = 0;
        earth_track_vertex_count = 0;
        marker_count = 0;
        star_count = 0;
        uploaded_track_revision = 0;
        uploaded_earth_track_revision = 0;
        uploaded_marker_revision = 0;
        uploaded_star_revision = 0;
        uploaded_cloud_revision = 0;
        if (!new_device)
            return false;
        scene_sample_count = [new_device supportsTextureSampleCount:4] ? 4
            : [new_device supportsTextureSampleCount:2] ? 2
                                                        : 1;
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
        id<MTLFunction> moon_vertex = [library newFunctionWithName:@"satview_moon_vertex"];
        id<MTLFunction> moon_fragment = [library newFunctionWithName:@"satview_moon_fragment"];
        id<MTLFunction> sun_vertex = [library newFunctionWithName:@"satview_sun_vertex"];
        id<MTLFunction> sun_fragment = [library newFunctionWithName:@"satview_sun_fragment"];
        id<MTLFunction> cloud_vertex = [library newFunctionWithName:@"satview_cloud_vertex"];
        id<MTLFunction> cloud_fragment = [library newFunctionWithName:@"satview_cloud_fragment"];
        id<MTLFunction> atmosphere_vertex = [library newFunctionWithName:@"satview_atmosphere_vertex"];
        id<MTLFunction> atmosphere_fragment = [library newFunctionWithName:@"satview_atmosphere_fragment"];
        id<MTLFunction> ground_atmosphere_vertex = [library newFunctionWithName:@"satview_ground_atmosphere_vertex"];
        id<MTLFunction> ground_atmosphere_fragment = [library newFunctionWithName:@"satview_ground_atmosphere_fragment"];
        id<MTLFunction> ground_surface_fragment = [library newFunctionWithName:@"satview_ground_surface_fragment"];
        id<MTLFunction> star_vertex = [library newFunctionWithName:@"satview_star_vertex"];
        id<MTLFunction> star_fragment = [library newFunctionWithName:@"satview_star_fragment"];
        id<MTLFunction> orbit_vertex = [library newFunctionWithName:@"satview_orbit_vertex"];
        id<MTLFunction> marker_vertex = [library newFunctionWithName:@"satview_marker_vertex"];
        id<MTLFunction> orbit_fragment = [library newFunctionWithName:@"satview_orbit_fragment"];
        id<MTLFunction> fullscreen_vertex = [library newFunctionWithName:@"satview_fullscreen_vertex"];
        id<MTLFunction> post_fragment = [library newFunctionWithName:@"satview_post_fragment"];
        id<MTLFunction> present_fragment = [library newFunctionWithName:@"satview_present_fragment"];
        id<MTLFunction> msaa_debug_fragment = [library newFunctionWithName:@"satview_msaa_debug_fragment"];
        if (!vertex || !fragment || !moon_vertex || !moon_fragment
            || !sun_vertex || !sun_fragment
            || !cloud_vertex || !cloud_fragment
            || !atmosphere_vertex || !atmosphere_fragment
            || !ground_atmosphere_vertex || !ground_atmosphere_fragment
            || !ground_surface_fragment
            || !star_vertex || !star_fragment
            || !orbit_vertex || !orbit_fragment
            || !fullscreen_vertex || !post_fragment || !present_fragment
            || (scene_sample_count > 1 && !msaa_debug_fragment))
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer,
                "SatView: required Metal shader functions missing from satview_scene.metallib");
            return false;
        }
        if (!marker_vertex)
        {
            DRAXUL_LOG_WARN(LogCategory::Renderer,
                "SatView: optional Metal marker shader missing from satview_scene.metallib");
        }

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vertex;
        desc.fragmentFunction = fragment;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        desc.rasterSampleCount = scene_sample_count;

        id<MTLRenderPipelineState> created = [new_device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal Earth pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        earth_pipeline.reset(created);

        desc.vertexFunction = moon_vertex;
        desc.fragmentFunction = moon_fragment;
        created = [new_device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal Moon pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            earth_pipeline.reset();
            return false;
        }
        moon_pipeline.reset(created);

        desc.vertexFunction = sun_vertex;
        desc.fragmentFunction = sun_fragment;
        created = [new_device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal Sun pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            earth_pipeline.reset();
            moon_pipeline.reset();
            return false;
        }
        sun_pipeline.reset(created);

        desc.vertexFunction = cloud_vertex;
        desc.fragmentFunction = cloud_fragment;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        created = [new_device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal cloud pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            earth_pipeline.reset();
            moon_pipeline.reset();
            return false;
        }
        cloud_pipeline.reset(created);

        desc.vertexFunction = atmosphere_vertex;
        desc.fragmentFunction = atmosphere_fragment;
        created = [new_device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal atmosphere pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            earth_pipeline.reset();
            moon_pipeline.reset();
            cloud_pipeline.reset();
            return false;
        }
        atmosphere_pipeline.reset(created);

        desc.vertexFunction = ground_atmosphere_vertex;
        desc.fragmentFunction = ground_atmosphere_fragment;
        created = [new_device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal ground atmosphere pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            earth_pipeline.reset();
            moon_pipeline.reset();
            cloud_pipeline.reset();
            atmosphere_pipeline.reset();
            return false;
        }
        ground_atmosphere_pipeline.reset(created);

        desc.vertexFunction = ground_atmosphere_vertex;
        desc.fragmentFunction = ground_surface_fragment;
        created = [new_device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal ground surface pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            earth_pipeline.reset();
            moon_pipeline.reset();
            cloud_pipeline.reset();
            atmosphere_pipeline.reset();
            ground_atmosphere_pipeline.reset();
            return false;
        }
        ground_surface_pipeline.reset(created);

        desc.vertexFunction = star_vertex;
        desc.fragmentFunction = star_fragment;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorZero;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        created = [new_device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal star pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            earth_pipeline.reset();
            moon_pipeline.reset();
            cloud_pipeline.reset();
            atmosphere_pipeline.reset();
            ground_atmosphere_pipeline.reset();
            ground_surface_pipeline.reset();
            return false;
        }
        star_pipeline.reset(created);

        desc.vertexFunction = orbit_vertex;
        desc.fragmentFunction = orbit_fragment;
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
            moon_pipeline.reset();
            cloud_pipeline.reset();
            atmosphere_pipeline.reset();
            ground_atmosphere_pipeline.reset();
            ground_surface_pipeline.reset();
            star_pipeline.reset();
            return false;
        }
        orbit_pipeline.reset(created);

        if (marker_vertex)
        {
            desc.vertexFunction = marker_vertex;
            desc.fragmentFunction = orbit_fragment;
            created = [new_device newRenderPipelineStateWithDescriptor:desc error:&error];
            if (!created)
            {
                DRAXUL_LOG_WARN(LogCategory::Renderer, "SatView: failed to create optional Metal marker pipeline: %s",
                    error ? [[error localizedDescription] UTF8String] : "unknown");
            }
            else
            {
                marker_pipeline.reset(created);
            }
        }

        MTLRenderPipelineDescriptor* fullscreen_desc = [[MTLRenderPipelineDescriptor alloc] init];
        fullscreen_desc.vertexFunction = fullscreen_vertex;
        fullscreen_desc.fragmentFunction = post_fragment;
        fullscreen_desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
        fullscreen_desc.rasterSampleCount = 1;
        created = [new_device newRenderPipelineStateWithDescriptor:fullscreen_desc error:&error];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal tone-map pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        tone_map_pipeline.reset(created);

        fullscreen_desc.fragmentFunction = present_fragment;
        fullscreen_desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        created = [new_device newRenderPipelineStateWithDescriptor:fullscreen_desc error:&error];
        if (!created)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal present pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        present_pipeline.reset(created);

        if (scene_sample_count > 1)
        {
            fullscreen_desc.fragmentFunction = msaa_debug_fragment;
            fullscreen_desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
            created = [new_device newRenderPipelineStateWithDescriptor:fullscreen_desc error:&error];
            if (!created)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal MSAA debug pipeline: %s",
                    error ? [[error localizedDescription] UTF8String] : "unknown");
                return false;
            }
            msaa_debug_pipeline.reset(created);
        }

        MTLSamplerDescriptor* hdr_sampler_desc = [[MTLSamplerDescriptor alloc] init];
        hdr_sampler_desc.minFilter = MTLSamplerMinMagFilterLinear;
        hdr_sampler_desc.magFilter = MTLSamplerMinMagFilterLinear;
        hdr_sampler_desc.mipFilter = MTLSamplerMipFilterNotMipmapped;
        hdr_sampler_desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        hdr_sampler_desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        hdr_sampler.reset([new_device newSamplerStateWithDescriptor:hdr_sampler_desc]);
        if (!hdr_sampler.get())
            return false;

        DRAXUL_LOG_INFO(LogCategory::Renderer, "SatView: HDR scene using %lux MSAA",
            static_cast<unsigned long>(scene_sample_count));

        MTLDepthStencilDescriptor* depth_desc = [[MTLDepthStencilDescriptor alloc] init];
        depth_desc.depthCompareFunction = MTLCompareFunctionLessEqual;
        depth_desc.depthWriteEnabled = YES;
        id<MTLDepthStencilState> depth = [new_device newDepthStencilStateWithDescriptor:depth_desc];
        if (!depth)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal depth-write state");
            earth_pipeline.reset();
            moon_pipeline.reset();
            cloud_pipeline.reset();
            atmosphere_pipeline.reset();
            ground_atmosphere_pipeline.reset();
            ground_surface_pipeline.reset();
            star_pipeline.reset();
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
            moon_pipeline.reset();
            cloud_pipeline.reset();
            atmosphere_pipeline.reset();
            ground_atmosphere_pipeline.reset();
            ground_surface_pipeline.reset();
            star_pipeline.reset();
            orbit_pipeline.reset();
            depth_write_state.reset();
            return false;
        }
        depth_read_state.reset(depth);

        depth_desc.depthCompareFunction = MTLCompareFunctionAlways;
        depth_desc.depthWriteEnabled = NO;
        depth = [new_device newDepthStencilStateWithDescriptor:depth_desc];
        if (!depth)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal disabled-depth state");
            earth_pipeline.reset();
            moon_pipeline.reset();
            cloud_pipeline.reset();
            atmosphere_pipeline.reset();
            ground_atmosphere_pipeline.reset();
            ground_surface_pipeline.reset();
            star_pipeline.reset();
            orbit_pipeline.reset();
            depth_write_state.reset();
            depth_read_state.reset();
            return false;
        }
        depth_disabled_state.reset(depth);
        return true;
    }

    bool ensure_hdr_targets(id<MTLDevice> metal_device, uint32_t frame_count, int width, int height)
    {
        frame_count = std::max(1u, frame_count);
        if (hdr_targets.size() == frame_count && !hdr_targets.empty()
            && hdr_targets.front().width == width && hdr_targets.front().height == height)
            return true;

        hdr_targets.clear();
        hdr_targets.resize(frame_count);
        const bool multisampled = scene_sample_count > 1;
        for (auto& targets : hdr_targets)
        {
            if (multisampled)
            {
                MTLTextureDescriptor* msaa_desc = [MTLTextureDescriptor
                    texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                 width:static_cast<NSUInteger>(width)
                                                height:static_cast<NSUInteger>(height)
                                             mipmapped:NO];
                msaa_desc.textureType = MTLTextureType2DMultisample;
                msaa_desc.sampleCount = scene_sample_count;
                msaa_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
                msaa_desc.storageMode = MTLStorageModePrivate;
                targets.scene_msaa.reset([metal_device newTextureWithDescriptor:msaa_desc]);
            }

            MTLTextureDescriptor* depth_desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                             width:static_cast<NSUInteger>(width)
                                            height:static_cast<NSUInteger>(height)
                                         mipmapped:NO];
            depth_desc.textureType = multisampled ? MTLTextureType2DMultisample : MTLTextureType2D;
            depth_desc.sampleCount = scene_sample_count;
            depth_desc.usage = MTLTextureUsageRenderTarget;
            depth_desc.storageMode = MTLStorageModePrivate;
            targets.scene_depth.reset([metal_device newTextureWithDescriptor:depth_desc]);

            MTLTextureDescriptor* hdr_desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                             width:static_cast<NSUInteger>(width)
                                            height:static_cast<NSUInteger>(height)
                                         mipmapped:NO];
            hdr_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            hdr_desc.storageMode = MTLStorageModePrivate;
            targets.scene_hdr.reset([metal_device newTextureWithDescriptor:hdr_desc]);

            MTLTextureDescriptor* final_desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm_sRGB
                                             width:static_cast<NSUInteger>(width)
                                            height:static_cast<NSUInteger>(height)
                                         mipmapped:NO];
            final_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead
                | MTLTextureUsagePixelFormatView;
            final_desc.storageMode = MTLStorageModePrivate;
            targets.scene_final_srgb.reset([metal_device newTextureWithDescriptor:final_desc]);
            if (targets.scene_final_srgb.get())
            {
                targets.scene_final_unorm.reset(
                    [targets.scene_final_srgb.get() newTextureViewWithPixelFormat:MTLPixelFormatBGRA8Unorm]);
            }

            MTLTextureDescriptor* debug_desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                             width:static_cast<NSUInteger>(width)
                                            height:static_cast<NSUInteger>(height)
                                         mipmapped:NO];
            debug_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            debug_desc.storageMode = MTLStorageModePrivate;
            targets.msaa_difference.reset([metal_device newTextureWithDescriptor:debug_desc]);

            if ((multisampled && !targets.scene_msaa.get())
                || !targets.scene_depth.get() || !targets.scene_hdr.get()
                || !targets.scene_final_srgb.get() || !targets.scene_final_unorm.get()
                || !targets.msaa_difference.get())
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create Metal HDR targets");
                hdr_targets.clear();
                return false;
            }
            targets.width = width;
            targets.height = height;
        }
        return true;
    }

    template <typename T>
    bool ensure_buffer(
        id<MTLDevice> metal_device,
        const std::vector<T>& items,
        uint64_t revision,
        ObjCRef<id<MTLBuffer>>& buffer,
        NSUInteger& item_count,
        uint64_t& uploaded_revision)
    {
        if (revision == uploaded_revision)
            return true;

        item_count = static_cast<NSUInteger>(
            std::min<std::size_t>(items.size(), std::numeric_limits<NSUInteger>::max()));
        if (item_count == 0)
        {
            uploaded_revision = revision;
            return true;
        }

        const NSUInteger byte_size = item_count * sizeof(T);
        if (!buffer.get() || [buffer.get() length] < byte_size)
        {
            const NSUInteger current_size = buffer.get() ? [buffer.get() length] : 0;
            const NSUInteger new_size = std::max(byte_size, std::max<NSUInteger>(current_size * 2, 4096));
            id<MTLBuffer> replacement = [metal_device newBufferWithLength:new_size
                                                                   options:MTLResourceStorageModeShared];
            if (!replacement)
            {
                item_count = 0;
                return false;
            }
            buffer.reset(replacement);
        }

        void* dst = [buffer.get() contents];
        if (!dst)
        {
            item_count = 0;
            return false;
        }
        std::memcpy(dst, items.data(), byte_size);
        uploaded_revision = revision;
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
    PERF_MEASURE();
    auto* metal_ctx = static_cast<MetalRenderContext*>(&ctx);
    id<MTLCommandBuffer> command_buffer = metal_ctx->command_buffer();
    if (!command_buffer || !state_->ensure(metal_ctx->device()))
        return;
    const int width = std::max(1, ctx.viewport_w());
    const int height = std::max(1, ctx.viewport_h());
    if (!state_->ensure_hdr_targets(
            metal_ctx->device(), ctx.buffered_frame_count(), width, height))
        return;
    if (pending_cloud_image_
        && state_->ensure_cloud_texture(*pending_cloud_image_, cloud_revision_))
    {
        pending_cloud_image_.reset();
    }
    state_->ensure_buffer(
        metal_ctx->device(),
        track_vertices_,
        track_revision_,
        state_->track_vertex_buffer,
        state_->track_vertex_count,
        state_->uploaded_track_revision);
    state_->ensure_buffer(
        metal_ctx->device(),
        earth_track_vertices_,
        earth_track_revision_,
        state_->earth_track_vertex_buffer,
        state_->earth_track_vertex_count,
        state_->uploaded_earth_track_revision);
    state_->ensure_buffer(
        metal_ctx->device(),
        markers_,
        marker_revision_,
        state_->marker_buffer,
        state_->marker_count,
        state_->uploaded_marker_revision);
    state_->ensure_buffer(
        metal_ctx->device(),
        stars_,
        star_revision_,
        state_->star_buffer,
        state_->star_count,
        state_->uploaded_star_revision);

    const uint32_t frame_index = ctx.frame_index() % static_cast<uint32_t>(state_->hdr_targets.size());
    auto& targets = state_->hdr_targets[frame_index];
    MTLRenderPassDescriptor* scene_desc = [MTLRenderPassDescriptor renderPassDescriptor];
    scene_desc.colorAttachments[0].texture = state_->scene_sample_count > 1
        ? targets.scene_msaa.get()
        : targets.scene_hdr.get();
    scene_desc.colorAttachments[0].loadAction = MTLLoadActionClear;
    scene_desc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    if (state_->scene_sample_count > 1)
    {
        scene_desc.colorAttachments[0].resolveTexture = targets.scene_hdr.get();
        scene_desc.colorAttachments[0].storeAction = hdr_debug_enabled_
            ? MTLStoreActionStoreAndMultisampleResolve
            : MTLStoreActionMultisampleResolve;
    }
    else
    {
        scene_desc.colorAttachments[0].storeAction = MTLStoreActionStore;
    }
    scene_desc.depthAttachment.texture = targets.scene_depth.get();
    scene_desc.depthAttachment.loadAction = MTLLoadActionClear;
    scene_desc.depthAttachment.storeAction = MTLStoreActionDontCare;
    scene_desc.depthAttachment.clearDepth = 1.0;

    id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:scene_desc];
    if (!encoder)
        return;
    MTLViewport viewport{ 0.0, 0.0, static_cast<double>(width), static_cast<double>(height), 0.0, 1.0 };
    MTLScissorRect scissor{ 0, 0, static_cast<NSUInteger>(width), static_cast<NSUInteger>(height) };
    [encoder setViewport:viewport];
    [encoder setScissorRect:scissor];

    [encoder setDepthStencilState:state_->depth_write_state.get()];
    [encoder setFragmentTexture:state_->earth_day_texture.get() atIndex:0];
    [encoder setFragmentTexture:state_->earth_night_texture.get() atIndex:1];
    [encoder setFragmentTexture:state_->earth_cloud_texture.get() atIndex:2];
    [encoder setFragmentTexture:(state_->live_cloud_texture.get()
                                    ? state_->live_cloud_texture.get()
                                    : state_->earth_cloud_texture.get())
                         atIndex:3];
    [encoder setFragmentTexture:state_->moon_texture.get() atIndex:4];
    [encoder setFragmentTexture:state_->sun_texture.get() atIndex:5];
    [encoder setFragmentSamplerState:state_->earth_sampler.get() atIndex:0];

    auto draw_stars = [&]() {
        const NSUInteger draw_count = state_->star_count;
        if (draw_count == 0 || !state_->star_buffer.get() || !state_->star_pipeline.get())
            return;

        SatViewFrameUniforms star_frame = frame_;
        star_frame.render_params.x = star_min_magnitude_;
        star_frame.render_params.y = star_max_magnitude_;
        star_frame.render_params.z = star_projection_aspect_scale_;
        star_frame.render_params.w = star_brightness_scale_;
        [encoder setRenderPipelineState:state_->star_pipeline.get()];
        [encoder setDepthStencilState:state_->depth_disabled_state.get()];
        [encoder setVertexBytes:&star_frame length:sizeof(star_frame) atIndex:0];
        [encoder setVertexBuffer:state_->star_buffer.get() offset:0 atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:kSatViewStarVerticesPerInstance
                  instanceCount:draw_count];
    };

    if (map_projection_)
    {
        [encoder setRenderPipelineState:sun_map_projection_
                ? state_->sun_pipeline.get()
                : moon_map_projection_ ? state_->moon_pipeline.get()
                                       : state_->earth_pipeline.get()];
        [encoder setVertexBytes:&frame_ length:sizeof(frame_) atIndex:0];
        [encoder setFragmentBytes:&frame_ length:sizeof(frame_) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:6];
    }
    else if (ground_projection_)
    {
        if (atmosphere_enabled_)
        {
            [encoder setRenderPipelineState:state_->ground_atmosphere_pipeline.get()];
            [encoder setDepthStencilState:state_->depth_disabled_state.get()];
            [encoder setVertexBytes:&frame_ length:sizeof(frame_) atIndex:0];
            [encoder setFragmentBytes:&frame_ length:sizeof(frame_) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:3];
        }

        draw_stars();

        if (sun_enabled_ && sun_position_radius_.w > 0.0f)
        {
            SatViewFrameUniforms sun_frame = frame_;
            sun_frame.camera_orientation = sun_position_radius_;
            sun_frame.sun_dir_time = glm::vec4(
                sun_body_to_render_.x,
                sun_body_to_render_.y,
                sun_body_to_render_.z,
                sun_body_to_render_.w);
            [encoder setRenderPipelineState:state_->sun_pipeline.get()];
            [encoder setDepthStencilState:state_->depth_write_state.get()];
            [encoder setVertexBytes:&sun_frame length:sizeof(sun_frame) atIndex:0];
            [encoder setFragmentBytes:&sun_frame length:sizeof(sun_frame) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:kSatViewSphereVertexCount];
        }

        if (moon_enabled_ && moon_position_radius_.w > 0.0f)
        {
            SatViewFrameUniforms moon_frame = frame_;
            moon_frame.camera_orientation = moon_position_radius_;
            [encoder setRenderPipelineState:state_->moon_pipeline.get()];
            [encoder setDepthStencilState:state_->depth_write_state.get()];
            [encoder setVertexBytes:&moon_frame length:sizeof(moon_frame) atIndex:0];
            [encoder setFragmentBytes:&moon_frame length:sizeof(moon_frame) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:kSatViewSphereVertexCount];
        }

        [encoder setRenderPipelineState:state_->ground_surface_pipeline.get()];
        [encoder setDepthStencilState:state_->depth_disabled_state.get()];
        [encoder setVertexBytes:&frame_ length:sizeof(frame_) atIndex:0];
        [encoder setFragmentBytes:&frame_ length:sizeof(frame_) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:3];
    }
    else
    {
        draw_stars();
        [encoder setDepthStencilState:state_->depth_write_state.get()];

        if (sun_enabled_ && sun_position_radius_.w > 0.0f)
        {
            SatViewFrameUniforms sun_frame = frame_;
            sun_frame.camera_orientation = sun_position_radius_;
            sun_frame.sun_dir_time = glm::vec4(
                sun_body_to_render_.x,
                sun_body_to_render_.y,
                sun_body_to_render_.z,
                sun_body_to_render_.w);
            [encoder setRenderPipelineState:state_->sun_pipeline.get()];
            [encoder setVertexBytes:&sun_frame length:sizeof(sun_frame) atIndex:0];
            [encoder setFragmentBytes:&sun_frame length:sizeof(sun_frame) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:kSatViewSphereVertexCount];
        }

        if (moon_enabled_ && moon_position_radius_.w > 0.0f)
        {
            SatViewFrameUniforms moon_frame = frame_;
            moon_frame.camera_orientation = moon_position_radius_;
            [encoder setRenderPipelineState:state_->moon_pipeline.get()];
            [encoder setVertexBytes:&moon_frame length:sizeof(moon_frame) atIndex:0];
            [encoder setFragmentBytes:&moon_frame length:sizeof(moon_frame) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:kSatViewSphereVertexCount];
        }

        [encoder setRenderPipelineState:state_->earth_pipeline.get()];
        [encoder setVertexBytes:&frame_ length:sizeof(frame_) atIndex:0];
        [encoder setFragmentBytes:&frame_ length:sizeof(frame_) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:kSatViewSphereVertexCount];

        if (frame_.sun_dir_time.w > 0.5f)
        {
            [encoder setRenderPipelineState:state_->cloud_pipeline.get()];
            [encoder setDepthStencilState:state_->depth_read_state.get()];
            [encoder setVertexBytes:&frame_ length:sizeof(frame_) atIndex:0];
            [encoder setFragmentBytes:&frame_ length:sizeof(frame_) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:kSatViewSphereVertexCount];
        }

        if (atmosphere_enabled_)
        {
            [encoder setRenderPipelineState:state_->atmosphere_pipeline.get()];
            [encoder setDepthStencilState:state_->depth_read_state.get()];
            [encoder setVertexBytes:&frame_ length:sizeof(frame_) atIndex:0];
            [encoder setFragmentBytes:&frame_ length:sizeof(frame_) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:kSatViewSphereVertexCount];
        }
    }

    if (state_->earth_track_vertex_count != 0 && state_->earth_track_vertex_buffer.get())
    {
        [encoder setRenderPipelineState:state_->orbit_pipeline.get()];
        [encoder setDepthStencilState:state_->depth_read_state.get()];
        [encoder setVertexBuffer:state_->earth_track_vertex_buffer.get() offset:0 atIndex:1];
        SatViewFrameUniforms earth_track_frame = frame_;
        earth_track_frame.render_params.w = -1.0f;
        if (map_projection_)
        {
            for (int copy = -1; copy <= 1; ++copy)
            {
                earth_track_frame.camera_orientation.w = static_cast<float>(copy * 2);
                [encoder setVertexBytes:&earth_track_frame length:sizeof(earth_track_frame) atIndex:0];
                [encoder drawPrimitives:MTLPrimitiveTypeLine
                            vertexStart:0
                            vertexCount:state_->earth_track_vertex_count];
            }
        }
        else
        {
            earth_track_frame.camera_orientation = sun_position_radius_;
            [encoder setVertexBytes:&earth_track_frame length:sizeof(earth_track_frame) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeLine
                        vertexStart:0
                        vertexCount:state_->earth_track_vertex_count];
        }
        [encoder setVertexBytes:&frame_ length:sizeof(frame_) atIndex:0];
    }

    if (state_->track_vertex_count != 0 && state_->track_vertex_buffer.get())
    {
        [encoder setRenderPipelineState:state_->orbit_pipeline.get()];
        [encoder setDepthStencilState:state_->depth_read_state.get()];
        [encoder setVertexBuffer:state_->track_vertex_buffer.get() offset:0 atIndex:1];
        if (map_projection_)
        {
            for (int copy = -1; copy <= 1; ++copy)
            {
                SatViewFrameUniforms track_frame = frame_;
                track_frame.camera_orientation.w = static_cast<float>(copy * 2);
                [encoder setVertexBytes:&track_frame length:sizeof(track_frame) atIndex:0];
                [encoder drawPrimitives:MTLPrimitiveTypeLine
                            vertexStart:0
                            vertexCount:state_->track_vertex_count];
            }
        }
        else
        {
            [encoder setVertexBytes:&frame_ length:sizeof(frame_) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeLine
                        vertexStart:0
                        vertexCount:state_->track_vertex_count];
        }
    }

    if (state_->marker_count != 0 && state_->marker_buffer.get() && state_->marker_pipeline.get())
    {
        [encoder setRenderPipelineState:state_->marker_pipeline.get()];
        [encoder setDepthStencilState:state_->depth_read_state.get()];
        [encoder setVertexBytes:&frame_ length:sizeof(frame_) atIndex:0];
        [encoder setVertexBuffer:state_->marker_buffer.get() offset:0 atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeLine
                    vertexStart:0
                    vertexCount:kSatViewMarkerVerticesPerInstance
                  instanceCount:state_->marker_count];
    }

    [encoder endEncoding];

    if (hdr_debug_enabled_)
    {
        MTLRenderPassDescriptor* debug_desc = [MTLRenderPassDescriptor renderPassDescriptor];
        debug_desc.colorAttachments[0].texture = targets.msaa_difference.get();
        debug_desc.colorAttachments[0].loadAction = MTLLoadActionClear;
        debug_desc.colorAttachments[0].storeAction = MTLStoreActionStore;
        debug_desc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
        id<MTLRenderCommandEncoder> debug_encoder =
            [command_buffer renderCommandEncoderWithDescriptor:debug_desc];
        if (debug_encoder)
        {
            [debug_encoder setViewport:viewport];
            [debug_encoder setScissorRect:scissor];
            if (state_->scene_sample_count > 1 && state_->msaa_debug_pipeline.get())
            {
                const uint32_t sample_count = static_cast<uint32_t>(state_->scene_sample_count);
                [debug_encoder setRenderPipelineState:state_->msaa_debug_pipeline.get()];
                [debug_encoder setFragmentBytes:&sample_count length:sizeof(sample_count) atIndex:0];
                [debug_encoder setFragmentTexture:targets.scene_msaa.get() atIndex:0];
                [debug_encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            }
            [debug_encoder endEncoding];
            targets.debug_ready = true;
        }
    }

    MTLRenderPassDescriptor* post_desc = [MTLRenderPassDescriptor renderPassDescriptor];
    post_desc.colorAttachments[0].texture = targets.scene_final_srgb.get();
    post_desc.colorAttachments[0].loadAction = MTLLoadActionClear;
    post_desc.colorAttachments[0].storeAction = MTLStoreActionStore;
    post_desc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    id<MTLRenderCommandEncoder> post_encoder =
        [command_buffer renderCommandEncoderWithDescriptor:post_desc];
    if (!post_encoder)
        return;
    const glm::vec4 tone_map(tone_map_exposure_, tone_map_white_point_, 0.0f, 0.0f);
    [post_encoder setViewport:viewport];
    [post_encoder setScissorRect:scissor];
    [post_encoder setRenderPipelineState:state_->tone_map_pipeline.get()];
    [post_encoder setFragmentBytes:&tone_map length:sizeof(tone_map) atIndex:0];
    [post_encoder setFragmentTexture:targets.scene_hdr.get() atIndex:0];
    [post_encoder setFragmentSamplerState:state_->hdr_sampler.get() atIndex:0];
    [post_encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [post_encoder endEncoding];
    state_->last_prepass_frame = frame_index;
}

void SatViewScenePass::record(IRenderContext& ctx)
{
    PERF_MEASURE();
    auto* metal_ctx = static_cast<MetalRenderContext*>(&ctx);
    id<MTLRenderCommandEncoder> encoder = metal_ctx->encoder();
    if (!encoder || state_->hdr_targets.empty() || !state_->present_pipeline.get())
        return;
    const uint32_t frame_index = ctx.frame_index() % static_cast<uint32_t>(state_->hdr_targets.size());
    const auto& targets = state_->hdr_targets[frame_index];
    if (!targets.scene_final_unorm.get())
        return;

    MTLViewport viewport;
    viewport.originX = ctx.viewport_x();
    viewport.originY = ctx.viewport_y();
    viewport.width = ctx.viewport_w();
    viewport.height = ctx.viewport_h();
    viewport.znear = 0.0;
    viewport.zfar = 1.0;
    MTLScissorRect scissor;
    scissor.x = static_cast<NSUInteger>(std::max(0, ctx.viewport_x()));
    scissor.y = static_cast<NSUInteger>(std::max(0, ctx.viewport_y()));
    scissor.width = static_cast<NSUInteger>(std::max(0, ctx.viewport_w()));
    scissor.height = static_cast<NSUInteger>(std::max(0, ctx.viewport_h()));
    [encoder setViewport:viewport];
    [encoder setScissorRect:scissor];
    [encoder setRenderPipelineState:state_->present_pipeline.get()];
    [encoder setFragmentTexture:targets.scene_final_unorm.get() atIndex:0];
    [encoder setFragmentSamplerState:state_->hdr_sampler.get() atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

void SatViewScenePass::render_hdr_debug_ui()
{
    if (state_->hdr_targets.empty())
        return;
    const auto& targets = state_->hdr_targets[
        state_->last_prepass_frame % static_cast<uint32_t>(state_->hdr_targets.size())];
    if (!ImGui::Begin("SatView HDR Buffers"))
    {
        ImGui::End();
        return;
    }
    const uint32_t samples = static_cast<uint32_t>(state_->scene_sample_count);
    ImGui::Text("Requested: 4x  Active: %ux%s", samples, samples < 4 ? " (fallback)" : "");
    ImGui::Text("Size: %dx%d  Exposure: %.2f  White point: %.2f",
        targets.width, targets.height, tone_map_exposure_, tone_map_white_point_);

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float aspect = targets.height > 0
        ? static_cast<float>(targets.width) / static_cast<float>(targets.height)
        : 1.0f;
    const float image_width = std::max(64.0f,
        (available.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f);
    const ImVec2 size(image_width, image_width / aspect);
    if (ImGui::BeginTable("##satview_hdr_grid", 2))
    {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("MSAA Difference");
        if (targets.debug_ready && targets.msaa_difference.get())
            ImGui::Image((__bridge void*)targets.msaa_difference.get(), size);
        else
            ImGui::TextDisabled("Waiting for debug frame");

        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Scene HDR Resolved");
        ImGui::Image((__bridge void*)targets.scene_hdr.get(), size);

        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Scene Final");
        ImGui::Image((__bridge void*)targets.scene_final_unorm.get(),
            size, ImVec2(0, 1), ImVec2(1, 0));
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace draxul::satview
