#include <catch2/catch_test_macros.hpp>

#include <draxul/nanovg_demo_host.h>

using namespace draxul;

TEST_CASE("NanoVG demo texture probe spans buffered frames and balances ownership",
    "[nanovg][render-test][texture]")
{
    STATIC_REQUIRE(kNanoVGTextureProbeFrameCount > 2);

    bool image_exists = false;
    int creates = 0;
    int updates = 0;
    int samples = 0;
    int destroys = 0;
    int frame_requests = 0;

    for (std::uint32_t frame = 0; frame < kNanoVGTextureProbeFrameCount; ++frame)
    {
        const NanoVGTextureProbeStep step = nanovg_texture_probe_step(frame);
        if (step.destroy)
        {
            REQUIRE(image_exists);
            image_exists = false;
            ++destroys;
        }
        if (step.create)
        {
            REQUIRE_FALSE(image_exists);
            image_exists = true;
            ++creates;
        }
        if (step.update)
        {
            REQUIRE(image_exists);
            ++updates;
        }
        if (step.sample)
        {
            REQUIRE(image_exists);
            ++samples;
        }
        if (step.request_next_frame)
            ++frame_requests;
    }

    CHECK(creates == 2);
    CHECK(updates == 2);
    CHECK(samples == 7);
    CHECK(destroys == creates);
    CHECK_FALSE(image_exists);
    CHECK(frame_requests == static_cast<int>(kNanoVGTextureProbeFrameCount - 1));
}

TEST_CASE("NanoVG demo texture probe stops requesting frames after completion",
    "[nanovg][render-test][texture]")
{
    const NanoVGTextureProbeStep done =
        nanovg_texture_probe_step(kNanoVGTextureProbeFrameCount);
    CHECK_FALSE(done.destroy);
    CHECK_FALSE(done.create);
    CHECK_FALSE(done.update);
    CHECK_FALSE(done.sample);
    CHECK_FALSE(done.request_next_frame);
}
