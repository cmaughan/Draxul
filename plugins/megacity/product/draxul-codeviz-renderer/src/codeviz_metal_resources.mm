#include "codeviz_metal_resources.h"

#include <algorithm>
#include <cstring>
#include <draxul/perf_timing.h>

namespace draxul::codeviz_metal
{
namespace
{

NSUInteger align_up(NSUInteger value, NSUInteger alignment)
{
    if (alignment <= 1)
        return value;
    const NSUInteger remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

NSUInteger grow_capacity(NSUInteger current_size, NSUInteger required_size)
{
    return current_size == 0
        ? required_size
        : std::max(required_size, current_size * 2);
}

bool reserve_transient_buffer(id<MTLDevice> device, TransientBufferArena& arena,
    NSUInteger size, NSUInteger alignment, NSUInteger minimum_size, BufferSlice& slice)
{
    PERF_MEASURE();
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

} // namespace

bool upload_mesh(id<MTLDevice> device, const MeshData& mesh, MeshBuffers& buffers)
{
    PERF_MEASURE();
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

bool ensure_buffer_capacity(id<MTLDevice> device, NSUInteger required_size,
    ObjCRef<id<MTLBuffer>>& buffer)
{
    PERF_MEASURE();
    if (required_size == 0)
        return true;
    if (buffer && [buffer.get() length] >= required_size)
        return true;

    id<MTLBuffer> replacement = [device
        newBufferWithLength:grow_capacity(buffer ? [buffer.get() length] : 0, required_size)
        options:MTLResourceStorageModeShared];
    if (!replacement)
        return false;
    buffer.reset(replacement);
    return true;
}

bool upload_performance_heat_values(id<MTLDevice> device,
    const std::vector<float>& values, ObjCRef<id<MTLBuffer>>& buffer)
{
    PERF_MEASURE();
    const NSUInteger byte_count = static_cast<NSUInteger>(
        std::max<size_t>(values.size(), 1u) * sizeof(float));
    if (!ensure_buffer_capacity(device, byte_count, buffer))
        return false;

    float* dst = static_cast<float*>([buffer.get() contents]);
    if (values.empty())
        dst[0] = 0.0f;
    else
        std::memcpy(dst, values.data(), values.size() * sizeof(float));
    return true;
}

bool stream_transient_mesh(id<MTLDevice> device, const MeshData& mesh,
    TransientGeometryArena& arena, MeshSlice& slice)
{
    PERF_MEASURE();
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
        return false;
    BufferSlice index_slice;
    if (!reserve_transient_buffer(device, arena.indices, index_bytes, alignof(uint16_t),
            kMinimumIndexArenaBytes, index_slice))
        return false;

    std::memcpy(vertex_slice.mapped, mesh.vertices.data(), vertex_bytes);
    std::memcpy(index_slice.mapped, mesh.indices.data(), index_bytes);
    slice.vertex_buffer = vertex_slice.buffer;
    slice.vertex_offset = vertex_slice.offset;
    slice.index_buffer = index_slice.buffer;
    slice.index_offset = index_slice.offset;
    slice.index_count = static_cast<NSUInteger>(mesh.indices.size());
    return true;
}

} // namespace draxul::codeviz_metal
