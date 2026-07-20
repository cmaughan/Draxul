#include <draxul/satview/satview_solar_system.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <filesystem>
#include <set>
#include <string>

using namespace draxul::satview;

TEST_CASE("SatView Solar System catalogue has unique, renderable bodies", "[satview][solar-system]")
{
    const auto bodies = satview_solar_system_bodies();
    REQUIRE(bodies.size() == 29);

    std::set<int> ids;
    std::set<std::string> config_names;
    for (const SatViewSolarSystemBody& body : bodies)
    {
        CHECK(ids.insert(static_cast<int>(body.id)).second);
        CHECK(config_names.insert(std::string(body.config_name)).second);
        CHECK_FALSE(body.name.empty());
        CHECK(body.equatorial_radius_km > 0.0);
        CHECK(body.polar_radius_km > 0.0);
        CHECK(body.polar_radius_km <= body.equatorial_radius_km);
        CHECK_FALSE(body.texture_path.empty());
        CHECK(std::filesystem::exists(
            std::filesystem::path(DRAXUL_PROJECT_ROOT)
            / "assets" / "satview" / body.texture_path));
        CHECK(satview_camera_pov_from_config_name(body.config_name) == body.id);
        if (body.parent.has_value())
            CHECK(ids.contains(static_cast<int>(*body.parent)));
    }
}

TEST_CASE("SatView approximate natural-body orbits remain bounded", "[satview][solar-system]")
{
    constexpr double kJ2000UnixSeconds = 946728000.0;
    for (const SatViewSolarSystemBody& body : satview_solar_system_bodies())
    {
        if (!body.parent.has_value())
            continue;
        const glm::dvec3 position = satview_body_position_in_parent_frame(
            body, kJ2000UnixSeconds + 123.0 * 86400.0);
        const double radius = glm::length(position);
        CHECK(std::isfinite(radius));
        CHECK(radius >= body.semi_major_axis_km * (1.0 - body.eccentricity) * 0.999);
        CHECK(radius <= body.semi_major_axis_km * (1.0 + body.eccentricity) * 1.001);

        const auto track = satview_body_orbit_in_parent_frame(body, 64);
        REQUIRE(track.size() == 64);
        for (const glm::dvec3& point : track)
            CHECK(std::isfinite(glm::length(point)));
    }
}

TEST_CASE("SatView major-moon hierarchy exposes local systems", "[satview][solar-system]")
{
    CHECK(satview_child_bodies(SatViewCameraPov::Sun).size() == 8);
    CHECK(satview_child_bodies(SatViewCameraPov::Mars).size() == 2);
    CHECK(satview_child_bodies(SatViewCameraPov::Jupiter).size() == 4);
    CHECK(satview_child_bodies(SatViewCameraPov::Saturn).size() == 7);
    CHECK(satview_child_bodies(SatViewCameraPov::Uranus).size() == 5);
    CHECK(satview_child_bodies(SatViewCameraPov::Neptune).size() == 1);
    CHECK(satview_child_bodies(SatViewCameraPov::Venus).empty());
}

TEST_CASE("SatView contextual bodies preserve real position and apparent angular size", "[satview][solar-system]")
{
    constexpr double kJ2000UnixSeconds = 946728000.0;
    const auto mercury_sun = satview_context_body_state(
        SatViewCameraPov::Mercury,
        SatViewCameraPov::Sun,
        glm::dvec3(0.0, 0.0, 4.0),
        kJ2000UnixSeconds);
    REQUIRE(mercury_sun.has_value());
    const double mercury_sun_diameter_degrees = glm::degrees(2.0 * mercury_sun->angular_radius_radians);
    CHECK(mercury_sun_diameter_degrees > 1.0);
    CHECK(mercury_sun_diameter_degrees < 2.0);
    const double mercury_sun_distance = glm::length(
        mercury_sun->position_focus_radii - glm::dvec3(0.0, 0.0, 4.0));
    CHECK(mercury_sun_distance > 20000.0);
    CHECK(mercury_sun_distance < 30000.0);

    const auto phobos_mars = satview_context_body_state(
        SatViewCameraPov::Phobos,
        SatViewCameraPov::Mars,
        glm::dvec3(0.0),
        kJ2000UnixSeconds);
    REQUIRE(phobos_mars.has_value());
    const double phobos_mars_diameter_degrees = glm::degrees(2.0 * phobos_mars->angular_radius_radians);
    CHECK(phobos_mars_diameter_degrees > 40.0);
    CHECK(phobos_mars_diameter_degrees < 45.0);
}

TEST_CASE("SatView Sun planet tracks respect per-planet visibility", "[satview][solar-system]")
{
    SatViewPlanetTrackConfig tracks;
    tracks.mercury = false;
    tracks.venus = false;
    tracks.earth = true;
    tracks.mars = false;
    tracks.jupiter = true;
    tracks.saturn = false;
    tracks.uranus = false;
    tracks.neptune = false;

    const auto selected = satview_child_orbit_tracks(
        SatViewCameraPov::Sun,
        tracks,
        32);

    REQUIRE(selected.size() == 2);
    CHECK(selected[0].body == SatViewCameraPov::Earth);
    CHECK(selected[1].body == SatViewCameraPov::Jupiter);
    for (const SatViewBodyOrbitTrack& track : selected)
    {
        CHECK(track.points_focus_radii.size() == 32);
        CHECK(track.radius_focus_radii > 0.0);
    }
}

TEST_CASE("SatView planet body views expose visible child body geometry", "[satview][solar-system]")
{
    constexpr double kJ2000UnixSeconds = 946728000.0;

    const auto saturn_moons = satview_child_body_instances(
        SatViewCameraPov::Saturn,
        kJ2000UnixSeconds);

    REQUIRE(saturn_moons.size() == 7);
    CHECK(saturn_moons.front().body == SatViewCameraPov::Mimas);
    CHECK(saturn_moons.back().body == SatViewCameraPov::Iapetus);
    for (const SatViewBodyRenderInstance& moon : saturn_moons)
    {
        CHECK(glm::length(moon.position_focus_radii) > 1.0);
        CHECK(moon.radius_focus_radii > 0.0);
        CHECK(moon.polar_radius_ratio > 0.0);
        CHECK(moon.polar_radius_ratio <= 1.0);
    }
}

TEST_CASE("SatView Saturn ring bands form a layered disk", "[satview][solar-system]")
{
    const auto bands = satview_planetary_ring_bands(SatViewCameraPov::Saturn);

    REQUIRE(bands.size() >= 4);
    bool found_dark_gap = false;
    double previous_outer = 0.0;
    for (const SatViewRingBand& band : bands)
    {
        CHECK(band.inner_radius_body_radii > previous_outer);
        CHECK(band.outer_radius_body_radii > band.inner_radius_body_radii);
        CHECK(band.color.a >= 0.0f);
        CHECK(band.color.a <= 1.0f);
        found_dark_gap = found_dark_gap || band.color.a < 0.25f;
        previous_outer = band.outer_radius_body_radii;
    }
    CHECK(found_dark_gap);
}

TEST_CASE("SatView Saturn moon views expose Saturn parent ring bands", "[satview][solar-system]")
{
    const auto titan_bands = satview_planetary_ring_bands(SatViewCameraPov::Titan);

    REQUIRE(titan_bands.size() >= 4);
    CHECK(titan_bands.front().inner_radius_body_radii > 1.0);
    CHECK(titan_bands.back().outer_radius_body_radii > titan_bands.front().outer_radius_body_radii);
}
