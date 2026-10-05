#include <draxul/activity_coin_pass.h>

#include <draxul/vulkan/vk_render_context.h>

#include <draxul/log.h>
#include <draxul/runtime_path.h>

#include <fstream>
#include <string>

namespace draxul
{
namespace
{

constexpr uint32_t kCoinVertexCount = 32 * 12;

struct CoinUniforms
{
    float viewport_radius[4];
    float coin[4];
};

std::vector<uint32_t> read_spirv(const char* name)
{
    const auto path = bundled_asset_path("shaders") / name;
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Activity coin failed to open shader: %s",
            path.string().c_str());
        return {};
    }
    const auto size = static_cast<size_t>(file.tellg());
    if (size == 0 || size % sizeof(uint32_t) != 0)
        return {};
    std::vector<uint32_t> words(size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(size));
    return words;
}

class VulkanActivityCoinPass final : public IActivityCoinPass
{
public:
    ~VulkanActivityCoinPass() override
    {
        if (device_ != VK_NULL_HANDLE && pipeline_ != VK_NULL_HANDLE)
            vkDeviceWaitIdle(device_);
        destroy_pipeline();
    }

    void set_coins(std::vector<ActivityCoinInstance> coins) override
    {
        coins_ = std::move(coins);
    }

    void record(IRenderContext& ctx) override
    {
        if (coins_.empty())
            return;
        auto& vk_ctx = static_cast<VkRenderContext&>(ctx);
        VkCommandBuffer cmd = vk_ctx.command_buffer();
        const int width = vk_ctx.viewport_w();
        const int height = vk_ctx.viewport_h();
        if (cmd == VK_NULL_HANDLE || width <= 0 || height <= 0
            || !ensure_pipeline(vk_ctx.device(), vk_ctx.render_pass()))
        {
            coins_.clear();
            return;
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        for (const ActivityCoinInstance& coin : coins_)
        {
            const CoinUniforms uniforms{
                { static_cast<float>(width), static_cast<float>(height), coin.radius,
                    coin.brightness },
                { coin.center_x, coin.center_y, coin.angle,
                    static_cast<float>(static_cast<int>(coin.style)) },
            };
            vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
                sizeof(uniforms), &uniforms);
            vkCmdDraw(cmd, kCoinVertexCount, 1, 0, 0);
        }
        coins_.clear();
    }

private:
    bool ensure_pipeline(VkDevice device, VkRenderPass render_pass)
    {
        if (device == VK_NULL_HANDLE || render_pass == VK_NULL_HANDLE)
            return false;
        if (device_ == device && render_pass_ == render_pass)
            return pipeline_ != VK_NULL_HANDLE;
        if (pipeline_ != VK_NULL_HANDLE)
            vkDeviceWaitIdle(device_);
        destroy_pipeline();
        device_ = device;
        render_pass_ = render_pass;

        const auto vertex_words = read_spirv("activity_coin.vert.spv");
        const auto fragment_words = read_spirv("activity_coin.frag.spv");
        if (vertex_words.empty() || fragment_words.empty())
            return false;

        const auto make_shader = [&](const std::vector<uint32_t>& words) {
            VkShaderModuleCreateInfo create_info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            create_info.codeSize = words.size() * sizeof(uint32_t);
            create_info.pCode = words.data();
            VkShaderModule module = VK_NULL_HANDLE;
            return vkCreateShaderModule(device_, &create_info, nullptr, &module) == VK_SUCCESS
                ? module
                : VK_NULL_HANDLE;
        };
        const VkShaderModule vertex = make_shader(vertex_words);
        const VkShaderModule fragment = make_shader(fragment_words);
        const auto destroy_modules = [&] {
            if (vertex)
                vkDestroyShaderModule(device_, vertex, nullptr);
            if (fragment)
                vkDestroyShaderModule(device_, fragment, nullptr);
        };
        if (!vertex || !fragment)
        {
            destroy_modules();
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Activity coin could not create shader modules");
            return false;
        }

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_range.size = sizeof(CoinUniforms);
        VkPipelineLayoutCreateInfo layout_info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layout_info.pushConstantRangeCount = 1;
        layout_info.pPushConstantRanges = &push_range;
        if (vkCreatePipelineLayout(device_, &layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS)
        {
            destroy_modules();
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Activity coin could not create pipeline layout");
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertex_input{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
        };
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
        };
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        // Facets facing away are culled in the vertex shader, which keeps the
        // result independent of the backend's winding convention.
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
        };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
        };
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth_stencil{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
        };
        depth_stencil.depthTestEnable = VK_FALSE;
        depth_stencil.depthWriteEnable = VK_FALSE;
        depth_stencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
            | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
            | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
        };
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;
        const VkDynamicState dynamic_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
        };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;

        VkGraphicsPipelineCreateInfo pipeline_info{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = stages;
        pipeline_info.pVertexInputState = &vertex_input;
        pipeline_info.pInputAssemblyState = &assembly;
        pipeline_info.pViewportState = &viewport;
        pipeline_info.pRasterizationState = &raster;
        pipeline_info.pMultisampleState = &multisample;
        pipeline_info.pDepthStencilState = &depth_stencil;
        pipeline_info.pColorBlendState = &blend;
        pipeline_info.pDynamicState = &dynamic;
        pipeline_info.layout = pipeline_layout_;
        pipeline_info.renderPass = render_pass_;
        pipeline_info.subpass = 0;
        const VkResult result = vkCreateGraphicsPipelines(
            device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_);
        destroy_modules();
        if (result != VK_SUCCESS)
        {
            pipeline_ = VK_NULL_HANDLE;
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "Activity coin could not create graphics pipeline");
            return false;
        }
        return true;
    }

    void destroy_pipeline()
    {
        if (device_ == VK_NULL_HANDLE)
            return;
        if (pipeline_ != VK_NULL_HANDLE)
            vkDestroyPipeline(device_, pipeline_, nullptr);
        if (pipeline_layout_ != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
        pipeline_layout_ = VK_NULL_HANDLE;
    }

    std::vector<ActivityCoinInstance> coins_;
    VkDevice device_ = VK_NULL_HANDLE;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace

std::unique_ptr<IActivityCoinPass> create_activity_coin_pass()
{
    return std::make_unique<VulkanActivityCoinPass>();
}

} // namespace draxul
