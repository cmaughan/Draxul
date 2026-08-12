#include <catch2/catch_test_macros.hpp>

#include <draxul/plugin_manager.h>
#include <draxul/plugin_host.h>
#include <draxul/events.h>
#include <draxul/imgui_host.h>
#include "support/test_host_callbacks.h"

#include <imgui.h>

#include <filesystem>
#include <fstream>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{

class RecordingImGuiHost final : public draxul::IImGuiHost
{
public:
    bool initialize_imgui_backend() override
    {
        ++initialize_count;
        return true;
    }
    void shutdown_imgui_backend() override { ++shutdown_count; }
    void rebuild_imgui_font_texture() override { ++font_rebuild_count; }
    void begin_imgui_frame() override { ++begin_count; }
    bool render_imgui_draw_data(const ImDrawData*, ImGuiContext*) override
    {
        ++render_count;
        return true;
    }

    int initialize_count = 0;
    int shutdown_count = 0;
    int font_rebuild_count = 0;
    int begin_count = 0;
    int render_count = 0;
};

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
    std::string_view manifest_library = {},
    std::string_view name = "Fixture",
    std::string_view version = "1.0.0")
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
             << "name = \"" << name << "\"\n"
             << "version = \"" << version << "\"\n"
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

TEST_CASE("spinning triangle persists pane-local state through host services",
    "[plugin][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "triangle", "dev.draxul.spinning-triangle",
        DRAXUL_TRIANGLE_PLUGIN_PATH, {}, "Spinning Triangle", "0.2.0");
    const auto manager = draxul::PluginManager::discover(bundled, user);

    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.client_plugin_id
        = "dev.draxul.spinning-triangle";
    context.launch_options.client_plugin_config_json
        = R"({"remember_state":true})";
    context.pane_id = "pane-a";
    context.initial_viewport.pixel_size = { 640, 360 };

    const auto first_ui_root = temp.root / "ui-a";
    {
        draxul::PluginHost host(manager, first_ui_root);
        draxul::tests::TestHostCallbacks callbacks;
        REQUIRE(host.initialize(context, callbacks));
        CHECK(host.status_text().find("running clockwise")
            != std::string::npos);
        CHECK(host.status_text().find("remembered") != std::string::npos);
        CHECK(host.status_text().find("paths ready") != std::string::npos);
        CHECK(host.dispatch_action("reverse"));
        CHECK(host.dispatch_action("toggle_pause"));
        CHECK(host.status_text().find("paused counter-clockwise")
            != std::string::npos);
        host.shutdown();
    }

    // Recreating the client-local host for the same server pane restores its
    // state without changing the pane's shared launch configuration.
    {
        draxul::PluginHost host(manager, first_ui_root);
        draxul::tests::TestHostCallbacks callbacks;
        REQUIRE(host.initialize(context, callbacks));
        CHECK(host.status_text().find("paused counter-clockwise")
            != std::string::npos);
        host.shutdown();
    }

    // Another attached UI resolves the same topology against independent
    // local storage and therefore begins with the launch defaults.
    {
        draxul::PluginHost host(manager, temp.root / "ui-b");
        draxul::tests::TestHostCallbacks callbacks;
        REQUIRE(host.initialize(context, callbacks));
        CHECK(host.status_text().find("running clockwise")
            != std::string::npos);
        host.shutdown();
    }

    // Corrupt local state is an instance-local warning, not a failed shared
    // pane. The plugin remains interactive and can replace the bad document.
    const auto corrupt_root = temp.root / "ui-corrupt";
    const auto corrupt_file = corrupt_root / "config"
        / "dev.draxul.spinning-triangle" / "panes" / "pane-a"
        / "state.json";
    std::filesystem::create_directories(corrupt_file.parent_path());
    {
        std::ofstream output(corrupt_file);
        output << "not-json";
    }
    {
        draxul::PluginHost host(manager, corrupt_root);
        draxul::tests::TestHostCallbacks callbacks;
        REQUIRE(host.initialize(context, callbacks));
        CHECK(host.status_text().find("saved state is corrupt")
            != std::string::npos);
        CHECK(host.dispatch_action("toggle_pause"));
        host.shutdown();
    }
}

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
    const auto fixture_query_service = reinterpret_cast<int (*)(const char*,
        size_t, uint32_t, void*, size_t)>(
        symbol("draxul_fixture_query_host_service"));
    REQUIRE(reset);
    REQUIRE(count);
    REQUIRE(event_at);
    REQUIRE(fixture_request_tick);
    REQUIRE(fixture_tick_count);
    REQUIRE(fixture_quiesce_count);
    REQUIRE(fixture_action_count);
    REQUIRE(fixture_query_service);
    reset();

    draxul::PluginHost host(manager, temp.root / "storage");
    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.client_plugin_id = "dev.draxul.fixture";
    context.launch_options.client_plugin_config_json = "{}";
    context.pane_id = "fixture-pane";
    context.initial_viewport.pixel_pos = { 100, 50 };
    context.initial_viewport.pixel_size = { 640, 360 };
    draxul::tests::TestHostCallbacks callbacks;
    REQUIRE(host.initialize(context, callbacks));

    DraxulPluginPathServiceV2 paths{};
    REQUIRE(fixture_query_service(DRAXUL_PLUGIN_PATH_SERVICE_ID,
        std::strlen(DRAXUL_PLUGIN_PATH_SERVICE_ID),
        DRAXUL_PLUGIN_PATH_SERVICE_VERSION, &paths, sizeof(paths)));
    size_t path_size = 0;
    REQUIRE(paths.get_path(paths.service_context, DRAXUL_PLUGIN_PATH_DATA,
        nullptr, &path_size));
    REQUIRE(path_size > 1);
    std::string data_path(path_size, '\0');
    REQUIRE(paths.get_path(paths.service_context, DRAXUL_PLUGIN_PATH_DATA,
        data_path.data(), &path_size));
    CHECK(std::filesystem::exists(std::filesystem::u8path(data_path.c_str())));

    DraxulPluginStorageServiceV2 storage{};
    REQUIRE(fixture_query_service(DRAXUL_PLUGIN_STORAGE_SERVICE_ID,
        std::strlen(DRAXUL_PLUGIN_STORAGE_SERVICE_ID),
        DRAXUL_PLUGIN_STORAGE_SERVICE_VERSION, &storage, sizeof(storage)));

    DraxulPluginImGuiOverlayServiceV2 overlay{};
    REQUIRE(fixture_query_service(DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_ID,
        std::strlen(DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_ID),
        DRAXUL_PLUGIN_IMGUI_OVERLAY_SERVICE_VERSION,
        &overlay, sizeof(overlay)));
    CHECK(overlay.imgui_version_num == IMGUI_VERSION_NUM);
    CHECK(overlay.draw_vert_size == sizeof(ImDrawVert));
    CHECK(overlay.draw_idx_size == sizeof(ImDrawIdx));
    CHECK_FALSE(overlay.initialize(overlay.service_context, nullptr));

    RecordingImGuiHost imgui_host;
    host.set_imgui_font("fixture-font.ttf", 17.0f);
    host.attach_imgui_host(imgui_host);
    ImGuiContext* overlay_context = ImGui::CreateContext();
    REQUIRE(overlay_context);
    REQUIRE(overlay.initialize(overlay.service_context, overlay_context));
    REQUIRE(overlay.begin_frame(overlay.service_context, overlay_context));
    REQUIRE(overlay.render_draw_data(overlay.service_context,
        reinterpret_cast<void*>(1), overlay_context));
    size_t font_path_size = 0;
    float font_size = 0.0f;
    REQUIRE(overlay.get_font(overlay.service_context, nullptr,
        &font_path_size, &font_size));
    std::string font_path(font_path_size, '\0');
    REQUIRE(overlay.get_font(overlay.service_context, font_path.data(),
        &font_path_size, &font_size));
    CHECK(std::string_view(font_path.c_str()) == "fixture-font.ttf");
    CHECK(font_size == 17.0f);
    overlay.rebuild_font_texture(overlay.service_context, overlay_context);
    overlay.shutdown(overlay.service_context, overlay_context);
    CHECK(imgui_host.initialize_count == 1);
    CHECK(imgui_host.begin_count == 1);
    CHECK(imgui_host.render_count == 1);
    CHECK(imgui_host.font_rebuild_count == 1);
    CHECK(imgui_host.shutdown_count == 1);
    ImGui::DestroyContext(overlay_context);
    constexpr std::string_view state_key = "fixture-state";
    constexpr std::string_view first_json = R"({"value":1})";
    constexpr std::string_view second_json = R"({"value":2})";
    CHECK(storage.write_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
        first_json.data(), first_json.size()) == DRAXUL_PLUGIN_STORAGE_OK);
    CHECK(storage.write_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
        second_json.data(), second_json.size()) == DRAXUL_PLUGIN_STORAGE_OK);
    size_t state_size = 0;
    REQUIRE(storage.read_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
        nullptr, &state_size) == DRAXUL_PLUGIN_STORAGE_OK);
    std::string saved_state(state_size, '\0');
    REQUIRE(storage.read_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
        saved_state.data(), &state_size) == DRAXUL_PLUGIN_STORAGE_OK);
    CHECK(std::string_view(saved_state.c_str()) == second_json);
    CHECK(storage.write_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, "../escape", 9,
        second_json.data(), second_json.size())
        == DRAXUL_PLUGIN_STORAGE_INVALID_KEY);
    CHECK(storage.write_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
        "bad", 3) == DRAXUL_PLUGIN_STORAGE_INVALID_JSON);
    std::string oversized(DRAXUL_PLUGIN_MAX_STORAGE_JSON_BYTES + 1, ' ');
    CHECK(storage.write_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
        oversized.data(), oversized.size())
        == DRAXUL_PLUGIN_STORAGE_TOO_LARGE);
    uint32_t worker_result = DRAXUL_PLUGIN_STORAGE_OK;
    std::thread worker([&] {
        size_t size = 0;
        worker_result = storage.read_json(storage.service_context,
            DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
            nullptr, &size);
    });
    worker.join();
    CHECK(worker_result == DRAXUL_PLUGIN_STORAGE_WRONG_THREAD);
    CHECK(storage.remove(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size())
        == DRAXUL_PLUGIN_STORAGE_OK);
    state_size = 0;
    CHECK(storage.read_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
        nullptr, &state_size) == DRAXUL_PLUGIN_STORAGE_NOT_FOUND);
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
