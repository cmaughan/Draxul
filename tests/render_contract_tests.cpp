#include <draxul/nanovg_pass.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("NanoVG pass factory links through the render contract",
    "[render-contracts][nanovg]")
{
    auto pass = draxul::create_nanovg_pass();
    REQUIRE(pass);
    pass->set_draw_callback({});
}
