#include <draxul/satview/satview_lunar_surface_catalog.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <ranges>

namespace
{

using namespace draxul::satview;

TEST_CASE("SatView loads the reviewed lunar surface catalogue", "[satview][moon][catalog]")
{
    const SatViewLunarSurfaceCatalog catalog = load_satview_lunar_surface_catalog();

    INFO(catalog.error);
    REQUIRE(catalog.error.empty());
    CHECK(catalog.objects.size() == 70);
    CHECK(catalog.site_count == 46);
    CHECK(std::ranges::all_of(catalog.objects, &SatViewLunarSurfaceObject::renderable));
    CHECK(std::ranges::count(catalog.objects, true, &SatViewLunarSurfaceObject::site_representative)
        == 46);
}

TEST_CASE("SatView loads the curated Mars surface catalogue", "[satview][mars][catalog]")
{
    const SatViewSurfaceCatalog catalog = load_satview_mars_surface_catalog();

    INFO(catalog.error);
    REQUIRE(catalog.error.empty());
    CHECK(catalog.body == CentralBody::Mars);
    CHECK(catalog.objects.size() == 9);
    CHECK(catalog.site_count == 9);
    CHECK(std::ranges::all_of(catalog.objects, &SatViewSurfaceObject::renderable));
    CHECK(std::ranges::all_of(catalog.objects, [](const SatViewSurfaceObject& object) {
        return object.body == CentralBody::Mars;
    }));
    CHECK(std::ranges::count(catalog.objects, SatViewSurfaceKind::Rover,
              &SatViewSurfaceObject::kind)
        == 4);
}

TEST_CASE("SatView lunar catalogue retains the reviewed IM-2 east longitude", "[satview][moon][catalog]")
{
    const SatViewLunarSurfaceCatalog catalog = load_satview_lunar_surface_catalog();
    const auto found = std::ranges::find(catalog.objects, std::string("im-2-im-2"),
        &SatViewLunarSurfaceObject::id);

    REQUIRE(found != catalog.objects.end());
    CHECK(found->longitude_east_degrees > 29.19);
    CHECK(found->longitude_east_degrees < 29.20);
    CHECK(found->location_quality == SatViewLunarLocationQuality::Confirmed);
}

TEST_CASE("SatView rejects lunar surface rows without stable identifiers", "[satview][moon][catalog]")
{
    const auto parsed = parse_satview_lunar_surface_catalog(
        "ID,MISSION_ID,MISSION_NAME,PARENT_SITE_ID,DISPLAY_NAME,KIND,"
        "LATITUDE_DEGREES,LONGITUDE_EAST_DEGREES,LOCATION_QUALITY\n"
        ",apollo-11,Apollo 11,apollo-11-site-01,Eagle,lander,0.67,23.47,confirmed\n");

    CHECK(parsed.objects.empty());
    CHECK_FALSE(parsed.error.empty());
}

TEST_CASE("SatView retains site-only inventory without fabricating coordinates", "[satview][moon][catalog]")
{
    const auto parsed = parse_satview_lunar_surface_catalog(
        "ID,MISSION_ID,MISSION_NAME,PARENT_SITE_ID,DISPLAY_NAME,KIND,"
        "LATITUDE_DEGREES,LONGITUDE_EAST_DEGREES,LOCATION_QUALITY\n"
        "apollo-tool,apollo-11,Apollo 11,apollo-11-site-01,Tool,crewed_artifact,,,site_only\n");

    INFO(parsed.error);
    REQUIRE(parsed.error.empty());
    REQUIRE(parsed.objects.size() == 1);
    CHECK_FALSE(parsed.objects.front().renderable());
    CHECK(parsed.objects.front().location_quality == SatViewLunarLocationQuality::SiteOnly);
}

TEST_CASE("SatView generic surface parser stamps the requested central body", "[satview][catalog][surface]")
{
    const auto parsed = parse_satview_surface_catalog(
        "ID,MISSION_ID,MISSION_NAME,PARENT_SITE_ID,DISPLAY_NAME,KIND,"
        "LATITUDE_DEGREES,LONGITUDE_EAST_DEGREES,LOCATION_QUALITY\n"
        "mars-test,mars-test,Mars Test,mars-test-site-01,Mars Test,lander,1.0,2.0,reported\n",
        CentralBody::Mars);

    INFO(parsed.error);
    REQUIRE(parsed.error.empty());
    REQUIRE(parsed.objects.size() == 1);
    CHECK(parsed.body == CentralBody::Mars);
    CHECK(parsed.objects.front().body == CentralBody::Mars);
}

} // namespace
