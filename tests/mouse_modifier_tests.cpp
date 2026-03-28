#include "support/fake_rpc_channel.h"

#include <SDL3/SDL.h>
#include <catch2/catch_all.hpp>
#include <draxul/events.h>
#include <draxul/nvim.h>

#include <string>
#include <vector>

// These tests verify that modifier state carried in the MouseButtonEvent,
// MouseWheelEvent, and MouseMoveEvent structs is faithfully propagated to
// Neovim's nvim_input_mouse RPC call.  They construct the draxul event structs
// directly (bypassing the SDL translator) so that the modifier bits are fully
// under test control and no global SDL state is consulted.

using namespace draxul;

namespace
{
using draxul::tests::FakeRpcChannel;

// Return the modifier string from the most recent nvim_input_mouse notification.
std::string last_mouse_mod(const FakeRpcChannel& rpc)
{
    REQUIRE(!rpc.notifications.empty());
    REQUIRE(rpc.notifications.back().method == "nvim_input_mouse");
    return rpc.notifications.back().params[2].as_str();
}

} // namespace

// ---------------------------------------------------------------------------
// Button event modifier tests
// ---------------------------------------------------------------------------

TEST_CASE("mouse_modifier: no modifiers on button press produces empty modifier string", "[mouse_modifier]")
{
    FakeRpcChannel rpc;
    NvimInput input;
    input.initialize(&rpc, 10, 20);

    input.on_mouse_button({ SDL_BUTTON_LEFT, true, kModNone, { 0, 0 } });
    INFO("modifier string should be empty when no modifiers are set");
    REQUIRE(last_mouse_mod(rpc) == "");
}

TEST_CASE("mouse_modifier: Ctrl on button press produces 'C' modifier string", "[mouse_modifier]")
{
    FakeRpcChannel rpc;
    NvimInput input;
    input.initialize(&rpc, 10, 20);

    input.on_mouse_button({ SDL_BUTTON_LEFT, true, kModCtrl, { 0, 0 } });
    INFO("Ctrl modifier should produce 'C' in modifier string");
    REQUIRE(last_mouse_mod(rpc) == "C");
}

TEST_CASE("mouse_modifier: Shift on button press produces 'S' modifier string", "[mouse_modifier]")
{
    FakeRpcChannel rpc;
    NvimInput input;
    input.initialize(&rpc, 10, 20);

    input.on_mouse_button({ SDL_BUTTON_LEFT, true, kModShift, { 0, 0 } });
    INFO("Shift modifier should produce 'S' in modifier string");
    REQUIRE(last_mouse_mod(rpc) == "S");
}

TEST_CASE("mouse_modifier: Ctrl+Shift on button press produces 'SC' modifier string", "[mouse_modifier]")
{
    FakeRpcChannel rpc;
    NvimInput input;
    input.initialize(&rpc, 10, 20);

    input.on_mouse_button({ SDL_BUTTON_RIGHT, true, kModCtrl | kModShift, { 0, 0 } });
    INFO("Both Shift and Ctrl modifiers should appear in modifier string");
    std::string mod = last_mouse_mod(rpc);
    REQUIRE(mod.find('S') != std::string::npos);
    REQUIRE(mod.find('C') != std::string::npos);
}

TEST_CASE("mouse_modifier: Alt on button press produces 'A' modifier string", "[mouse_modifier]")
{
    FakeRpcChannel rpc;
    NvimInput input;
    input.initialize(&rpc, 10, 20);

    input.on_mouse_button({ SDL_BUTTON_MIDDLE, true, kModAlt, { 0, 0 } });
    INFO("Alt modifier should produce 'A' in modifier string");
    REQUIRE(last_mouse_mod(rpc) == "A");
}

// ---------------------------------------------------------------------------
// Wheel event modifier tests
// ---------------------------------------------------------------------------

TEST_CASE("mouse_modifier: no modifiers on wheel produces empty modifier string", "[mouse_modifier]")
{
    FakeRpcChannel rpc;
    NvimInput input;
    input.initialize(&rpc, 10, 20);

    input.on_mouse_wheel({ { 0.0f, 1.0f }, kModNone, { 0, 0 } });
    INFO("modifier string should be empty when no modifiers are set");
    REQUIRE(last_mouse_mod(rpc) == "");
}

TEST_CASE("mouse_modifier: Alt on wheel up produces 'A' modifier string", "[mouse_modifier]")
{
    FakeRpcChannel rpc;
    NvimInput input;
    input.initialize(&rpc, 10, 20);

    input.on_mouse_wheel({ { 0.0f, 1.0f }, kModAlt, { 0, 0 } });
    INFO("Alt modifier on wheel should produce 'A' in modifier string");
    REQUIRE(last_mouse_mod(rpc) == "A");
}

TEST_CASE("mouse_modifier: Ctrl on wheel down produces 'C' modifier string", "[mouse_modifier]")
{
    FakeRpcChannel rpc;
    NvimInput input;
    input.initialize(&rpc, 10, 20);

    input.on_mouse_wheel({ { 0.0f, -1.0f }, kModCtrl, { 0, 0 } });
    INFO("Ctrl modifier on wheel should produce 'C' in modifier string");
    REQUIRE(last_mouse_mod(rpc) == "C");
}

// ---------------------------------------------------------------------------
// Drag event modifier tests
// ---------------------------------------------------------------------------

TEST_CASE("mouse_modifier: Shift on drag produces 'S' modifier string", "[mouse_modifier]")
{
    FakeRpcChannel rpc;
    NvimInput input;
    input.initialize(&rpc, 10, 20);

    // Press first to set mouse_pressed_ state, then move.
    input.on_mouse_button({ SDL_BUTTON_LEFT, true, kModNone, { 0, 0 } });
    rpc.notifications.clear();

    input.on_mouse_move({ kModShift, { 10, 10 } });
    INFO("Shift modifier on drag should produce 'S' in modifier string");
    REQUIRE(last_mouse_mod(rpc) == "S");
}

TEST_CASE("mouse_modifier: no modifier on drag produces empty modifier string", "[mouse_modifier]")
{
    FakeRpcChannel rpc;
    NvimInput input;
    input.initialize(&rpc, 10, 20);

    input.on_mouse_button({ SDL_BUTTON_LEFT, true, kModNone, { 0, 0 } });
    rpc.notifications.clear();

    input.on_mouse_move({ kModNone, { 10, 10 } });
    INFO("modifier string should be empty when no modifiers are set during drag");
    REQUIRE(last_mouse_mod(rpc) == "");
}
