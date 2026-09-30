#include <catch2/catch_test_macros.hpp>
#include "server_status_surface.h"

using namespace draxul;

TEST_CASE("server stop dialog launch rejects a missing executable",
    "[server][status-surface][lifecycle]")
{
    std::string error;
    CHECK_FALSE(launch_server_stop_dialog(
        "Z:/missing/draxul.exe", "D:/runtime", error));
    CHECK(error == "The Draxul executable is unavailable.");
}
