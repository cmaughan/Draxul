#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <draxul/satview/satview_catalog.h>
#include <draxul/satview/satview_propagation.h>
#include <glm/geometric.hpp>

using Catch::Approx;
using draxul::satview::SatellitePropagationSettings;
using draxul::satview::build_satellite_propagation_model;
using draxul::satview::parse_celestrak_epoch_utc;
using draxul::satview::parse_celestrak_gp_json;
using draxul::satview::propagate_satellites;

namespace
{

const char* kVallado00005Json = R"json([
  {
    "OBJECT_NAME": "VANGUARD 1",
    "OBJECT_ID": "1958-002B",
    "EPOCH": "2000-06-27T18:50:19.733568",
    "MEAN_MOTION": 10.82419157,
    "ECCENTRICITY": 0.1859667,
    "INCLINATION": 34.2682,
    "RA_OF_ASC_NODE": 348.7242,
    "ARG_OF_PERICENTER": 331.7664,
    "MEAN_ANOMALY": 19.3264,
    "EPHEMERIS_TYPE": 0,
    "CLASSIFICATION_TYPE": "U",
    "NORAD_CAT_ID": 5,
    "ELEMENT_SET_NO": 475,
    "REV_AT_EPOCH": 41366,
    "BSTAR": 0.000028098,
    "MEAN_MOTION_DOT": 0.00000023,
    "MEAN_MOTION_DDOT": 0.0
  }
])json";

void check_vec3(
    const glm::dvec3& actual,
    double x,
    double y,
    double z,
    double margin)
{
    CHECK(actual.x == Approx(x).margin(margin));
    CHECK(actual.y == Approx(y).margin(margin));
    CHECK(actual.z == Approx(z).margin(margin));
}

} // namespace

TEST_CASE("SatView propagation parses CelesTrak UTC epochs", "[satview][propagation]")
{
    const auto epoch = parse_celestrak_epoch_utc("2000-06-27T18:50:19.733568Z");
    REQUIRE(epoch.has_value());
    CHECK(*epoch == Approx(962132419.733568).margin(0.0005));

    CHECK_FALSE(parse_celestrak_epoch_utc("2000-02-30T00:00:00").has_value());
    CHECK_FALSE(parse_celestrak_epoch_utc("not an epoch").has_value());
}

TEST_CASE("SatView propagation matches Vallado SGP4 verification case 00005", "[satview][propagation]")
{
    const auto catalog = parse_celestrak_gp_json(kVallado00005Json, "vallado", "AIAA-2006-6753");
    REQUIRE(catalog);

    auto build = build_satellite_propagation_model(catalog.catalog);
    REQUIRE(build);
    REQUIRE(build.compiled_records == 1);
    CHECK(build.skipped_records == 0);
    CHECK(build.model.size() == 1);

    const double epoch_seconds = *parse_celestrak_epoch_utc("2000-06-27T18:50:19.733568");

    auto at_epoch = propagate_satellites(build.model, epoch_seconds);
    REQUIRE(at_epoch);
    REQUIRE(at_epoch.states.size() == 1);
    check_vec3(at_epoch.states[0].teme_position_km, 7022.46529266, -1400.08296755, 0.03995155, 0.001);
    check_vec3(at_epoch.states[0].teme_velocity_km_per_s, 1.893841015, 6.405893759, 4.534807250, 0.000001);

    auto after_six_hours = propagate_satellites(build.model, epoch_seconds + 21600.0);
    REQUIRE(after_six_hours);
    REQUIRE(after_six_hours.states.size() == 1);
    check_vec3(after_six_hours.states[0].teme_position_km, -7154.03120202, -3783.17682504, -3536.19412294, 0.001);
    check_vec3(after_six_hours.states[0].teme_velocity_km_per_s, 4.741887409, -4.151817765, -2.093935425, 0.000001);
}

TEST_CASE("SatView propagation generates configurable track samples", "[satview][propagation]")
{
    const auto catalog = parse_celestrak_gp_json(kVallado00005Json, "vallado", "AIAA-2006-6753");
    REQUIRE(catalog);
    auto build = build_satellite_propagation_model(catalog.catalog);
    REQUIRE(build);

    SatellitePropagationSettings settings;
    settings.track_sample_count = 12;
    settings.track_satellite_limit = 1;

    const double epoch_seconds = *parse_celestrak_epoch_utc("2000-06-27T18:50:19.733568");
    auto result = propagate_satellites(build.model, epoch_seconds, settings);

    REQUIRE(result);
    REQUIRE(result.states.size() == 1);
    REQUIRE(result.tracks.size() == 1);
    CHECK(result.tracks[0].ecef_points_km.size() == 12);
    CHECK(result.tracks[0].render_points_earth_radii.size() == 12);
    CHECK(glm::length(result.tracks[0].render_points_earth_radii[0]) > 1.0);
}

TEST_CASE("SatView propagation includes selected track outside general track cap", "[satview][propagation]")
{
    const std::string json = R"json([
      {
        "OBJECT_NAME": "VANGUARD 1",
        "OBJECT_ID": "1958-002B",
        "EPOCH": "2000-06-27T18:50:19.733568",
        "MEAN_MOTION": 10.82419157,
        "ECCENTRICITY": 0.1859667,
        "INCLINATION": 34.2682,
        "RA_OF_ASC_NODE": 348.7242,
        "ARG_OF_PERICENTER": 331.7664,
        "MEAN_ANOMALY": 19.3264,
        "EPHEMERIS_TYPE": 0,
        "CLASSIFICATION_TYPE": "U",
        "NORAD_CAT_ID": 5,
        "ELEMENT_SET_NO": 475,
        "REV_AT_EPOCH": 41366,
        "BSTAR": 0.000028098,
        "MEAN_MOTION_DOT": 0.00000023,
        "MEAN_MOTION_DDOT": 0.0
      },
      {
        "OBJECT_NAME": "VANGUARD 1 CLONE",
        "OBJECT_ID": "1958-002C",
        "EPOCH": "2000-06-27T18:50:19.733568",
        "MEAN_MOTION": 10.82419157,
        "ECCENTRICITY": 0.1859667,
        "INCLINATION": 34.2682,
        "RA_OF_ASC_NODE": 348.7242,
        "ARG_OF_PERICENTER": 331.7664,
        "MEAN_ANOMALY": 19.3264,
        "EPHEMERIS_TYPE": 0,
        "CLASSIFICATION_TYPE": "U",
        "NORAD_CAT_ID": 6,
        "ELEMENT_SET_NO": 475,
        "REV_AT_EPOCH": 41366,
        "BSTAR": 0.000028098,
        "MEAN_MOTION_DOT": 0.00000023,
        "MEAN_MOTION_DDOT": 0.0
      }
    ])json";
    const auto catalog = parse_celestrak_gp_json(json, "selected", "test");
    REQUIRE(catalog);
    auto build = build_satellite_propagation_model(catalog.catalog);
    REQUIRE(build);

    SatellitePropagationSettings settings;
    settings.track_sample_count = 8;
    settings.track_satellite_limit = 1;
    settings.selected_track_norad_catalog_id = 6;

    const double epoch_seconds = *parse_celestrak_epoch_utc("2000-06-27T18:50:19.733568");
    auto result = propagate_satellites(build.model, epoch_seconds, settings);

    REQUIRE(result);
    REQUIRE(result.tracks.size() == 2);
    CHECK(result.tracks[0].norad_catalog_id == 5);
    CHECK(result.tracks[1].norad_catalog_id == 6);
    CHECK(result.tracks[1].ecef_points_km.size() == 8);
}
