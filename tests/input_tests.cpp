
#include <SDL3/SDL.h>
#include <catch2/catch_all.hpp>
#include <draxul/nvim.h>

#include <string>
#include <vector>

using namespace draxul;

namespace
{

class FakeRpcChannel final : public IRpcChannel
{
public:
    struct Call
    {
        std::string method;
        std::vector<MpackValue> params;
    };

    RpcResult request(const std::string& method, const std::vector<MpackValue>& params) override
    {
        requests.push_back({ method, params });
        RpcResult result;
        result.transport_ok = true;
        result.result = NvimRpc::make_nil();
        return result;
    }

    void notify(const std::string& method, const std::vector<MpackValue>& params) override
    {
        notifications.push_back({ method, params });
    }

    std::vector<Call> requests;
    std::vector<Call> notifications;
};

} // namespace

TEST_CASE("input translates control chords and suppresses duplicate text", "[input]")
{
    FakeRpcChannel rpc;
    NvimInput input;
    input.initialize(&rpc, 10, 20);

    input.on_key({ 0, SDLK_A, kModCtrl, true });
    INFO("control chord emits one notification");
    REQUIRE(static_cast<int>(rpc.notifications.size()) == 1);
    INFO("control chord uses nvim_input");
    REQUIRE(rpc.notifications[0].method == std::string("nvim_input"));
    INFO("control chord is translated");
    REQUIRE(rpc.notifications[0].params[0].as_str() == std::string("<C-a>"));

    input.on_text_input({ "a" });
    INFO("suppressed text input does not emit");
    REQUIRE(static_cast<int>(rpc.notifications.size()) == 1);
}

TEST_CASE("input escapes lt and maps mouse coordinates to grid cells", "[input]")
{
    FakeRpcChannel rpc;
    NvimInput input;
    input.initialize(&rpc, 10, 20);

    input.on_text_input({ "<" });
    input.on_mouse_button({ SDL_BUTTON_LEFT, true, kModShift, { 15, 25 } });
    input.on_mouse_move({ kModShift, { 25, 45 } });
    input.on_mouse_wheel({ { 0.0f, 1.0f }, kModCtrl, { 15, 25 } });

    INFO("text, button, drag, and wheel each emit a notification");
    REQUIRE(static_cast<int>(rpc.notifications.size()) == 4);
    INFO("lt is escaped");
    REQUIRE(rpc.notifications[0].params[0].as_str() == std::string("<lt>"));
    INFO("mouse uses nvim_input_mouse");
    REQUIRE(rpc.notifications[1].method == std::string("nvim_input_mouse"));
    INFO("mouse button name is encoded");
    REQUIRE(rpc.notifications[1].params[0].as_str() == std::string("left"));
    INFO("mouse action is encoded");
    REQUIRE(rpc.notifications[1].params[1].as_str() == std::string("press"));
    INFO("mouse modifiers are encoded");
    REQUIRE(rpc.notifications[1].params[2].as_str() == std::string("S"));
    INFO("mouse row is derived from cell height");
    REQUIRE(rpc.notifications[1].params[4].as_int() == 1);
    INFO("mouse col is derived from cell width");
    REQUIRE(rpc.notifications[1].params[5].as_int() == 1);
    INFO("drag keeps the pressed button");
    REQUIRE(rpc.notifications[2].params[0].as_str() == std::string("left"));
    INFO("drag action is encoded");
    REQUIRE(rpc.notifications[2].params[1].as_str() == std::string("drag"));
    INFO("drag modifiers are encoded");
    REQUIRE(rpc.notifications[2].params[2].as_str() == std::string("S"));
    INFO("wheel input uses the wheel button");
    REQUIRE(rpc.notifications[3].params[0].as_str() == std::string("wheel"));
    INFO("wheel direction is encoded");
    REQUIRE(rpc.notifications[3].params[1].as_str() == std::string("up"));
    INFO("wheel modifiers are encoded");
    REQUIRE(rpc.notifications[3].params[2].as_str() == std::string("C"));
}

TEST_CASE("mouse coordinates are offset by viewport origin", "[input]")
{
    FakeRpcChannel rpc;
    NvimInput input;
    // cell_w=10, cell_h=20; viewport origin at pixel (8, 12)
    input.initialize(&rpc, 10, 20);
    input.set_viewport_origin(8, 12);
    input.set_grid_size(10, 5);

    // pixel (8 + 1*10, 12 + 2*20) = (18, 52) => col=1, row=2
    input.on_mouse_button({ SDL_BUTTON_LEFT, true, kModNone, { 18, 52 } });
    INFO("button press emits one notification");
    REQUIRE(static_cast<int>(rpc.notifications.size()) == 1);
    INFO("row accounts for viewport y offset");
    REQUIRE(rpc.notifications[0].params[4].as_int() == 2);
    INFO("col accounts for viewport x offset");
    REQUIRE(rpc.notifications[0].params[5].as_int() == 1);
}

TEST_CASE("mouse click in padding area is clamped to col=0 row=0", "[input]")
{
    FakeRpcChannel rpc;
    NvimInput input;
    // cell_w=10, cell_h=20; viewport origin at pixel (8, 12)
    input.initialize(&rpc, 10, 20);
    input.set_viewport_origin(8, 12);
    input.set_grid_size(10, 5);

    // pixel (3, 5) is inside padding (x < 8, y < 12) => clamped to col=0, row=0
    input.on_mouse_button({ SDL_BUTTON_LEFT, true, kModNone, { 3, 5 } });
    INFO("button press in padding emits one notification");
    REQUIRE(static_cast<int>(rpc.notifications.size()) == 1);
    INFO("row in padding area is clamped to 0");
    REQUIRE(rpc.notifications[0].params[4].as_int() == 0);
    INFO("col in padding area is clamped to 0");
    REQUIRE(rpc.notifications[0].params[5].as_int() == 0);
}
