// Vulkan spinning cube renderer for MegaCityHost.
// CubeRenderPass::record() obtains device and render_pass from VkRenderContext
// and lazily creates (or recreates on swapchain rebuild) the VkCubePass pipeline.
#include "cube_render_pass.h"
#include "vk_cube_pass.h"
#include "vk_render_context.h"
#include <cmath>
#include <draxul/log.h>

namespace draxul
{

// ---------------------------------------------------------------------------
// Column-major mat4 helpers (Vulkan: Y-flipped projection, depth [0,1])
// ---------------------------------------------------------------------------
struct Mat4
{
    float m[16] = {};
};

static Mat4 mat4_identity()
{
    Mat4 r;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}

// Right-handed perspective, Y-flipped for Vulkan NDC (y-down), depth [0,1].
static Mat4 mat4_perspective(float fov_y, float aspect, float near_z, float far_z)
{
    float ys = 1.0f / tanf(fov_y * 0.5f);
    float xs = ys / aspect;
    float zs = far_z / (near_z - far_z);
    Mat4 r;
    r.m[0] = xs;
    r.m[5] = -ys; // negate to flip Y so +Y is up on screen
    r.m[10] = zs;
    r.m[11] = -1.0f; // perspective divide
    r.m[14] = zs * near_z;
    return r;
}

static Mat4 mat4_translation(float tx, float ty, float tz)
{
    Mat4 r = mat4_identity();
    r.m[12] = tx;
    r.m[13] = ty;
    r.m[14] = tz;
    return r;
}

static Mat4 mat4_rotation_y(float angle)
{
    float c = cosf(angle), s = sinf(angle);
    Mat4 r = mat4_identity();
    r.m[0] = c;
    r.m[2] = s;
    r.m[8] = -s;
    r.m[10] = c;
    return r;
}

static Mat4 mat4_rotation_x(float angle)
{
    float c = cosf(angle), s = sinf(angle);
    Mat4 r = mat4_identity();
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
}

// C = A * B (column-major)
static Mat4 mat4_mul(const Mat4& a, const Mat4& b)
{
    Mat4 r;
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
            for (int k = 0; k < 4; k++)
                r.m[col * 4 + row] += a.m[k * 4 + row] * b.m[col * 4 + k];
    return r;
}

// ---------------------------------------------------------------------------
// CubeRenderPass::State — Vulkan pipeline state, lazily created / recreated.
// ---------------------------------------------------------------------------
struct CubeRenderPass::State
{
    VkCubePass cube_pass;
    VkRenderPass last_render_pass = VK_NULL_HANDLE;
    VkDevice last_device = VK_NULL_HANDLE;

    bool ensure(VkDevice device, VkRenderPass render_pass)
    {
        if (cube_pass.pipeline != VK_NULL_HANDLE && render_pass == last_render_pass)
            return true; // already valid for this render pass

        // Destroy stale pipeline (render pass was recreated, e.g., swapchain rebuild)
        if (cube_pass.pipeline != VK_NULL_HANDLE)
            cube_pass.destroy(last_device);

        if (!cube_pass.create(device, render_pass, "shaders"))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity (Vulkan): failed to create cube pipeline");
            return false;
        }

        last_device = device;
        last_render_pass = render_pass;
        return true;
    }

    void destroy()
    {
        if (cube_pass.pipeline != VK_NULL_HANDLE && last_device != VK_NULL_HANDLE)
            cube_pass.destroy(last_device);
    }
};

CubeRenderPass::CubeRenderPass()
    : state_(std::make_unique<State>())
{
}

CubeRenderPass::~CubeRenderPass()
{
    state_->destroy();
}

// ---------------------------------------------------------------------------
// Called every frame from I3DRenderer (inside render pass on Vulkan)
// ---------------------------------------------------------------------------
void CubeRenderPass::record(IRenderContext& ctx)
{
    auto* vk_ctx = static_cast<VkRenderContext*>(&ctx);
    auto cmd = static_cast<VkCommandBuffer>(ctx.native_command_buffer());
    if (!cmd)
        return;

    if (!state_->ensure(vk_ctx->device(), vk_ctx->render_pass()))
        return;

    const int viewport_w = ctx.viewport_w();
    const int viewport_h = ctx.viewport_h();
    float aspect = (viewport_h > 0) ? (float)viewport_w / (float)viewport_h : 1.0f;
    Mat4 proj = mat4_perspective(0.7854f /* 45 deg */, aspect, 0.1f, 100.0f);
    Mat4 view = mat4_translation(0.0f, 0.0f, -3.0f);
    Mat4 rot = mat4_mul(mat4_rotation_y(angle_), mat4_rotation_x(angle_ * 0.4f));
    Mat4 mvp = mat4_mul(proj, mat4_mul(view, rot));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->cube_pass.pipeline);
    vkCmdPushConstants(cmd, state_->cube_pass.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, mvp.m);
    vkCmdDraw(cmd, 36, 1, 0, 0);
}

} // namespace draxul
