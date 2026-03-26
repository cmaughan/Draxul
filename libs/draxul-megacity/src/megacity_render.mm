#include "isometric_scene_pass.h"

#include "megacity_material_assets.h"
#include "mesh_library.h"
#include "objc_ref.h"
#import <Metal/Metal.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <draxul/log.h>
#include <imgui.h>
#import <simd/simd.h>
#include <vector>

namespace draxul
{

namespace
{

struct FrameUniforms
{
    simd_float4x4 view;
    simd_float4x4 proj;
    simd_float4x4 inv_view_proj;
    simd_float4 camera_pos;
    simd_float4 light_dir;
    simd_float4 point_light_pos;
    simd_float4 label_fade_px;
    simd_float4 render_tuning;
    simd_float4 screen_params; // x = viewport origin x, y = viewport origin y, z = 1 / viewport width, w = 1 / viewport height
    simd_float4 ao_params; // x = radius world, y = radius pixels, z = bias, w = power
    simd_float4 debug_view; // x = AO debug mode, y = AO denoise enabled
    simd_float4 world_debug_bounds; // x = min x, y = max x, z = min z, w = max z
};

struct ObjectUniforms
{
    simd_float4x4 world;
    simd_float4 color;
    simd_float4 material_info;
    simd_float4 uv_rect;
    simd_float4 label_metrics;
};

struct MeshBuffers
{
    ObjCRef<id<MTLBuffer>> vertex_buffer;
    ObjCRef<id<MTLBuffer>> index_buffer;
    NSUInteger index_count = 0;
};

struct BufferSlice
{
    id<MTLBuffer> buffer = nil;
    NSUInteger offset = 0;
    void* mapped = nullptr;
};

struct MeshSlice
{
    id<MTLBuffer> vertex_buffer = nil;
    NSUInteger vertex_offset = 0;
    id<MTLBuffer> index_buffer = nil;
    NSUInteger index_offset = 0;
    NSUInteger index_count = 0;
};

struct TransientBufferArena
{
    ObjCRef<id<MTLBuffer>> buffer;
    NSUInteger head = 0;

    void reset()
    {
        head = 0;
    }
};

struct TransientGeometryArena
{
    TransientBufferArena vertices;
    TransientBufferArena indices;

    void reset()
    {
        vertices.reset();
        indices.reset();
    }
};

struct FrameResources
{
    ObjCRef<id<MTLBuffer>> frame_uniforms;
    TransientGeometryArena geometry_arena;
};

bool same_grid_spec(const FloorGridSpec& a, const FloorGridSpec& b)
{
    return a.enabled == b.enabled
        && a.min_x == b.min_x
        && a.max_x == b.max_x
        && a.min_z == b.min_z
        && a.max_z == b.max_z
        && a.tile_size == b.tile_size
        && a.line_width == b.line_width
        && a.y == b.y
        && a.color.x == b.color.x
        && a.color.y == b.color.y
        && a.color.z == b.color.z
        && a.color.w == b.color.w;
}

simd_float4x4 to_simd_matrix(const glm::mat4& mat)
{
    simd_float4x4 out;
    for (int column = 0; column < 4; ++column)
    {
        out.columns[column] = simd_make_float4(
            mat[column][0],
            mat[column][1],
            mat[column][2],
            mat[column][3]);
    }
    return out;
}

float compute_ao_radius_pixels(const glm::mat4& proj, float radius_world, int viewport_h)
{
    if (viewport_h <= 0)
        return 1.0f;
    const float ortho_scale_y = std::abs(proj[1][1]);
    return std::max(1.0f, radius_world * ortho_scale_y * 0.5f * static_cast<float>(viewport_h));
}

bool upload_mesh(id<MTLDevice> device, const MeshData& mesh, MeshBuffers& buffers)
{
    id<MTLBuffer> vertex_buffer = [device newBufferWithBytes:mesh.vertices.data()
                                                      length:mesh.vertices.size() * sizeof(SceneVertex)
                                                     options:MTLResourceStorageModeShared];
    id<MTLBuffer> index_buffer = [device newBufferWithBytes:mesh.indices.data()
                                                     length:mesh.indices.size() * sizeof(uint16_t)
                                                    options:MTLResourceStorageModeShared];
    if (!vertex_buffer || !index_buffer)
        return false;

    buffers.vertex_buffer.reset(vertex_buffer);
    buffers.index_buffer.reset(index_buffer);
    buffers.index_count = static_cast<NSUInteger>(mesh.indices.size());
    return true;
}

NSUInteger align_up(NSUInteger value, NSUInteger alignment)
{
    if (alignment <= 1)
        return value;
    const NSUInteger remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

NSUInteger grow_capacity(NSUInteger current_size, NSUInteger required_size)
{
    if (current_size == 0)
        return required_size;
    return std::max(required_size, current_size * 2);
}

bool ensure_buffer_capacity(id<MTLDevice> device, NSUInteger required_size, ObjCRef<id<MTLBuffer>>& buffer)
{
    if (required_size == 0)
        return true;
    if (buffer && [buffer.get() length] >= required_size)
        return true;

    id<MTLBuffer> replacement = [device newBufferWithLength:grow_capacity(buffer ? [buffer.get() length] : 0, required_size)
                                                    options:MTLResourceStorageModeShared];
    if (!replacement)
        return false;

    buffer.reset(replacement);
    return true;
}

bool reserve_transient_buffer(id<MTLDevice> device, TransientBufferArena& arena,
    NSUInteger size, NSUInteger alignment, NSUInteger minimum_size, BufferSlice& slice)
{
    if (size == 0)
    {
        slice = {};
        return true;
    }

    const NSUInteger offset = align_up(arena.head, alignment);
    const NSUInteger required_size = offset + size;
    if (!ensure_buffer_capacity(device, std::max(required_size, minimum_size), arena.buffer))
        return false;

    slice.buffer = arena.buffer.get();
    slice.offset = offset;
    slice.mapped = static_cast<char*>([arena.buffer.get() contents]) + offset;
    arena.head = required_size;
    return true;
}

bool stream_transient_mesh(id<MTLDevice> device, const MeshData& mesh,
    TransientGeometryArena& arena, MeshSlice& slice)
{
    constexpr NSUInteger kMinimumVertexArenaBytes = 16 * 1024;
    constexpr NSUInteger kMinimumIndexArenaBytes = 4 * 1024;

    const NSUInteger vertex_bytes = static_cast<NSUInteger>(mesh.vertices.size() * sizeof(SceneVertex));
    const NSUInteger index_bytes = static_cast<NSUInteger>(mesh.indices.size() * sizeof(uint16_t));
    if (vertex_bytes == 0 || index_bytes == 0)
    {
        slice = {};
        return true;
    }

    BufferSlice vertex_slice;
    if (!reserve_transient_buffer(device, arena.vertices, vertex_bytes, alignof(SceneVertex),
            kMinimumVertexArenaBytes, vertex_slice))
    {
        return false;
    }

    BufferSlice index_slice;
    if (!reserve_transient_buffer(device, arena.indices, index_bytes, alignof(uint16_t),
            kMinimumIndexArenaBytes, index_slice))
    {
        return false;
    }

    std::memcpy(vertex_slice.mapped, mesh.vertices.data(), vertex_bytes);
    std::memcpy(index_slice.mapped, mesh.indices.data(), index_bytes);
    slice.vertex_buffer = vertex_slice.buffer;
    slice.vertex_offset = vertex_slice.offset;
    slice.index_buffer = index_slice.buffer;
    slice.index_offset = index_slice.offset;
    slice.index_count = static_cast<NSUInteger>(mesh.indices.size());
    return true;
}

} // namespace

struct GBufferTargets
{
    ObjCRef<id<MTLTexture>> material; // RGBA8Unorm — RG octahedral normal, B roughness, A specular
    ObjCRef<id<MTLTexture>> base_color; // RGBA8Unorm — RGB albedo, A metallic
    ObjCRef<id<MTLTexture>> ao_raw; // RGBA8Unorm — raw ambient occlusion before denoise
    ObjCRef<id<MTLTexture>> ao; // RGBA8Unorm — R ambient occlusion, GBA reserved
    ObjCRef<id<MTLTexture>> depth; // Depth32Float — hardware depth
    int width = 0;
    int height = 0;
};

struct IsometricScenePass::State
{
    ObjCRef<id<MTLRenderPipelineState>> pipeline;
    ObjCRef<id<MTLDepthStencilState>> depth_state;
    MeshBuffers cube_mesh;
    MeshBuffers floor_mesh;
    MeshBuffers road_surface_mesh;
    MeshBuffers roof_sign_mesh;
    MeshBuffers wall_sign_mesh;
    MeshData cached_grid_mesh;
    FloorGridSpec cached_grid_spec;
    bool has_cached_grid_mesh = false;
    ObjCRef<id<MTLTexture>> label_atlas_texture;
    ObjCRef<id<MTLSamplerState>> label_sampler;
    uint64_t label_atlas_revision = 0;
    std::vector<FrameResources> frame_resources;
    uint32_t buffered_frame_count = 1;
    bool initialized = false;

    // GBuffer pre-pass resources
    ObjCRef<id<MTLRenderPipelineState>> gbuffer_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> ao_pipeline;
    ObjCRef<id<MTLRenderPipelineState>> ao_blur_pipeline;
    ObjCRef<id<MTLSamplerState>> gbuffer_sampler;
    ObjCRef<id<MTLSamplerState>> gbuffer_point_sampler;
    ObjCRef<id<MTLSamplerState>> material_sampler;
    ObjCRef<id<MTLTexture>> road_albedo_texture;
    ObjCRef<id<MTLTexture>> road_normal_texture;
    ObjCRef<id<MTLTexture>> road_roughness_texture;
    ObjCRef<id<MTLTexture>> road_ao_texture;
    std::vector<GBufferTargets> gbuffer_targets; // per frame
    bool gbuffer_initialized = false;
    uint32_t last_prepass_frame = 0;

    bool init(id<MTLDevice> device, int grid_width, int grid_height, float tile_size)
    {
        if (initialized)
            return pipeline.get() != nil && depth_state.get() != nil
                && material_sampler.get() != nil && road_albedo_texture.get() != nil;

        initialized = true;
        if (!ensure_road_materials(device))
            return false;

        NSError* error = nil;
        NSString* exePath = [[NSBundle mainBundle] executablePath];
        NSString* exeDir = [exePath stringByDeletingLastPathComponent];
        NSString* libPath = [exeDir stringByAppendingPathComponent:@"shaders/megacity_scene.metallib"];
        NSURL* libURL = [NSURL fileURLWithPath:libPath];

        id<MTLLibrary> library = [device newLibraryWithURL:libURL error:&error];
        if (!library)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to load megacity_scene.metallib from %s: %s",
                [libPath UTF8String],
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }

        id<MTLFunction> vert_fn = [library newFunctionWithName:@"scene_vertex"];
        id<MTLFunction> frag_fn = [library newFunctionWithName:@"scene_fragment"];
        if (!vert_fn || !frag_fn)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: scene_vertex or scene_fragment not found in shader library");
            return false;
        }

        MTLVertexDescriptor* vertex_desc = [[MTLVertexDescriptor alloc] init];
        vertex_desc.attributes[0].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[0].offset = offsetof(SceneVertex, position);
        vertex_desc.attributes[0].bufferIndex = 0;
        vertex_desc.attributes[1].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[1].offset = offsetof(SceneVertex, normal);
        vertex_desc.attributes[1].bufferIndex = 0;
        vertex_desc.attributes[2].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[2].offset = offsetof(SceneVertex, color);
        vertex_desc.attributes[2].bufferIndex = 0;
        vertex_desc.attributes[3].format = MTLVertexFormatFloat2;
        vertex_desc.attributes[3].offset = offsetof(SceneVertex, uv);
        vertex_desc.attributes[3].bufferIndex = 0;
        vertex_desc.attributes[4].format = MTLVertexFormatFloat;
        vertex_desc.attributes[4].offset = offsetof(SceneVertex, tex_blend);
        vertex_desc.attributes[4].bufferIndex = 0;
        vertex_desc.layouts[0].stride = sizeof(SceneVertex);
        vertex_desc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vert_fn;
        desc.fragmentFunction = frag_fn;
        desc.vertexDescriptor = vertex_desc;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        id<MTLRenderPipelineState> pipeline_state = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pipeline_state)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to create scene pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        pipeline.reset(pipeline_state);

        MTLDepthStencilDescriptor* depth_desc = [[MTLDepthStencilDescriptor alloc] init];
        depth_desc.depthCompareFunction = MTLCompareFunctionLessEqual;
        depth_desc.depthWriteEnabled = YES;
        depth_state.reset([device newDepthStencilStateWithDescriptor:depth_desc]);
        if (!depth_state)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to create depth state");
            return false;
        }

        if (!upload_mesh(device, build_unit_cube_mesh(), cube_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload cube mesh");
            return false;
        }
        if (!upload_mesh(device, build_floor_box_mesh(), floor_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload floor mesh");
            return false;
        }
        if (!upload_mesh(device, build_road_surface_mesh(), road_surface_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload road surface mesh");
            return false;
        }
        if (!upload_mesh(device, build_roof_sign_mesh(), roof_sign_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload roof sign mesh");
            return false;
        }
        if (!upload_mesh(device, build_wall_sign_mesh(), wall_sign_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload wall sign mesh");
            return false;
        }

        MTLSamplerDescriptor* sampler_desc = [[MTLSamplerDescriptor alloc] init];
        sampler_desc.minFilter = MTLSamplerMinMagFilterLinear;
        sampler_desc.magFilter = MTLSamplerMinMagFilterLinear;
        sampler_desc.mipFilter = MTLSamplerMipFilterNotMipmapped;
        sampler_desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sampler_desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        sampler_desc.maxAnisotropy = 8;
        label_sampler.reset([device newSamplerStateWithDescriptor:sampler_desc]);
        if (!label_sampler)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to create label sampler");
            return false;
        }
        DRAXUL_LOG_INFO(LogCategory::App, "MegaCity: scene pipeline initialized");
        return true;
    }

    bool ensure_frame_resources(id<MTLDevice> device, uint32_t frame_count)
    {
        frame_count = std::max(1u, frame_count);
        if (buffered_frame_count == frame_count && frame_resources.size() == frame_count)
            return true;

        frame_resources.clear();
        frame_resources.resize(frame_count);
        buffered_frame_count = frame_count;

        for (auto& frame : frame_resources)
        {
            if (!ensure_buffer_capacity(device, sizeof(FrameUniforms), frame.frame_uniforms))
            {
                DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to allocate frame uniform buffer");
                frame_resources.clear();
                buffered_frame_count = 1;
                return false;
            }
        }
        return true;
    }

    bool ensure_floor_grid(id<MTLDevice> device, const FloorGridSpec& spec)
    {
        (void)device;
        if (!spec.enabled)
        {
            cached_grid_mesh = {};
            cached_grid_spec = {};
            has_cached_grid_mesh = false;
            return true;
        }
        if (has_cached_grid_mesh && same_grid_spec(cached_grid_spec, spec))
            return true;

        cached_grid_mesh = build_outline_grid_mesh(spec);
        cached_grid_spec = spec;
        has_cached_grid_mesh = true;
        return true;
    }

    bool ensure_label_atlas(id<MTLDevice> device, const std::shared_ptr<const LabelAtlasData>& atlas)
    {
        if (!atlas || !atlas->valid())
        {
            if (label_atlas_texture)
                return true;

            MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                            width:1
                                                                                           height:1
                                                                                        mipmapped:NO];
            desc.usage = MTLTextureUsageShaderRead;
            id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
            if (!texture)
                return false;
            label_atlas_texture.reset(texture);
            const uint8_t clear[4] = { 0, 0, 0, 0 };
            [label_atlas_texture.get() replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                                         mipmapLevel:0
                                           withBytes:clear
                                         bytesPerRow:4];
            label_atlas_revision = 0;
            return true;
        }

        if (label_atlas_revision == atlas->revision && label_atlas_texture)
            return true;

        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                        width:static_cast<NSUInteger>(atlas->width)
                                                                                       height:static_cast<NSUInteger>(atlas->height)
                                                                                    mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
        if (!texture)
            return false;

        label_atlas_texture.reset(texture);
        [label_atlas_texture.get() replaceRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(atlas->width),
                                                     static_cast<NSUInteger>(atlas->height))
                                     mipmapLevel:0
                                       withBytes:atlas->rgba.data()
                                     bytesPerRow:static_cast<NSUInteger>(atlas->width * 4)];
        label_atlas_revision = atlas->revision;
        return true;
    }

    id<MTLTexture> make_texture(id<MTLDevice> device, MTLPixelFormat format, const LoadedTextureImage& image)
    {
        MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                                        width:static_cast<NSUInteger>(image.width)
                                                                                       height:static_cast<NSUInteger>(image.height)
                                                                                    mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [device newTextureWithDescriptor:desc];
        if (!texture)
            return nil;

        [texture replaceRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(image.width), static_cast<NSUInteger>(image.height))
                   mipmapLevel:0
                     withBytes:image.rgba.data()
                   bytesPerRow:static_cast<NSUInteger>(image.width * 4)];
        return texture;
    }

    bool ensure_road_materials(id<MTLDevice> device)
    {
        if (road_albedo_texture && road_normal_texture && road_roughness_texture && road_ao_texture && material_sampler)
            return true;

        const AsphaltRoadMaterialImages images = load_asphalt_road_material_images();
        if (!images.valid())
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to load asphalt road material images");
            return false;
        }

        road_albedo_texture.reset(make_texture(device, MTLPixelFormatRGBA8Unorm_sRGB, images.albedo));
        road_normal_texture.reset(make_texture(device, MTLPixelFormatRGBA8Unorm, images.normal));
        road_roughness_texture.reset(make_texture(device, MTLPixelFormatRGBA8Unorm, images.roughness));
        road_ao_texture.reset(make_texture(device, MTLPixelFormatRGBA8Unorm, images.ao));
        if (!road_albedo_texture || !road_normal_texture || !road_roughness_texture || !road_ao_texture)
            return false;

        MTLSamplerDescriptor* material_sampler_desc = [[MTLSamplerDescriptor alloc] init];
        material_sampler_desc.minFilter = MTLSamplerMinMagFilterLinear;
        material_sampler_desc.magFilter = MTLSamplerMinMagFilterLinear;
        material_sampler_desc.mipFilter = MTLSamplerMipFilterNotMipmapped;
        material_sampler_desc.sAddressMode = MTLSamplerAddressModeRepeat;
        material_sampler_desc.tAddressMode = MTLSamplerAddressModeRepeat;
        material_sampler_desc.rAddressMode = MTLSamplerAddressModeRepeat;
        material_sampler_desc.maxAnisotropy = 8;
        material_sampler.reset([device newSamplerStateWithDescriptor:material_sampler_desc]);
        return material_sampler.get() != nil;
    }

    bool init_gbuffer(id<MTLDevice> device)
    {
        if (gbuffer_initialized)
            return gbuffer_pipeline.get() != nil && ao_pipeline.get() != nil && ao_blur_pipeline.get() != nil
                && gbuffer_sampler.get() != nil && gbuffer_point_sampler.get() != nil
                && material_sampler.get() != nil && road_albedo_texture.get() != nil;

        gbuffer_initialized = true;
        if (!ensure_road_materials(device))
            return false;

        NSError* error = nil;
        NSString* exePath = [[NSBundle mainBundle] executablePath];
        NSString* exeDir = [exePath stringByDeletingLastPathComponent];
        NSString* libPath = [exeDir stringByAppendingPathComponent:@"shaders/megacity_gbuffer.metallib"];
        NSURL* libURL = [NSURL fileURLWithPath:libPath];

        id<MTLLibrary> library = [device newLibraryWithURL:libURL error:&error];
        if (!library)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to load megacity_gbuffer.metallib: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }

        id<MTLFunction> vert_fn = [library newFunctionWithName:@"gbuffer_vertex"];
        id<MTLFunction> frag_fn = [library newFunctionWithName:@"gbuffer_fragment"];
        if (!vert_fn || !frag_fn)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: gbuffer_vertex or gbuffer_fragment not found");
            return false;
        }

        MTLVertexDescriptor* vertex_desc = [[MTLVertexDescriptor alloc] init];
        vertex_desc.attributes[0].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[0].offset = offsetof(SceneVertex, position);
        vertex_desc.attributes[0].bufferIndex = 0;
        vertex_desc.attributes[1].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[1].offset = offsetof(SceneVertex, normal);
        vertex_desc.attributes[1].bufferIndex = 0;
        vertex_desc.attributes[2].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[2].offset = offsetof(SceneVertex, color);
        vertex_desc.attributes[2].bufferIndex = 0;
        vertex_desc.attributes[3].format = MTLVertexFormatFloat2;
        vertex_desc.attributes[3].offset = offsetof(SceneVertex, uv);
        vertex_desc.attributes[3].bufferIndex = 0;
        vertex_desc.attributes[4].format = MTLVertexFormatFloat;
        vertex_desc.attributes[4].offset = offsetof(SceneVertex, tex_blend);
        vertex_desc.attributes[4].bufferIndex = 0;
        vertex_desc.layouts[0].stride = sizeof(SceneVertex);
        vertex_desc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vert_fn;
        desc.fragmentFunction = frag_fn;
        desc.vertexDescriptor = vertex_desc;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
        desc.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA8Unorm;
        desc.colorAttachments[2].pixelFormat = MTLPixelFormatRGBA8Unorm;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pso)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to create GBuffer pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        gbuffer_pipeline.reset(pso);

        NSString* aoLibPath = [exeDir stringByAppendingPathComponent:@"shaders/megacity_ao.metallib"];
        NSURL* aoLibURL = [NSURL fileURLWithPath:aoLibPath];
        id<MTLLibrary> aoLibrary = [device newLibraryWithURL:aoLibURL error:&error];
        if (!aoLibrary)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to load megacity_ao.metallib: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }

        id<MTLFunction> ao_vert_fn = [aoLibrary newFunctionWithName:@"ao_vertex"];
        id<MTLFunction> ao_frag_fn = [aoLibrary newFunctionWithName:@"ao_fragment"];
        id<MTLFunction> ao_blur_frag_fn = [aoLibrary newFunctionWithName:@"ao_blur_fragment"];
        if (!ao_vert_fn || !ao_frag_fn || !ao_blur_frag_fn)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: AO shader functions not found");
            return false;
        }

        MTLRenderPipelineDescriptor* ao_desc = [[MTLRenderPipelineDescriptor alloc] init];
        ao_desc.vertexFunction = ao_vert_fn;
        ao_desc.fragmentFunction = ao_frag_fn;
        ao_desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;

        id<MTLRenderPipelineState> ao_pso = [device newRenderPipelineStateWithDescriptor:ao_desc error:&error];
        if (!ao_pso)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to create AO pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        ao_pipeline.reset(ao_pso);

        MTLRenderPipelineDescriptor* ao_blur_desc = [[MTLRenderPipelineDescriptor alloc] init];
        ao_blur_desc.vertexFunction = ao_vert_fn;
        ao_blur_desc.fragmentFunction = ao_blur_frag_fn;
        ao_blur_desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;

        id<MTLRenderPipelineState> ao_blur_pso = [device newRenderPipelineStateWithDescriptor:ao_blur_desc error:&error];
        if (!ao_blur_pso)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to create AO blur pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        ao_blur_pipeline.reset(ao_blur_pso);

        MTLSamplerDescriptor* gbuffer_sampler_desc = [[MTLSamplerDescriptor alloc] init];
        gbuffer_sampler_desc.minFilter = MTLSamplerMinMagFilterLinear;
        gbuffer_sampler_desc.magFilter = MTLSamplerMinMagFilterLinear;
        gbuffer_sampler_desc.mipFilter = MTLSamplerMipFilterNotMipmapped;
        gbuffer_sampler_desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        gbuffer_sampler_desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        gbuffer_sampler_desc.rAddressMode = MTLSamplerAddressModeClampToEdge;
        gbuffer_sampler.reset([device newSamplerStateWithDescriptor:gbuffer_sampler_desc]);
        if (!gbuffer_sampler)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to create GBuffer sampler");
            return false;
        }

        MTLSamplerDescriptor* gbuffer_point_sampler_desc = [[MTLSamplerDescriptor alloc] init];
        gbuffer_point_sampler_desc.minFilter = MTLSamplerMinMagFilterNearest;
        gbuffer_point_sampler_desc.magFilter = MTLSamplerMinMagFilterNearest;
        gbuffer_point_sampler_desc.mipFilter = MTLSamplerMipFilterNotMipmapped;
        gbuffer_point_sampler_desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        gbuffer_point_sampler_desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        gbuffer_point_sampler_desc.rAddressMode = MTLSamplerAddressModeClampToEdge;
        gbuffer_point_sampler.reset([device newSamplerStateWithDescriptor:gbuffer_point_sampler_desc]);
        if (!gbuffer_point_sampler)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to create point-sampled GBuffer sampler");
            return false;
        }
        DRAXUL_LOG_INFO(LogCategory::App, "MegaCity: GBuffer pipeline initialized");
        return true;
    }

    bool ensure_gbuffer_targets(id<MTLDevice> device, uint32_t frame_count, int width, int height)
    {
        if (width <= 0 || height <= 0)
            return false;

        frame_count = std::max(1u, frame_count);
        if (gbuffer_targets.size() == frame_count
            && !gbuffer_targets.empty()
            && gbuffer_targets[0].width == width
            && gbuffer_targets[0].height == height)
            return true;

        gbuffer_targets.clear();
        gbuffer_targets.resize(frame_count);

        for (auto& targets : gbuffer_targets)
        {
            MTLTextureDescriptor* rgba8_desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                             width:static_cast<NSUInteger>(width)
                                            height:static_cast<NSUInteger>(height)
                                         mipmapped:NO];
            rgba8_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            rgba8_desc.storageMode = MTLStorageModePrivate;

            MTLTextureDescriptor* depth_desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                             width:static_cast<NSUInteger>(width)
                                            height:static_cast<NSUInteger>(height)
                                         mipmapped:NO];
            depth_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            depth_desc.storageMode = MTLStorageModePrivate;

            targets.material.reset([device newTextureWithDescriptor:rgba8_desc]);
            targets.base_color.reset([device newTextureWithDescriptor:rgba8_desc]);
            targets.ao_raw.reset([device newTextureWithDescriptor:rgba8_desc]);
            targets.ao.reset([device newTextureWithDescriptor:rgba8_desc]);
            targets.depth.reset([device newTextureWithDescriptor:depth_desc]);
            targets.width = width;
            targets.height = height;

            if (!targets.material || !targets.base_color || !targets.ao_raw || !targets.ao || !targets.depth)
            {
                DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to create GBuffer textures");
                gbuffer_targets.clear();
                return false;
            }
        }
        return true;
    }
};

IsometricScenePass::IsometricScenePass(int grid_width, int grid_height, float tile_size)
    : grid_width_(grid_width)
    , grid_height_(grid_height)
    , tile_size_(tile_size)
    , state_(std::make_unique<State>())
{
}

IsometricScenePass::~IsometricScenePass() = default;

void IsometricScenePass::record_prepass(IRenderContext& ctx)
{
    id<MTLCommandBuffer> cmd_buf = (__bridge id<MTLCommandBuffer>)ctx.native_command_buffer();
    if (!cmd_buf)
        return;

    const int vw = ctx.viewport_w();
    const int vh = ctx.viewport_h();
    if (vw <= 0 || vh <= 0)
        return;

    const uint32_t frame_count = std::max(1u, ctx.buffered_frame_count());
    if (!state_->init(cmd_buf.device, grid_width_, grid_height_, tile_size_))
        return;
    if (!state_->init_gbuffer(cmd_buf.device))
        return;
    if (!state_->ensure_frame_resources(cmd_buf.device, frame_count))
        return;
    if (!state_->ensure_gbuffer_targets(cmd_buf.device, frame_count, vw, vh))
        return;
    if (!state_->ensure_floor_grid(cmd_buf.device, scene_.floor_grid))
        return;

    const uint32_t frame_index = ctx.frame_index() % static_cast<uint32_t>(state_->frame_resources.size());
    state_->last_prepass_frame = frame_index;
    auto& frame_resources = state_->frame_resources[frame_index];
    frame_resources.geometry_arena.reset();

    MeshSlice grid_slice;
    if (state_->has_cached_grid_mesh
        && !stream_transient_mesh(cmd_buf.device, state_->cached_grid_mesh, frame_resources.geometry_arena, grid_slice))
    {
        return;
    }

    auto& gbuffer = state_->gbuffer_targets[frame_index];

    // Build GBuffer render pass descriptor
    MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    rpDesc.colorAttachments[0].texture = gbuffer.material.get();
    rpDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.5, 0.5, 0.0, 0.0);
    rpDesc.colorAttachments[1].texture = gbuffer.base_color.get();
    rpDesc.colorAttachments[1].loadAction = MTLLoadActionClear;
    rpDesc.colorAttachments[1].storeAction = MTLStoreActionStore;
    rpDesc.colorAttachments[1].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    rpDesc.colorAttachments[2].texture = gbuffer.ao.get();
    rpDesc.colorAttachments[2].loadAction = MTLLoadActionClear;
    rpDesc.colorAttachments[2].storeAction = MTLStoreActionStore;
    rpDesc.colorAttachments[2].clearColor = MTLClearColorMake(1.0, 0.0, 0.0, 1.0);
    rpDesc.depthAttachment.texture = gbuffer.depth.get();
    rpDesc.depthAttachment.loadAction = MTLLoadActionClear;
    rpDesc.depthAttachment.storeAction = MTLStoreActionStore;
    rpDesc.depthAttachment.clearDepth = 1.0;

    id<MTLRenderCommandEncoder> encoder = [cmd_buf renderCommandEncoderWithDescriptor:rpDesc];
    if (!encoder)
        return;

    MTLViewport viewport;
    viewport.originX = 0;
    viewport.originY = 0;
    viewport.width = vw;
    viewport.height = vh;
    viewport.znear = 0.0;
    viewport.zfar = 1.0;
    [encoder setViewport:viewport];

    MTLScissorRect scissor;
    scissor.x = 0;
    scissor.y = 0;
    scissor.width = static_cast<NSUInteger>(vw);
    scissor.height = static_cast<NSUInteger>(vh);
    [encoder setScissorRect:scissor];

    // Upload frame uniforms (same as main pass)
    FrameUniforms frame;
    frame.view = to_simd_matrix(scene_.camera.view);
    frame.proj = to_simd_matrix(scene_.camera.proj);
    frame.inv_view_proj = to_simd_matrix(scene_.camera.inv_view_proj);
    frame.camera_pos = simd_make_float4(
        scene_.camera.camera_pos.x, scene_.camera.camera_pos.y,
        scene_.camera.camera_pos.z, scene_.camera.camera_pos.w);
    frame.light_dir = simd_make_float4(
        scene_.camera.light_dir.x, scene_.camera.light_dir.y,
        scene_.camera.light_dir.z, scene_.camera.light_dir.w);
    frame.point_light_pos = simd_make_float4(
        scene_.camera.point_light_pos.x, scene_.camera.point_light_pos.y,
        scene_.camera.point_light_pos.z, scene_.camera.point_light_pos.w);
    frame.label_fade_px = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);
    frame.render_tuning = simd_make_float4(1.0f, 0.0f, 0.0f, 0.0f);
    frame.screen_params = simd_make_float4(0.0f, 0.0f, 1.0f / std::max(vw, 1), 1.0f / std::max(vh, 1));
    frame.ao_params = simd_make_float4(
        scene_.camera.ao_settings.x,
        compute_ao_radius_pixels(scene_.camera.proj, scene_.camera.ao_settings.x, vh),
        scene_.camera.ao_settings.y,
        scene_.camera.ao_settings.z);
    frame.debug_view = simd_make_float4(0.0f, 1.0f, 0.0f, 0.0f);
    frame.world_debug_bounds = simd_make_float4(-5.0f, 5.0f, -5.0f, 5.0f);
    std::memcpy([frame_resources.frame_uniforms.get() contents], &frame, sizeof(frame));

    [encoder setRenderPipelineState:state_->gbuffer_pipeline.get()];
    [encoder setDepthStencilState:state_->depth_state.get()];
    [encoder setCullMode:MTLCullModeBack];
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    [encoder setVertexBuffer:frame_resources.frame_uniforms.get() offset:0 atIndex:1];
    [encoder setFragmentTexture:state_->road_albedo_texture.get() atIndex:0];
    [encoder setFragmentTexture:state_->road_normal_texture.get() atIndex:1];
    [encoder setFragmentTexture:state_->road_roughness_texture.get() atIndex:2];
    [encoder setFragmentTexture:state_->road_ao_texture.get() atIndex:3];
    [encoder setFragmentSamplerState:state_->material_sampler.get() atIndex:0];

    // Draw scene objects
    for (const SceneObject& obj : scene_.objects)
    {
        const MeshBuffers* mesh = nullptr;
        switch (obj.mesh)
        {
        case MeshId::Floor:
            mesh = &state_->floor_mesh;
            break;
        case MeshId::Cube:
            mesh = &state_->cube_mesh;
            break;
        case MeshId::RoadSurface:
            mesh = &state_->road_surface_mesh;
            break;
        case MeshId::RoofSign:
            mesh = &state_->roof_sign_mesh;
            break;
        case MeshId::WallSign:
            mesh = &state_->wall_sign_mesh;
            break;
        case MeshId::Grid:
            continue;
        }
        if (!mesh || mesh->index_count == 0)
            continue;

        ObjectUniforms object;
        object.world = to_simd_matrix(obj.world);
        object.color = simd_make_float4(obj.color.x, obj.color.y, obj.color.z, obj.color.w);
        object.material_info = simd_make_float4(
            obj.material_info.x, obj.material_info.y, obj.material_info.z, obj.material_info.w);
        object.uv_rect = simd_make_float4(0.0f, 0.0f, 1.0f, 1.0f);
        object.label_metrics = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);

        [encoder setVertexBuffer:mesh->vertex_buffer.get() offset:0 atIndex:0];
        [encoder setVertexBytes:&object length:sizeof(object) atIndex:2];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:mesh->index_count
                             indexType:MTLIndexTypeUInt16
                           indexBuffer:mesh->index_buffer.get()
                     indexBufferOffset:0];
    }

    // Draw floor grid
    if (grid_slice.index_count > 0)
    {
        ObjectUniforms object;
        object.world = matrix_identity_float4x4;
        object.color = simd_make_float4(
            scene_.floor_grid.color.x, scene_.floor_grid.color.y,
            scene_.floor_grid.color.z, scene_.floor_grid.color.w);
        object.material_info = simd_make_float4(0.0f, 1.0f, 1.0f, 1.0f);
        object.uv_rect = simd_make_float4(0.0f, 0.0f, 1.0f, 1.0f);
        object.label_metrics = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);

        [encoder setVertexBuffer:grid_slice.vertex_buffer offset:grid_slice.vertex_offset atIndex:0];
        [encoder setVertexBytes:&object length:sizeof(object) atIndex:2];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:grid_slice.index_count
                             indexType:MTLIndexTypeUInt16
                           indexBuffer:grid_slice.index_buffer
                     indexBufferOffset:grid_slice.index_offset];
    }

    [encoder endEncoding];

    MTLRenderPassDescriptor* aoDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    aoDesc.colorAttachments[0].texture = gbuffer.ao_raw.get();
    aoDesc.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    aoDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> aoEncoder = [cmd_buf renderCommandEncoderWithDescriptor:aoDesc];
    if (!aoEncoder)
        return;

    [aoEncoder setViewport:viewport];
    [aoEncoder setScissorRect:scissor];
    [aoEncoder setRenderPipelineState:state_->ao_pipeline.get()];
    [aoEncoder setCullMode:MTLCullModeNone];
    [aoEncoder setFragmentBuffer:frame_resources.frame_uniforms.get() offset:0 atIndex:0];
    [aoEncoder setFragmentTexture:gbuffer.material.get() atIndex:0];
    [aoEncoder setFragmentTexture:gbuffer.depth.get() atIndex:1];
    [aoEncoder setFragmentSamplerState:state_->gbuffer_point_sampler.get() atIndex:0];
    [aoEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [aoEncoder endEncoding];

    MTLRenderPassDescriptor* blurDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    blurDesc.colorAttachments[0].texture = gbuffer.ao.get();
    blurDesc.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    blurDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> blurEncoder = [cmd_buf renderCommandEncoderWithDescriptor:blurDesc];
    if (!blurEncoder)
        return;

    [blurEncoder setViewport:viewport];
    [blurEncoder setScissorRect:scissor];
    [blurEncoder setRenderPipelineState:state_->ao_blur_pipeline.get()];
    [blurEncoder setCullMode:MTLCullModeNone];
    [blurEncoder setFragmentBuffer:frame_resources.frame_uniforms.get() offset:0 atIndex:0];
    [blurEncoder setFragmentTexture:gbuffer.ao_raw.get() atIndex:0];
    [blurEncoder setFragmentTexture:gbuffer.material.get() atIndex:1];
    [blurEncoder setFragmentTexture:gbuffer.depth.get() atIndex:2];
    [blurEncoder setFragmentSamplerState:state_->gbuffer_point_sampler.get() atIndex:0];
    [blurEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [blurEncoder endEncoding];
}

void IsometricScenePass::record(IRenderContext& ctx)
{
    id<MTLCommandBuffer> cmd_buf = (__bridge id<MTLCommandBuffer>)ctx.native_command_buffer();
    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)ctx.native_render_encoder();
    if (!cmd_buf || !encoder)
        return;

    const uint32_t frame_count = std::max(1u, ctx.buffered_frame_count());
    if (!state_->init(cmd_buf.device, grid_width_, grid_height_, tile_size_))
        return;
    if (!state_->init_gbuffer(cmd_buf.device))
        return;
    if (!state_->ensure_frame_resources(cmd_buf.device, frame_count))
        return;
    if (!state_->ensure_gbuffer_targets(cmd_buf.device, frame_count, ctx.viewport_w(), ctx.viewport_h()))
        return;
    if (!state_->ensure_floor_grid(cmd_buf.device, scene_.floor_grid))
        return;
    if (!state_->ensure_label_atlas(cmd_buf.device, scene_.label_atlas))
        return;

    const uint32_t frame_index = ctx.frame_index() % static_cast<uint32_t>(state_->frame_resources.size());
    auto& frame_resources = state_->frame_resources[frame_index];
    frame_resources.geometry_arena.reset();

    MeshSlice grid_slice;
    if (state_->has_cached_grid_mesh
        && !stream_transient_mesh(cmd_buf.device, state_->cached_grid_mesh, frame_resources.geometry_arena, grid_slice))
    {
        return;
    }

    MTLViewport viewport;
    viewport.originX = ctx.viewport_x();
    viewport.originY = ctx.viewport_y();
    viewport.width = ctx.viewport_w();
    viewport.height = ctx.viewport_h();
    viewport.znear = 0.0;
    viewport.zfar = 1.0;
    [encoder setViewport:viewport];

    MTLScissorRect scissor;
    scissor.x = static_cast<NSUInteger>(std::max(0, ctx.viewport_x()));
    scissor.y = static_cast<NSUInteger>(std::max(0, ctx.viewport_y()));
    scissor.width = static_cast<NSUInteger>(std::max(0, ctx.viewport_w()));
    scissor.height = static_cast<NSUInteger>(std::max(0, ctx.viewport_h()));
    [encoder setScissorRect:scissor];

    FrameUniforms frame;
    frame.view = to_simd_matrix(scene_.camera.view);
    frame.proj = to_simd_matrix(scene_.camera.proj);
    frame.inv_view_proj = to_simd_matrix(scene_.camera.inv_view_proj);
    frame.camera_pos = simd_make_float4(
        scene_.camera.camera_pos.x,
        scene_.camera.camera_pos.y,
        scene_.camera.camera_pos.z,
        scene_.camera.camera_pos.w);
    frame.light_dir = simd_make_float4(
        scene_.camera.light_dir.x,
        scene_.camera.light_dir.y,
        scene_.camera.light_dir.z,
        scene_.camera.light_dir.w);
    frame.point_light_pos = simd_make_float4(
        scene_.camera.point_light_pos.x,
        scene_.camera.point_light_pos.y,
        scene_.camera.point_light_pos.z,
        scene_.camera.point_light_pos.w);
    frame.label_fade_px = simd_make_float4(
        scene_.camera.label_fade_px.x,
        scene_.camera.label_fade_px.y,
        scene_.camera.label_fade_px.z,
        scene_.camera.label_fade_px.w);
    frame.render_tuning = simd_make_float4(
        scene_.camera.render_tuning.x,
        scene_.camera.render_tuning.y,
        scene_.camera.render_tuning.z,
        scene_.camera.render_tuning.w);
    frame.screen_params = simd_make_float4(
        static_cast<float>(ctx.viewport_x()),
        static_cast<float>(ctx.viewport_y()),
        1.0f / std::max(ctx.viewport_w(), 1),
        1.0f / std::max(ctx.viewport_h(), 1));
    frame.ao_params = simd_make_float4(
        scene_.camera.ao_settings.x,
        compute_ao_radius_pixels(scene_.camera.proj, scene_.camera.ao_settings.x, ctx.viewport_h()),
        scene_.camera.ao_settings.y,
        scene_.camera.ao_settings.z);
    frame.debug_view = simd_make_float4(
        scene_.camera.debug_view.x,
        scene_.camera.debug_view.y,
        scene_.camera.debug_view.z,
        scene_.camera.debug_view.w);
    frame.world_debug_bounds = simd_make_float4(
        scene_.camera.world_debug_bounds.x,
        scene_.camera.world_debug_bounds.y,
        scene_.camera.world_debug_bounds.z,
        scene_.camera.world_debug_bounds.w);
    std::memcpy([frame_resources.frame_uniforms.get() contents], &frame, sizeof(frame));

    [encoder setRenderPipelineState:state_->pipeline.get()];
    [encoder setDepthStencilState:state_->depth_state.get()];
    [encoder setCullMode:MTLCullModeBack];
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    [encoder setVertexBuffer:frame_resources.frame_uniforms.get() offset:0 atIndex:1];
    [encoder setFragmentBuffer:frame_resources.frame_uniforms.get() offset:0 atIndex:1];
    [encoder setFragmentTexture:state_->label_atlas_texture.get() atIndex:0];
    [encoder setFragmentTexture:state_->gbuffer_targets[frame_index].ao.get() atIndex:1];
    [encoder setFragmentTexture:state_->road_albedo_texture.get() atIndex:2];
    [encoder setFragmentTexture:state_->road_normal_texture.get() atIndex:3];
    [encoder setFragmentTexture:state_->road_roughness_texture.get() atIndex:4];
    [encoder setFragmentTexture:state_->road_ao_texture.get() atIndex:5];
    [encoder setFragmentSamplerState:state_->label_sampler.get() atIndex:0];
    [encoder setFragmentSamplerState:state_->gbuffer_sampler.get() atIndex:1];
    [encoder setFragmentSamplerState:state_->material_sampler.get() atIndex:2];

    for (const SceneObject& obj : scene_.objects)
    {
        const MeshBuffers* mesh = nullptr;
        switch (obj.mesh)
        {
        case MeshId::Floor:
            mesh = &state_->floor_mesh;
            break;
        case MeshId::Cube:
            mesh = &state_->cube_mesh;
            break;
        case MeshId::RoadSurface:
            mesh = &state_->road_surface_mesh;
            break;
        case MeshId::RoofSign:
            mesh = &state_->roof_sign_mesh;
            break;
        case MeshId::WallSign:
            mesh = &state_->wall_sign_mesh;
            break;
        case MeshId::Grid:
            continue;
        }
        if (!mesh || mesh->index_count == 0)
            continue;

        ObjectUniforms object;
        object.world = to_simd_matrix(obj.world);
        object.color = simd_make_float4(obj.color.x, obj.color.y, obj.color.z, obj.color.w);
        object.material_info = simd_make_float4(
            obj.material_info.x, obj.material_info.y, obj.material_info.z, obj.material_info.w);
        object.uv_rect = simd_make_float4(obj.uv_rect.x, obj.uv_rect.y, obj.uv_rect.z, obj.uv_rect.w);
        object.label_metrics = simd_make_float4(
            obj.label_ink_pixel_size.x,
            obj.label_ink_pixel_size.y,
            0.0f,
            0.0f);

        [encoder setVertexBuffer:mesh->vertex_buffer.get() offset:0 atIndex:0];
        [encoder setVertexBytes:&object length:sizeof(object) atIndex:2];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:mesh->index_count
                             indexType:MTLIndexTypeUInt16
                           indexBuffer:mesh->index_buffer.get()
                     indexBufferOffset:0];
    }

    if (grid_slice.index_count > 0)
    {
        ObjectUniforms object;
        object.world = matrix_identity_float4x4;
        object.color = simd_make_float4(
            scene_.floor_grid.color.x,
            scene_.floor_grid.color.y,
            scene_.floor_grid.color.z,
            scene_.floor_grid.color.w);
        object.material_info = simd_make_float4(0.0f, 1.0f, 1.0f, 1.0f);
        object.uv_rect = simd_make_float4(0.0f, 0.0f, 1.0f, 1.0f);
        object.label_metrics = simd_make_float4(0.0f, 0.0f, 0.0f, 0.0f);

        [encoder setVertexBuffer:grid_slice.vertex_buffer offset:grid_slice.vertex_offset atIndex:0];
        [encoder setVertexBytes:&object length:sizeof(object) atIndex:2];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:grid_slice.index_count
                             indexType:MTLIndexTypeUInt16
                           indexBuffer:grid_slice.index_buffer
                     indexBufferOffset:grid_slice.index_offset];
    }
}

void IsometricScenePass::render_gbuffer_debug_ui()
{
    if (state_->gbuffer_targets.empty())
        return;

    const uint32_t fi = state_->last_prepass_frame % static_cast<uint32_t>(state_->gbuffer_targets.size());
    auto& t = state_->gbuffer_targets[fi];
    if (!t.material || !t.base_color || !t.ao || !t.depth)
        return;

    if (!ImGui::Begin("GBuffer Debug"))
    {
        ImGui::End();
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float aspect = t.height > 0 ? static_cast<float>(t.width) / static_cast<float>(t.height) : 1.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float text_h = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const float cell_w = std::max(64.0f, (avail.x - spacing) * 0.5f);
    const float cell_h = std::max(32.0f, (avail.y - text_h * 3.0f) * 0.5f);
    const float img_w = std::min(cell_w, cell_h * aspect);
    const float img_h = img_w / aspect;
    const ImVec2 size(img_w, img_h);

    if (ImGui::BeginTable("##gbuffer_grid", 2))
    {
        ImGui::TableNextColumn();
        ImGui::Text("Norm/Rough/Spec");
        ImGui::Image((__bridge void*)t.material.get(), size);

        ImGui::TableNextColumn();
        ImGui::Text("RGB/Metallic");
        ImGui::Image((__bridge void*)t.base_color.get(), size);

        ImGui::TableNextColumn();
        ImGui::Text("Depth");
        ImGui::Image((__bridge void*)t.depth.get(), size);

        ImGui::TableNextColumn();
        ImGui::Text("Ambient Occlusion");
        ImGui::Image((__bridge void*)t.ao.get(), size);

        ImGui::EndTable();
    }

    ImGui::Text("Size: %dx%d", t.width, t.height);
    ImGui::End();
}

} // namespace draxul
