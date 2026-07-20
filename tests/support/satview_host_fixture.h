#pragma once

// Headless SatViewHost fixture (kanban 18): the shared test seam and fakes for
// driving the real SatView host without a GPU, network, or system clock.
//
// SatViewHostTestAccess is the ONE friend of SatViewHost (declared in
// satview_host.h) — its definition lives here so every host-level suite shares
// it. OfflineSatViewHost bundles the fakes an initialize() needs:
//   - a fixed fake clock (replaces the system-clock simulation seed),
//   - offline catalog + cloud transports (fake FetchFunctions + a temp cache
//     directory, reusing the pattern proven in the *_service_tests),
//   - a FakeTermRenderer that doubles as the render-pass sink and ImGui backend,
//   - a counting TestHostCallbacks that records frame requests.
//
// It builds the seams refactor card 26 (SatView library boundaries) leans on.

#include <draxul/satview/satview_config.h>
#include <draxul/satview/satview_host.h>

#ifdef DRAXUL_ENABLE_SATVIEW

#include "fake_renderer.h"
#include "temp_dir.h"
#include "test_host_callbacks.h"

#include <draxul/host.h>
#include <draxul/host_kind.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace draxul::satview
{

// The sole friend of SatViewHost used by tests. Installs the offline hooks and
// drives the same private methods the UI uses, keeping test setup free of
// devices, network, and the wall clock.
class SatViewHostTestAccess
{
public:
    // Install offline hooks BEFORE initialize(). Preserves production behaviour
    // when never called (test_hooks_.active stays false).
    static void install_offline_hooks(
        SatViewHost& host,
        std::function<double()> clock,
        std::function<std::string(std::string_view, std::string&)> catalog_fetch,
        std::function<std::string(std::string_view, std::string&)> cloud_fetch,
        std::string cache_directory)
    {
        SatViewHost::TestHooks hooks;
        hooks.active = true;
        hooks.clock = std::move(clock);
        hooks.catalog_fetch = std::move(catalog_fetch);
        hooks.cloud_fetch = std::move(cloud_fetch);
        hooks.cache_directory = std::move(cache_directory);
        host.test_hooks_ = std::move(hooks);
    }

    static void apply_config(SatViewHost& host, const SatViewConfig& config)
    {
        host.apply_config(config);
    }
    static SatViewConfig current_config(const SatViewHost& host)
    {
        return host.current_config();
    }
    static void set_camera_pov(SatViewHost& host, SatViewCameraPov pov, double simulation_seconds)
    {
        host.set_camera_pov(pov, simulation_seconds);
    }

    static void set_selected_satellite(SatViewHost& host, std::int64_t norad_catalog_id)
    {
        host.selected_norad_catalog_id_ = norad_catalog_id;
    }
    static bool has_selection(const SatViewHost& host)
    {
        return host.selected_norad_catalog_id_.has_value();
    }

    static bool running(const SatViewHost& host)
    {
        return host.running_;
    }
    static bool paused(const SatViewHost& host)
    {
        return host.paused_;
    }
    static bool scene_pass_attached(const SatViewHost& host)
    {
        return static_cast<bool>(host.scene_pass_);
    }
    static double simulated_seconds(const SatViewHost& host)
    {
        return host.simulated_seconds_;
    }

    static SatViewProjectionMode projection_mode(const SatViewHost& host)
    {
        return host.projection_mode_;
    }
    static SatViewCameraPov camera_pov(const SatViewHost& host)
    {
        return host.camera_pov_;
    }

    static bool config_dirty(const SatViewHost& host)
    {
        return host.config_dirty_;
    }
    static bool simulation_settings_dirty(const SatViewHost& host)
    {
        return host.simulation_settings_dirty_;
    }
    static bool track_buffer_dirty(const SatViewHost& host)
    {
        return host.track_buffer_dirty_;
    }
    static bool marker_buffer_dirty(const SatViewHost& host)
    {
        return host.marker_buffer_dirty_;
    }
};

// Minimal bundled sample data mirrored from the *_service_tests so the fake
// transports return well-formed payloads offline.
inline constexpr std::string_view kSatViewFixtureGpJson = R"json([
  {
    "OBJECT_NAME": "SMOKE ONE",
    "OBJECT_ID": "2026-090A",
    "EPOCH": "2026-06-26T00:00:00.000000",
    "MEAN_MOTION": 15.5,
    "ECCENTRICITY": 0.0005,
    "INCLINATION": 51.6,
    "RA_OF_ASC_NODE": 120.0,
    "ARG_OF_PERICENTER": 87.0,
    "MEAN_ANOMALY": 273.0,
    "NORAD_CAT_ID": 990001
  }
])json";

inline constexpr std::string_view kSatViewFixtureSatcatCsv = "OBJECT_NAME,NORAD_CAT_ID,OBJECT_ID,OBJECT_TYPE,OPS_STATUS_CODE,OWNER,DECAY_DATE,PERIOD,"
                                                             "INCLINATION,APOGEE,PERIGEE,RCS,DATA_STATUS_CODE,ORBIT_CENTER,ORBIT_TYPE\n"
                                                             "SMOKE ONE,990001,2026-090A,PAY,+,US,,92.9,51.6,430,410,100,,EA,ORB\n";

inline std::string satview_fixture_cloud_ppm()
{
    std::string image = "P6\n2 1\n255\n";
    const char pixels[] = {
        static_cast<char>(255),
        0,
        0,
        0,
        static_cast<char>(255),
        0,
    };
    image.append(pixels, sizeof(pixels));
    return image;
}

// A fixed, obviously-synthetic epoch (2026-06-26T00:00:00Z) so the fake clock is
// never confused with a live system-clock read.
inline constexpr double kSatViewFixtureClockSeconds = 1'782'432'000.0;

// Bundles the fakes and wires a SatViewHost initialize() offline. Owns the
// renderer, callbacks, temp cache directory, and host; destroying it runs the
// host's non-blocking shutdown.
struct OfflineSatViewHost
{
    tests::TempDir cache{ "satview-host-smoke" };
    tests::TestHostCallbacks callbacks;
    tests::FakeTermRenderer renderer;
    SatViewHost host;
    // The catalog/cloud services invoke the fake fetch on their worker threads,
    // so these counters are written off the main thread — keep them atomic to
    // stay race-free when the test reads them after pumping.
    std::atomic<int> catalog_fetch_calls{ 0 };
    std::atomic<int> cloud_fetch_calls{ 0 };
    double clock_seconds = kSatViewFixtureClockSeconds;

    // Initialize the host with offline hooks. When attach_imgui is true the
    // FakeTermRenderer is also attached as the ImGui backend so draw() records
    // the full ImGui + scene-pass path. Optionally binds a config_document so
    // shutdown()'s durable persistence path can be exercised hermetically
    // (wrap the caller in tests::HomeDirRedirect for that).
    bool initialize(bool attach_imgui = true, ConfigDocument* config_document = nullptr)
    {
        SatViewHostTestAccess::install_offline_hooks(
            host,
            [this]() { return clock_seconds; },
            [this](std::string_view url, std::string& error) {
                ++catalog_fetch_calls;
                error.clear();
                if (url.find("satcat.csv") != std::string_view::npos)
                    return std::string(kSatViewFixtureSatcatCsv);
                return std::string(kSatViewFixtureGpJson);
            },
            [this](std::string_view, std::string& error) {
                ++cloud_fetch_calls;
                error.clear();
                return satview_fixture_cloud_ppm();
            },
            cache.path.string());

        HostLaunchOptions launch;
        launch.kind = HostKind::SatView;

        HostViewport viewport;
        viewport.pixel_size = { 800, 600 };
        viewport.grid_size = { 1, 1 };

        HostContext context{
            .grid_renderer = &renderer,
            .config_document = config_document,
            .launch_options = std::move(launch),
            .initial_viewport = viewport,
            .display_ppi = 96.0f,
        };

        const bool ok = host.initialize(context, callbacks);
        if (ok && attach_imgui)
            host.attach_imgui_host(renderer);
        return ok;
    }

    // Pump + draw once through the fake frame context.
    void draw_once()
    {
        IFrameContext* frame = renderer.begin_frame();
        host.draw(*frame);
        renderer.end_frame();
    }

    // Pump repeatedly with a short real sleep so the offline catalog/cloud
    // workers finish and every one-shot redraw drains, leaving the host in its
    // steady-state animation cadence.
    void pump_until_quiescent(int iterations = 60)
    {
        for (int i = 0; i < iterations; ++i)
        {
            host.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    // Pump (with a short real sleep) until `done()` holds or the bound is hit.
    // Lets a test wait on a worker-thread milestone — e.g. the offline catalog
    // fetch landing — without racing on a fixed iteration count. Returns the
    // final predicate value so callers can REQUIRE it.
    template <typename Predicate>
    bool pump_until(Predicate&& done, int max_iterations = 500)
    {
        for (int i = 0; i < max_iterations; ++i)
        {
            if (done())
                return true;
            host.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return done();
    }
};

} // namespace draxul::satview

#endif // DRAXUL_ENABLE_SATVIEW
