#include "../plugins/support/nanovg/src/plugin_nanovg_pass_internal.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{

using draxul::plugin_support::detail::PluginNanoVGFrameOperations;
using draxul::plugin_support::detail::PluginNanoVGNativeBackend;
using draxul::plugin_support::detail::VulkanTargetIdentity;

struct BackendState
{
    std::vector<std::string> events;
    std::vector<const void*> forwarded_frames;
    std::vector<bool> preparation_results;
    std::size_t next_result = 0;
};

class RecordingBackend final : public PluginNanoVGNativeBackend
{
public:
    explicit RecordingBackend(std::shared_ptr<BackendState> state)
        : state_(std::move(state))
    {
    }

    ~RecordingBackend() override { state_->events.emplace_back("backend destroyed"); }

    NVGcontext* prepare_vulkan(
        const DraxulPluginVulkanFrameV2& frame) override
    {
        state_->events.emplace_back("prepare vulkan");
        state_->forwarded_frames.push_back(&frame);
        return next_context();
    }

    NVGcontext* prepare_metal(const DraxulPluginMetalFrameV2& frame) override
    {
        state_->events.emplace_back("prepare metal");
        state_->forwarded_frames.push_back(&frame);
        return next_context();
    }

private:
    NVGcontext* next_context()
    {
        const bool succeeds = state_->next_result >= state_->preparation_results.size()
            || state_->preparation_results[state_->next_result];
        ++state_->next_result;
        return succeeds
            ? reinterpret_cast<NVGcontext*>(static_cast<std::uintptr_t>(0x1234))
            : nullptr;
    }

    std::shared_ptr<BackendState> state_;
};

class RecordingOperations final : public PluginNanoVGFrameOperations
{
public:
    explicit RecordingOperations(std::shared_ptr<BackendState> state)
        : state_(std::move(state))
    {
    }

    ~RecordingOperations() override
    {
        state_->events.emplace_back("operations destroyed");
    }

    void begin_frame(NVGcontext*, int width, int height) override
    {
        state_->events.emplace_back(
            "begin " + std::to_string(width) + "x" + std::to_string(height));
    }

    void scissor(NVGcontext*, int x, int y, int width, int height) override
    {
        state_->events.emplace_back("scissor " + std::to_string(x) + ","
            + std::to_string(y) + " " + std::to_string(width) + "x"
            + std::to_string(height));
    }

    void translate(NVGcontext*, int x, int y) override
    {
        state_->events.emplace_back(
            "translate " + std::to_string(x) + "," + std::to_string(y));
    }

    void end_frame(NVGcontext*) override
    {
        state_->events.emplace_back("end");
    }

private:
    std::shared_ptr<BackendState> state_;
};

std::unique_ptr<draxul::plugin_support::IPluginNanoVGPass> make_pass(
    const std::shared_ptr<BackendState>& state)
{
    return draxul::plugin_support::detail::create_plugin_nanovg_pass_for_test(
        std::make_unique<RecordingBackend>(state),
        std::make_unique<RecordingOperations>(state));
}

DraxulPluginVulkanFrameV2 vulkan_frame()
{
    DraxulPluginVulkanFrameV2 frame{};
    frame.framebuffer_width = 900;
    frame.framebuffer_height = 700;
    frame.viewport.x = 31;
    frame.viewport.y = 47;
    frame.viewport.width = 320;
    frame.viewport.height = 180;
    return frame;
}

struct CallbackLifetime
{
    explicit CallbackLifetime(std::shared_ptr<BackendState> state)
        : state(std::move(state))
    {
    }
    ~CallbackLifetime() { state->events.emplace_back("callback destroyed"); }
    std::shared_ptr<BackendState> state;
};

} // namespace

TEST_CASE("plugin NanoVG pass forwards viewport operations and consumes callbacks",
    "[plugin][nanovg][adapter]")
{
    auto state = std::make_shared<BackendState>();
    auto pass = make_pass(state);
    auto frame = vulkan_frame();
    int calls = 0;
    pass->set_draw_callback([&](NVGcontext* context, int width, int height) {
        CHECK(context
            == reinterpret_cast<NVGcontext*>(static_cast<std::uintptr_t>(0x1234)));
        CHECK(width == 320);
        CHECK(height == 180);
        ++calls;
        state->events.emplace_back("draw");
    });

    CHECK(pass->render_vulkan(frame));
    CHECK(pass->render_vulkan(frame));
    CHECK(calls == 1);
    REQUIRE(state->forwarded_frames.size() == 1);
    CHECK(state->forwarded_frames.front() == &frame);
    CHECK(state->events
        == std::vector<std::string>{ "prepare vulkan", "begin 900x700",
            "scissor 31,47 320x180", "translate 31,47", "draw", "end" });
}

TEST_CASE("plugin NanoVG pass clamps negative viewport sizes",
    "[plugin][nanovg][adapter]")
{
    auto state = std::make_shared<BackendState>();
    auto pass = make_pass(state);
    auto frame = vulkan_frame();
    frame.viewport.x = 0;
    frame.viewport.y = 0;
    frame.viewport.width = -8;
    frame.viewport.height = -9;
    pass->set_draw_callback([&](NVGcontext*, int width, int height) {
        CHECK(width == 0);
        CHECK(height == 0);
    });

    CHECK(pass->render_vulkan(frame));
    CHECK(state->events
        == std::vector<std::string>{ "prepare vulkan", "begin 900x700",
            "scissor 0,0 0x0", "end" });
}

TEST_CASE("plugin NanoVG pass retains callbacks across initialization retry",
    "[plugin][nanovg][adapter]")
{
    auto state = std::make_shared<BackendState>();
    state->preparation_results = { false, true };
    auto pass = make_pass(state);
    auto frame = vulkan_frame();
    int calls = 0;
    pass->set_draw_callback([&](NVGcontext*, int, int) { ++calls; });

    CHECK_FALSE(pass->render_vulkan(frame));
    CHECK(calls == 0);
    CHECK(pass->render_vulkan(frame));
    CHECK(calls == 1);
}

TEST_CASE("plugin NanoVG pass forwards Metal frames through the same adapter",
    "[plugin][nanovg][adapter]")
{
    auto state = std::make_shared<BackendState>();
    auto pass = make_pass(state);
    DraxulPluginMetalFrameV2 frame{};
    frame.framebuffer_width = 640;
    frame.framebuffer_height = 480;
    frame.viewport.width = 640;
    frame.viewport.height = 480;
    pass->set_draw_callback([](NVGcontext*, int, int) {});

    CHECK(pass->render_metal(frame));
    REQUIRE(state->forwarded_frames.size() == 1);
    CHECK(state->forwarded_frames.front() == &frame);
    CHECK(state->events.front() == "prepare metal");
}

TEST_CASE("plugin NanoVG pass tears down native state before callback captures",
    "[plugin][nanovg][adapter]")
{
    auto state = std::make_shared<BackendState>();
    auto pass = make_pass(state);
    auto lifetime = std::make_shared<CallbackLifetime>(state);
    pass->set_draw_callback([lifetime](NVGcontext*, int, int) {});
    lifetime.reset();

    pass.reset();

    const auto backend = std::find(
        state->events.begin(), state->events.end(), "backend destroyed");
    const auto callback = std::find(
        state->events.begin(), state->events.end(), "callback destroyed");
    REQUIRE(backend != state->events.end());
    REQUIRE(callback != state->events.end());
    CHECK(backend < callback);
}

TEST_CASE("plugin NanoVG Vulkan identity resets only for device or format changes",
    "[plugin][nanovg][adapter][vulkan]")
{
    VulkanTargetIdentity identity;
    CHECK_FALSE(identity.requires_reset(10, 20));
    identity.set(10, 20);
    CHECK_FALSE(identity.requires_reset(10, 20));
    CHECK(identity.requires_reset(11, 20));
    CHECK(identity.requires_reset(10, 21));
    identity.clear();
    CHECK_FALSE(identity.requires_reset(11, 21));
}
