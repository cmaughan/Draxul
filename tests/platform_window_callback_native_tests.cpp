#include <catch2/catch_test_macros.hpp>

#include "../libs/draxul-window/src/platform_callback_test_seams.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

std::string read_source(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    std::string source(std::istreambuf_iterator<char>(input), {});
    source.erase(std::remove(source.begin(), source.end(), '\r'), source.end());
    return source;
}

std::size_t occurrence_count(std::string_view text, std::string_view needle)
{
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(needle, offset)) != std::string_view::npos)
    {
        ++count;
        offset += needle.size();
    }
    return count;
}

} // namespace

#ifdef _WIN32
TEST_CASE("Win32 tray callbacks follow the HWND user-data instance lifetime",
    "[window][win32][lifetime]")
{
    // The seam uses production tray_wnd_proc + GWLP_USERDATA with HWND_MESSAGE
    // windows, so it exercises native binding without showing a window or a
    // notification-area icon.
    const auto result = draxul::window_test::exercise_win32_tray_context_lifetime();
    REQUIRE(result.first_window_created);
    REQUIRE(result.first_window_destroyed);
    REQUIRE(result.second_window_created);
    REQUIRE(result.second_window_destroyed);

    CHECK(result.first_attach_calls == 1);
    CHECK(result.first_quit_calls == 0);
    CHECK(result.second_attach_calls == 1);
    CHECK(result.second_quit_calls == 1);
}
#endif

TEST_CASE("macOS Dock handler source contract installs and uninstalls one owned handler",
    "[window][macos][lifetime]")
{
    const auto source = read_source(std::filesystem::path(DRAXUL_PROJECT_ROOT)
        / "libs" / "draxul-window" / "src" / "sdl_window_macos.mm");
    REQUIRE_FALSE(source.empty());

    const auto install = source.find("void* install_dock_reopen_handler");
    const auto allocate = source.find("[[DraxulDockReopenHandler alloc] init]", install);
    const auto set_handler = source.find("setEventHandler:context->handler", install);
    REQUIRE(install != std::string::npos);
    REQUIRE(allocate != std::string::npos);
    REQUIRE(set_handler != std::string::npos);
    CHECK(install < allocate);
    CHECK(allocate < set_handler);

    const auto uninstall = source.find("void uninstall_dock_reopen_handler");
    const auto remove_handler = source.find("removeEventHandlerForEventClass", uninstall);
    const auto clear_callback = source.find("context->handler.callback = nil", uninstall);
    const auto release_handler = source.find("context->handler = nil", uninstall);
    REQUIRE(uninstall != std::string::npos);
    REQUIRE(remove_handler != std::string::npos);
    REQUIRE(clear_callback != std::string::npos);
    REQUIRE(release_handler != std::string::npos);
    CHECK(uninstall < remove_handler);
    CHECK(remove_handler < clear_callback);
    CHECK(clear_callback < release_handler);

    CHECK(source.find("std::make_unique<DockReopenContext>", install) != std::string::npos);
    CHECK(occurrence_count(source, "setEventHandler:") == 1);
    CHECK(occurrence_count(source, "removeEventHandlerForEventClass:") == 1);
}
