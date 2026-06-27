#include "satview_time_format.h"

#include <catch2/catch_test_macros.hpp>

using draxul::satview::format_local_simulation_time;
using draxul::satview::format_utc_offset;

TEST_CASE("SatView formats local UTC offsets", "[satview][time]")
{
    CHECK(format_utc_offset(0) == "UTC+00:00");
    CHECK(format_utc_offset(3600) == "UTC+01:00");
    CHECK(format_utc_offset(-5 * 3600 - 30 * 60) == "UTC-05:30");
}

TEST_CASE("SatView formats simulation epochs in the system timezone", "[satview][time]")
{
    const std::string formatted = format_local_simulation_time(1710936000.0);
    CHECK(formatted != "unavailable");
    CHECK(formatted.find("2024-03-") != std::string::npos);
    CHECK(formatted.find("UTC") != std::string::npos);
}
