// SatView host lifecycle and draw smoke tests (kanban 18).
//
// Drives the real SatViewHost end to end with NO GPU, NO network, and NO
// system-clock reads. Every dependency an initialize() needs is faked through
// the shared OfflineSatViewHost fixture (tests/support/satview_host_fixture.h):
// a fixed fake clock, offline catalog/cloud transports over a temp cache
// directory, and a FakeTermRenderer that doubles as the render-pass sink and
// ImGui backend. These tests protect the private host seams the item 26
// library-boundary refactor leans on (SatViewHostTestAccess).
//
// This file is compiled only when DRAXUL_ENABLE_SATVIEW is ON (the tests/
// CMakeLists filters satview_*_tests.cpp out otherwise), so the whole suite is
// absent from the binary in a SatView-OFF build.

#include <catch2/catch_test_macros.hpp>

#include "home_dir_redirect.h"
#include "satview_host_fixture.h"
#include "temp_dir.h"

#include <draxul/config_document.h>
#include <draxul/host.h>
#include <draxul/satview/satview_config.h>

using namespace draxul;
using namespace draxul::satview;

namespace
{

// A curated bundle of durable [satview] fields flipped away from their
// defaults. Chosen to be a fixed point of SatViewHost::apply_config under the
// Earth POV (values stay in range, no cross-coupling), so it survives an
// apply -> current_config round-trip and a store -> load round-trip unchanged.
SatViewConfig smoke_config(SatViewConfig base)
{
    base.camera_pov = SatViewCameraPov::Earth;
    base.color_mode = SatViewColorMode::OrbitClass;
    base.track_display_mode = SatViewTrackDisplayMode::SelectedOnly;
    base.satellite_display_mode = SatViewSatelliteDisplayMode::MarkersOnly;
    base.projection_mode = SatViewProjectionMode::Map;
    base.track_satellite_limit = 25;
    base.track_sample_count = 64;
    base.refresh_tracks_each_step = true;
    base.constellation_lines_enabled = true;
    base.constellation_boundaries_enabled = true;
    base.milky_way_enabled = true;
    base.star_min_magnitude = -1.0f;
    base.star_max_magnitude = 5.0f;
    base.tone_map_exposure = 2.0f;
    base.time_speed = 120.0f;
    base.clouds_enabled = false;
    base.atmosphere_enabled = false;
    base.cardinal_labels_enabled = true;
    return base;
}

// Assert the durable fields smoke_config() touched survived a round-trip.
void check_smoke_fields(const SatViewConfig& c)
{
    CHECK(c.color_mode == SatViewColorMode::OrbitClass);
    CHECK(c.track_display_mode == SatViewTrackDisplayMode::SelectedOnly);
    CHECK(c.satellite_display_mode == SatViewSatelliteDisplayMode::MarkersOnly);
    CHECK(c.projection_mode == SatViewProjectionMode::Map);
    CHECK(c.track_satellite_limit == 25);
    CHECK(c.track_sample_count == 64);
    CHECK(c.refresh_tracks_each_step);
    CHECK(c.constellation_lines_enabled);
    CHECK(c.constellation_boundaries_enabled);
    CHECK(c.milky_way_enabled);
    CHECK(c.star_min_magnitude == -1.0f);
    CHECK(c.star_max_magnitude == 5.0f);
    CHECK(c.tone_map_exposure == 2.0f);
    CHECK(c.time_speed == 120.0f);
    CHECK_FALSE(c.clouds_enabled);
    CHECK_FALSE(c.atmosphere_enabled);
    CHECK(c.cardinal_labels_enabled);
}

} // namespace

TEST_CASE("SatView host initializes, draws, and shuts down offline", "[satview][host][smoke]")
{
    OfflineSatViewHost offline;
    REQUIRE(offline.initialize());

    // Core wiring came up on the CPU-only path.
    CHECK(offline.host.is_running());
    CHECK(offline.host.init_error().empty());
    CHECK(SatViewHostTestAccess::running(offline.host));
    CHECK(SatViewHostTestAccess::scene_pass_attached(offline.host));
    CHECK(offline.callbacks.request_frame_calls > 0); // initialize() requests a redraw
    CHECK(offline.callbacks.last_window_title == "SatView");

    // The offline catalog and cloud transports are actually exercised. Both
    // fetch on their worker threads at startup with an empty temp cache, so
    // this proves no real network path was taken.
    REQUIRE(offline.pump_until([&] {
        return offline.catalog_fetch_calls.load() > 0 && offline.cloud_fetch_calls.load() > 0;
    }));

    // One pump + draw records the scene pass and ImGui without a GPU.
    offline.draw_once();
    CHECK(offline.renderer.record_render_pass_calls > 0);
    CHECK(offline.renderer.render_imgui_calls > 0);
    CHECK(offline.renderer.last_recorded_render_pass != nullptr);

    // Explicit shutdown is idempotent and leaves the host stopped (the fixture
    // destructor's second shutdown must also stay safe).
    offline.host.shutdown();
    CHECK_FALSE(offline.host.is_running());
    CHECK_FALSE(SatViewHostTestAccess::running(offline.host));
}

TEST_CASE("SatView host applies and reads back durable config in memory", "[satview][host][config]")
{
    OfflineSatViewHost offline;
    REQUIRE(offline.initialize());

    SatViewHostTestAccess::apply_config(offline.host, smoke_config(SatViewConfig{}));
    check_smoke_fields(SatViewHostTestAccess::current_config(offline.host));
}

TEST_CASE("SatView host persists durable config through shutdown", "[satview][host][config]")
{
    // Redirect HOME so the durable persist path (shutdown -> save_merged_satview
    // _config, which loads/saves the real config.toml) writes into a temp dir.
    tests::TempDir home{ "satview-host-config-home" };
    tests::HomeDirRedirect redirect{ home.path };
    ConfigDocument document = ConfigDocument::load();

    {
        OfflineSatViewHost offline;
        REQUIRE(offline.initialize(/*attach_imgui=*/true, &document));
        SatViewHostTestAccess::apply_config(offline.host, smoke_config(SatViewConfig{}));
        offline.host.shutdown(); // persists current_config() to the redirected config.toml
    }

    // Reload straight from disk to prove the [satview] fields were written, not
    // just held in memory.
    const SatViewConfig persisted = load_satview_config(ConfigDocument::load());
    check_smoke_fields(persisted);
}

TEST_CASE("SatView host transitions camera POV and redraws", "[satview][host]")
{
    OfflineSatViewHost offline;
    REQUIRE(offline.initialize());
    REQUIRE(SatViewHostTestAccess::camera_pov(offline.host) == SatViewCameraPov::Earth);

    const double when = SatViewHostTestAccess::simulated_seconds(offline.host);
    SatViewHostTestAccess::set_camera_pov(offline.host, SatViewCameraPov::Moon, when);
    CHECK(SatViewHostTestAccess::camera_pov(offline.host) == SatViewCameraPov::Moon);

    // Drawing in the new POV stays crash-free and still records the scene pass.
    offline.draw_once();
    CHECK(offline.renderer.record_render_pass_calls > 0);
}

TEST_CASE("SatView host tracks a satellite selection and clears it", "[satview][host]")
{
    OfflineSatViewHost offline;
    REQUIRE(offline.initialize());
    CHECK_FALSE(SatViewHostTestAccess::has_selection(offline.host));

    // Select the fixture's bundled "SMOKE ONE" object (NORAD 990001).
    SatViewHostTestAccess::set_selected_satellite(offline.host, 990001);
    CHECK(SatViewHostTestAccess::has_selection(offline.host));

    // The selected-satellite draw path stays crash-free without a GPU.
    offline.draw_once();

    // The public clear-selection action drops it back to no selection.
    REQUIRE(offline.host.dispatch_action("satview_clear_selection"));
    CHECK_FALSE(SatViewHostTestAccess::has_selection(offline.host));
}

TEST_CASE("SatView host frame requests settle when paused", "[satview][host][smoke]")
{
    OfflineSatViewHost offline;
    REQUIRE(offline.initialize());
    offline.pump_until_quiescent();

    // Pause via the public action (a config-style change), then drain the
    // one-shot redraw + dirty flag it raised.
    REQUIRE(offline.host.dispatch_action("satview_pause"));
    CHECK(SatViewHostTestAccess::paused(offline.host));
    offline.pump_until_quiescent();
    CHECK_FALSE(SatViewHostTestAccess::simulation_settings_dirty(offline.host));

    // A tight burst of pumps completes far inside one 33ms frame tick, so a
    // settled host requests at most a frame or two here -- NOT one per pump.
    // This proves the paused host holds a bounded animation cadence instead of
    // spinning and requesting frames forever.
    const int before = offline.callbacks.request_frame_calls;
    for (int i = 0; i < 200; ++i)
        offline.host.pump();
    const int burst = offline.callbacks.request_frame_calls - before;
    CHECK(burst <= 2);
}

TEST_CASE("SatView host dirty flags settle after a change", "[satview][host][smoke]")
{
    OfflineSatViewHost offline;
    REQUIRE(offline.initialize());
    offline.pump_until_quiescent();

    // A public action that dirties the simulation render settings.
    REQUIRE(offline.host.dispatch_action("satview_clear_selection"));
    CHECK(SatViewHostTestAccess::simulation_settings_dirty(offline.host));

    // A single pump consumes the dirty flag; it does not stay dirty forever.
    offline.host.pump();
    CHECK_FALSE(SatViewHostTestAccess::simulation_settings_dirty(offline.host));
}
