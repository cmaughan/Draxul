#include <catch2/catch_test_macros.hpp>

#include <draxul/plugin_manager.h>
#include <draxul/plugin_host.h>
#include <draxul/events.h>
#include "support/test_host_callbacks.h"

#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{

class TempPlugins
{
public:
    TempPlugins()
    {
        root = std::filesystem::temp_directory_path()
            / ("draxul-plugin-test-"
                + std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::create_directories(root);
    }
    ~TempPlugins()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    std::filesystem::path root;
};

void install_plugin(const std::filesystem::path& tier,
    std::string_view directory_name, std::string_view id,
    const std::filesystem::path& library,
    std::string_view manifest_library = {})
{
    const auto directory = tier / directory_name;
    std::filesystem::create_directories(directory);
    const std::string library_name = manifest_library.empty()
        ? library.filename().string()
        : std::string(manifest_library);
    if (!library.empty() && manifest_library.empty())
    {
        std::filesystem::copy_file(library,
            directory / library.filename(),
            std::filesystem::copy_options::overwrite_existing);
    }
    std::ofstream manifest(directory / "plugin.toml");
    manifest << "schema_version = 1\n"
             << "id = \"" << id << "\"\n"
             << "name = \"Fixture\"\n"
             << "version = \"1.0.0\"\n"
             << "abi_version = 2\n"
#ifdef _WIN32
             << "[platform.windows]\n"
#elif defined(__APPLE__)
             << "[platform.macos]\n"
#else
             << "[platform.linux]\n"
#endif
             << "library = \"" << library_name << "\"\n";
}

} // namespace

TEST_CASE("PluginHost translates SDK-owned input through a real module",
    "[plugin][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "input", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    const auto manager = draxul::PluginManager::discover(bundled, user);
    std::string error;
    const auto loaded = manager->load("dev.draxul.fixture", error);
    REQUIRE(loaded);
#ifdef _WIN32
    HMODULE fixture = LoadLibraryW(loaded->manifest().library_path.c_str());
    REQUIRE(fixture);
    const auto symbol = [&](const char* name) {
        return reinterpret_cast<void*>(GetProcAddress(fixture, name));
    };
#else
    void* fixture = dlopen(loaded->manifest().library_path.c_str(),
        RTLD_NOW | RTLD_LOCAL);
    REQUIRE(fixture);
    const auto symbol = [&](const char* name) { return dlsym(fixture, name); };
#endif
    const auto reset = reinterpret_cast<void (*)()>(
        symbol("draxul_fixture_reset_events"));
    const auto count = reinterpret_cast<size_t (*)()>(
        symbol("draxul_fixture_event_count"));
    const auto event_at = reinterpret_cast<int (*)(size_t,
        DraxulPluginInputEventV2*)>(
        symbol("draxul_fixture_event_at"));
    const auto fixture_request_tick = reinterpret_cast<void (*)()>(
        symbol("draxul_fixture_request_tick"));
    const auto fixture_tick_count = reinterpret_cast<size_t (*)()>(
        symbol("draxul_fixture_tick_count"));
    const auto fixture_quiesce_count = reinterpret_cast<size_t (*)()>(
        symbol("draxul_fixture_quiesce_count"));
    const auto fixture_action_count = reinterpret_cast<size_t (*)()>(
        symbol("draxul_fixture_action_dispatch_count"));
    REQUIRE(reset);
    REQUIRE(count);
    REQUIRE(event_at);
    REQUIRE(fixture_request_tick);
    REQUIRE(fixture_tick_count);
    REQUIRE(fixture_quiesce_count);
    REQUIRE(fixture_action_count);
    reset();

    draxul::PluginHost host(manager);
    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.client_plugin_id = "dev.draxul.fixture";
    context.launch_options.client_plugin_config_json = "{}";
    context.initial_viewport.pixel_pos = { 100, 50 };
    context.initial_viewport.pixel_size = { 640, 360 };
    draxul::tests::TestHostCallbacks callbacks;
    REQUIRE(host.initialize(context, callbacks));
    CHECK(host.display_name() == "Fixture instance");
    CHECK(host.status_text() == "ready");
    CHECK(host.runtime_state().content_ready);
    CHECK(host.default_background().r == 0.1f);
    CHECK(host.mouse_cursor_at(10, 10) == draxul::MouseCursor::Pointer);
    CHECK(host.print_hint().content_pos == glm::ivec2(1, 2));
    CHECK(host.print_hint().content_size == glm::ivec2(30, 40));
    CHECK(host.print_hint().paper_white);

    // The initial logic deadline invokes the real module's tick callback and
    // converts its redraw result into a host frame request.
    REQUIRE(host.next_deadline().has_value());
    const int frames_before_initial_tick = callbacks.request_frame_calls;
    host.pump();
    CHECK(fixture_tick_count() == 1);
    CHECK(callbacks.request_frame_calls == frames_before_initial_tick + 1);
    CHECK_FALSE(host.next_deadline().has_value());

    // A module callback requests logic work without directly requesting a
    // render. PluginHost wakes, ticks on the main thread, then honors the
    // tick result's redraw bit.
    const int wakes_before_tick = callbacks.wake_window_calls;
    fixture_request_tick();
    CHECK(callbacks.wake_window_calls == wakes_before_tick + 1);
    host.pump();
    CHECK(fixture_tick_count() == 2);

    // Render scheduling remains independent from logic scheduling.
    host.accept_render_result({ sizeof(DraxulPluginRenderResultV2),
        1'000'000, 1, nullptr });
    CHECK(host.next_deadline().has_value());
    host.set_presentation_visible(false);
    CHECK(host.status_text() == "hidden");
    REQUIRE(host.next_deadline().has_value());
    host.pump();
    CHECK_FALSE(host.next_deadline().has_value());
    const int frames_before_show = callbacks.request_frame_calls;
    host.set_presentation_visible(true);
    CHECK(callbacks.request_frame_calls > frames_before_show);
    REQUIRE(host.next_deadline().has_value());
    host.pump();
    CHECK_FALSE(host.next_deadline().has_value());
    host.accept_render_result({ sizeof(DraxulPluginRenderResultV2),
        0, 1, nullptr });
    const int frames_before_deadline = callbacks.request_frame_calls;
    REQUIRE(host.next_deadline().has_value());
    host.pump();
    CHECK(callbacks.request_frame_calls
        == frames_before_deadline + 1);
    CHECK_FALSE(host.next_deadline().has_value());
    CHECK(host.dispatch_action("fixture_action"));
    CHECK_FALSE(host.dispatch_action("unknown"));
    CHECK(fixture_action_count() == 1);
    const int frames_before_metadata = callbacks.request_frame_calls;
    host.pump();
    CHECK(callbacks.request_frame_calls == frames_before_metadata + 1);
    host.on_focus_gained();
    CHECK(host.status_text().find("focused") != std::string::npos);
    host.on_key({ 44, 32, draxul::kModCtrl, true });
    host.on_text_input({ "hello" });
    host.on_text_editing({ "compose", 2, 3 });
    host.on_mouse_button({ 1, true, draxul::kModShift,
        { 125, 90 }, 2 });
    host.on_mouse_move({ draxul::kModAlt, { 130, 100 },
        { 5.0f, 10.0f }, 1 });
    host.on_mouse_wheel({ { 1.0f, -2.0f }, draxul::kModNone,
        { 140, 110 } });
    host.on_focus_lost();

    REQUIRE(count() == 8);
    DraxulPluginInputEventV2 event{};
    REQUIRE(event_at(1, &event));
    CHECK(event.kind == DRAXUL_PLUGIN_INPUT_KEY);
    CHECK(event.physical_key == 44);
    CHECK(event.logical_key == 32);
    CHECK(event.modifiers == draxul::kModCtrl);
    REQUIRE(event_at(2, &event));
    CHECK(std::string(event.text_utf8, event.text_length) == "hello");
    REQUIRE(event_at(3, &event));
    CHECK(event.kind == DRAXUL_PLUGIN_INPUT_COMPOSITION);
    CHECK(event.composition_start == 2);
    CHECK(event.composition_length == 3);
    REQUIRE(event_at(4, &event));
    CHECK(event.kind == DRAXUL_PLUGIN_INPUT_POINTER_BUTTON);
    CHECK(event.x == 25);
    CHECK(event.y == 40);
    CHECK(event.clicks == 2);
    REQUIRE(event_at(5, &event));
    CHECK(event.kind == DRAXUL_PLUGIN_INPUT_POINTER_MOVE);
    CHECK(event.delta_x == 5.0f);
    CHECK(event.buttons == 1);
    REQUIRE(event_at(6, &event));
    CHECK(event.kind == DRAXUL_PLUGIN_INPUT_WHEEL);
    CHECK(event.delta_y == -2.0f);
    host.shutdown();
    CHECK(fixture_quiesce_count() == 1);
#ifdef _WIN32
    FreeLibrary(fixture);
#else
    dlclose(fixture);
#endif
}

TEST_CASE("plugin manager loads and caches a real native module",
    "[plugin][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "valid", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    const auto manager = draxul::PluginManager::discover(bundled, user);
    std::string error;
    const auto first = manager->load("dev.draxul.fixture", error);
    REQUIRE(first);
    REQUIRE(error.empty());
    const auto second = manager->load("dev.draxul.fixture", error);
    CHECK(second == first);
    CHECK(first->api().supported_backends
        == (DRAXUL_PLUGIN_BACKEND_VULKAN
            | DRAXUL_PLUGIN_BACKEND_METAL));
}

TEST_CASE("PluginHost rejects a module unsupported by the active backend",
    "[plugin][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "unsupported", "dev.draxul.fixture",
        DRAXUL_FIXTURE_UNSUPPORTED_PATH);
    const auto manager = draxul::PluginManager::discover(bundled, user);
    draxul::PluginHost host(manager);
    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.client_plugin_id = "dev.draxul.fixture";
    draxul::tests::TestHostCallbacks callbacks;
    CHECK_FALSE(host.initialize(context, callbacks));
    CHECK(host.init_error().find("does not support")
        != std::string::npos);
}

TEST_CASE("plugin manager reports malformed dynamic modules",
    "[plugin][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "missing", "dev.draxul.missing", {},
        "missing-plugin.dll");
    install_plugin(bundled, "symbol", "dev.draxul.no-symbol",
        DRAXUL_FIXTURE_NO_SYMBOL_PATH);
    install_plugin(bundled, "abi", "dev.draxul.fixture",
        DRAXUL_FIXTURE_BAD_ABI_PATH);
    install_plugin(bundled, "identity", "dev.draxul.expected",
        DRAXUL_FIXTURE_WRONG_ID_PATH);
    const auto manager = draxul::PluginManager::discover(bundled, user);
    std::string error;
    CHECK_FALSE(manager->load("dev.draxul.missing", error));
    CHECK(error.find("missing") != std::string::npos);
    error.clear();
    CHECK_FALSE(manager->load("dev.draxul.no-symbol", error));
    CHECK(error.find("draxul_plugin_query") != std::string::npos);
    error.clear();
    CHECK_FALSE(manager->load("dev.draxul.fixture", error));
    CHECK(error.find("ABI") != std::string::npos);
    error.clear();
    CHECK_FALSE(manager->load("dev.draxul.expected", error));
    CHECK(error.find("identity") != std::string::npos);
}

TEST_CASE("user plugin tier overrides bundled plugin and duplicate ids are invalid",
    "[plugin][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "bundled", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    install_plugin(user, "override", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    auto manager = draxul::PluginManager::discover(bundled, user);
    REQUIRE(manager->find("dev.draxul.fixture"));
    CHECK(manager->find("dev.draxul.fixture")->user_installed);

    install_plugin(user, "duplicate", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    manager = draxul::PluginManager::discover(bundled, user);
    REQUIRE(manager->find("dev.draxul.fixture"));
    CHECK(manager->find("dev.draxul.fixture")->error.find("Duplicate")
        != std::string::npos);
}
