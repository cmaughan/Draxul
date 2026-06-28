#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <draxul/satview/satview_catalog.h>
#include <draxul/satview/satview_propagation.h>
#include <glm/geometric.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

using Catch::Approx;
using draxul::satview::SatellitePropagationExecutor;
using draxul::satview::SatellitePropagationSettings;
using draxul::satview::build_satellite_propagation_model;
using draxul::satview::greenwich_sidereal_angle_radians;
using draxul::satview::kSatViewEarthEquatorialRadiusKm;
using draxul::satview::parse_celestrak_epoch_utc;
using draxul::satview::parse_celestrak_gp_json;
using draxul::satview::propagate_satellites;
using draxul::satview::solar_direction_render;
using draxul::satview::solar_direction_teme;
using draxul::satview::teme_position_to_render_earth_radii;

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

double unwrapped_longitude_span(const std::vector<glm::dvec3>& points)
{
    if (points.empty())
        return 0.0;

    constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
    double previous = std::atan2(points.front().y, points.front().x);
    double unwrapped = previous;
    double minimum = unwrapped;
    double maximum = unwrapped;
    for (std::size_t i = 1; i < points.size(); ++i)
    {
        const double longitude = std::atan2(points[i].y, points[i].x);
        unwrapped += std::remainder(longitude - previous, kTwoPi);
        previous = longitude;
        minimum = std::min(minimum, unwrapped);
        maximum = std::max(maximum, unwrapped);
    }
    return maximum - minimum;
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

TEST_CASE("SatView TEME coordinates align with the rendered Earth", "[satview][propagation][coordinates]")
{
    constexpr double kJ2000UnixSeconds = 946728000.0;
    const double sidereal_angle = greenwich_sidereal_angle_radians(kJ2000UnixSeconds);
    CHECK(sidereal_angle == Approx(4.8949612127).margin(1.0e-9));

    const auto north_pole = teme_position_to_render_earth_radii(
        glm::dvec3(0.0, 0.0, kSatViewEarthEquatorialRadiusKm));
    check_vec3(north_pole, 0.0, 1.0, 0.0, 1.0e-12);

    const auto greenwich = teme_position_to_render_earth_radii(
        kSatViewEarthEquatorialRadiusKm
        * glm::dvec3(std::cos(sidereal_angle), std::sin(sidereal_angle), 0.0));
    check_vec3(greenwich, -std::sin(sidereal_angle), 0.0, -std::cos(sidereal_angle), 1.0e-12);

    const auto east_90 = teme_position_to_render_earth_radii(
        kSatViewEarthEquatorialRadiusKm
        * glm::dvec3(-std::sin(sidereal_angle), std::cos(sidereal_angle), 0.0));
    check_vec3(east_90, -std::cos(sidereal_angle), 0.0, std::sin(sidereal_angle), 1.0e-12);
}

TEST_CASE("SatView solar direction follows UTC date and Earth rotation", "[satview][propagation][coordinates]")
{
    constexpr double kMarchEquinoxNoonUtc = 1710936000.0;
    constexpr double kJuneSolsticeNoonUtc = 1718884800.0;
    constexpr double kDecemberSolsticeNoonUtc = 1734782400.0;
    constexpr double kRadiansToDegrees = 180.0 / 3.14159265358979323846;

    const glm::dvec3 march_sun = solar_direction_teme(kMarchEquinoxNoonUtc);
    const double march_declination = std::asin(march_sun.z) * kRadiansToDegrees;
    const double march_right_ascension = std::atan2(march_sun.y, march_sun.x);
    const double march_subsolar_longitude = std::remainder(
        march_right_ascension - greenwich_sidereal_angle_radians(kMarchEquinoxNoonUtc),
        2.0 * 3.14159265358979323846)
        * kRadiansToDegrees;
    CHECK(march_declination == Approx(0.15).margin(0.2));
    CHECK(march_subsolar_longitude == Approx(1.83).margin(0.5));

    const glm::dvec3 june_sun = solar_direction_teme(kJuneSolsticeNoonUtc);
    const glm::dvec3 december_sun = solar_direction_teme(kDecemberSolsticeNoonUtc);
    CHECK(std::asin(june_sun.z) * kRadiansToDegrees == Approx(23.435).margin(0.1));
    CHECK(std::asin(december_sun.z) * kRadiansToDegrees == Approx(-23.435).margin(0.1));

    const glm::dvec3 march_render = solar_direction_render(kMarchEquinoxNoonUtc);
    check_vec3(march_render, -march_sun.y, march_sun.z, -march_sun.x, 1.0e-12);
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

    const auto& state = at_epoch.states[0];
    const glm::dvec3 render_position = teme_position_to_render_earth_radii(state.teme_position_km);
    const glm::dvec3 ecef_earth_radii = state.ecef_position_km / kSatViewEarthEquatorialRadiusKm;
    const double longitude = std::atan2(ecef_earth_radii.y, ecef_earth_radii.x);
    const double sidereal_angle = greenwich_sidereal_angle_radians(epoch_seconds);
    const double theta = longitude + std::acos(-1.0) + sidereal_angle;
    const double equatorial_radius = std::hypot(ecef_earth_radii.x, ecef_earth_radii.y);
    check_vec3(render_position,
        equatorial_radius * std::sin(theta),
        ecef_earth_radii.z,
        equatorial_radius * std::cos(theta),
        1.0e-10);

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
    const glm::dvec3 expected_render_point =
        teme_position_to_render_earth_radii(result.tracks[0].teme_points_km[0]);
    check_vec3(result.tracks[0].render_teme_points_earth_radii[0],
        expected_render_point.x,
        expected_render_point.y,
        expected_render_point.z,
        1.0e-12);
}

TEST_CASE("SatView parallel propagation matches sequential results", "[satview][propagation][parallel]")
{
    const auto parsed = parse_celestrak_gp_json(kVallado00005Json, "parallel", "test");
    REQUIRE(parsed);
    REQUIRE(parsed.catalog.objects.size() == 1);

    draxul::satview::SatelliteCatalog catalog = parsed.catalog;
    catalog.objects.clear();
    constexpr std::size_t kSatelliteCount = 256;
    for (std::size_t i = 0; i < kSatelliteCount; ++i)
    {
        auto record = parsed.catalog.objects.front();
        record.norad_catalog_id = 10000 + static_cast<std::int64_t>(i);
        record.object_name = "PARALLEL-" + std::to_string(i);
        record.mean_anomaly_deg = std::fmod(
            record.mean_anomaly_deg + static_cast<double>(i) * 1.25,
            360.0);
        catalog.objects.push_back(std::move(record));
    }

    auto build = build_satellite_propagation_model(catalog);
    REQUIRE(build);
    REQUIRE(build.compiled_records == kSatelliteCount);

    SatellitePropagationSettings settings;
    settings.track_satellite_limit = 64;
    settings.track_sample_count = 12;
    settings.selected_track_norad_catalog_id = 10000 + kSatelliteCount - 1;
    const double epoch_seconds = *parse_celestrak_epoch_utc("2000-06-27T18:50:19.733568");

    const auto sequential = propagate_satellites(build.model, epoch_seconds, settings);
    SatellitePropagationExecutor executor(4);
    const auto parallel = propagate_satellites(build.model, epoch_seconds, settings, &executor);

    REQUIRE(sequential);
    REQUIRE(parallel);
    CHECK(executor.concurrency() == 4);
    CHECK(parallel.failed_propagations == sequential.failed_propagations);
    REQUIRE(parallel.states.size() == sequential.states.size());
    REQUIRE(parallel.tracks.size() == sequential.tracks.size());

    for (std::size_t i = 0; i < sequential.states.size(); ++i)
    {
        const auto& expected = sequential.states[i];
        const auto& actual = parallel.states[i];
        CHECK(actual.norad_catalog_id == expected.norad_catalog_id);
        CHECK(actual.object_name == expected.object_name);
        CHECK(glm::length(actual.teme_position_km - expected.teme_position_km) == 0.0);
        CHECK(glm::length(actual.teme_velocity_km_per_s - expected.teme_velocity_km_per_s) == 0.0);
        CHECK(glm::length(actual.ecef_position_km - expected.ecef_position_km) == 0.0);
    }

    for (std::size_t i = 0; i < sequential.tracks.size(); ++i)
    {
        const auto& expected = sequential.tracks[i];
        const auto& actual = parallel.tracks[i];
        CHECK(actual.norad_catalog_id == expected.norad_catalog_id);
        REQUIRE(actual.teme_points_km.size() == expected.teme_points_km.size());
        REQUIRE(actual.ecef_points_km.size() == expected.ecef_points_km.size());
        for (std::size_t sample = 0; sample < expected.teme_points_km.size(); ++sample)
        {
            CHECK(glm::length(actual.teme_points_km[sample] - expected.teme_points_km[sample]) == 0.0);
            CHECK(glm::length(actual.ecef_points_km[sample] - expected.ecef_points_km[sample]) == 0.0);
        }
    }

    CHECK(parallel.tracks.back().norad_catalog_id
        == *settings.selected_track_norad_catalog_id);
}

TEST_CASE("SatView propagation prioritizes selection within the track cap", "[satview][propagation]")
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
    REQUIRE(result.tracks.size() == 1);
    CHECK(result.tracks[0].norad_catalog_id == 6);
    CHECK(result.tracks[0].ecef_points_km.size() == 8);
}

TEST_CASE("SatView QZSS samples form a regional Earth ground track", "[satview][propagation][map]")
{
    const std::string json = R"json([
      {
        "OBJECT_NAME": "QZS-4 (MICHIBIKI-4)",
        "OBJECT_ID": "2017-062A",
        "EPOCH": "2026-06-27T07:59:15.491616",
        "MEAN_MOTION": 1.00296711,
        "ECCENTRICITY": 0.07467153,
        "INCLINATION": 40.1035,
        "RA_OF_ASC_NODE": 343.3559,
        "ARG_OF_PERICENTER": 268.2655,
        "MEAN_ANOMALY": 281.5675,
        "EPHEMERIS_TYPE": 0,
        "CLASSIFICATION_TYPE": "U",
        "NORAD_CAT_ID": 42965,
        "ELEMENT_SET_NO": 999,
        "REV_AT_EPOCH": 3190,
        "BSTAR": 0.0,
        "MEAN_MOTION_DOT": -0.0000035,
        "MEAN_MOTION_DDOT": 0.0
      }
    ])json";
    const auto catalog = parse_celestrak_gp_json(json, "qzss", "test");
    REQUIRE(catalog);
    auto build = build_satellite_propagation_model(catalog.catalog);
    REQUIRE(build);

    SatellitePropagationSettings settings;
    settings.track_sample_count = 128;
    settings.track_satellite_limit = 1;
    const double epoch_seconds = *parse_celestrak_epoch_utc("2026-06-27T07:59:15.491616");
    const auto result = propagate_satellites(build.model, epoch_seconds, settings);

    REQUIRE(result);
    REQUIRE(result.tracks.size() == 1);
    const auto& track = result.tracks.front();
    REQUIRE(track.ecef_points_km.size() == 128);
    const double ground_track_longitude_span = unwrapped_longitude_span(track.ecef_points_km);
    const double inertial_longitude_span = unwrapped_longitude_span(track.teme_points_km);
    INFO("ground span radians: " << ground_track_longitude_span);
    INFO("inertial span radians: " << inertial_longitude_span);
    CHECK(ground_track_longitude_span < 1.2);
    CHECK(inertial_longitude_span > 5.5);
}

TEST_CASE("SatView USA 99 ground track exposes its drifting time window", "[satview][propagation][map]")
{
    const std::string json = R"json([
      {
        "OBJECT_NAME": "USA 99 (MILSTAR-1 1)",
        "OBJECT_ID": "1994-009A",
        "EPOCH": "2026-06-27T10:32:54.826944",
        "MEAN_MOTION": 0.98723407,
        "ECCENTRICITY": 0.00006439,
        "INCLINATION": 17.6416,
        "RA_OF_ASC_NODE": 27.5995,
        "ARG_OF_PERICENTER": 191.4747,
        "MEAN_ANOMALY": 162.5297,
        "EPHEMERIS_TYPE": 0,
        "CLASSIFICATION_TYPE": "U",
        "NORAD_CAT_ID": 22988,
        "ELEMENT_SET_NO": 999,
        "REV_AT_EPOCH": 3304,
        "BSTAR": 0.0,
        "MEAN_MOTION_DOT": -0.00000186,
        "MEAN_MOTION_DDOT": 0.0
      }
    ])json";
    const auto catalog = parse_celestrak_gp_json(json, "usa-99", "test");
    REQUIRE(catalog);
    auto build = build_satellite_propagation_model(catalog.catalog);
    REQUIRE(build);

    SatellitePropagationSettings settings;
    settings.track_sample_count = 128;
    settings.track_satellite_limit = 1;
    const double epoch_seconds = *parse_celestrak_epoch_utc("2026-06-27T10:32:54.826944");
    const auto initial = propagate_satellites(build.model, epoch_seconds, settings);
    REQUIRE(initial);
    REQUIRE(initial.tracks.size() == 1);
    const auto& track = initial.tracks.front();
    REQUIRE(track.ecef_points_km.size() == 128);
    CHECK(track.sample_center_unix_seconds == Approx(epoch_seconds));
    CHECK(track.sample_horizon_minutes == Approx(1440.0 / 0.98723407).margin(0.01));

    const double first_longitude = std::atan2(
        track.ecef_points_km.front().y,
        track.ecef_points_km.front().x);
    const double last_longitude = std::atan2(
        track.ecef_points_km.back().y,
        track.ecef_points_km.back().x);
    const double endpoint_drift = std::abs(std::remainder(
        last_longitude - first_longitude,
        2.0 * std::numbers::pi_v<double>));
    double maximum_segment_longitude_step = 0.0;
    for (std::size_t i = 1; i < track.ecef_points_km.size(); ++i)
    {
        const double previous = std::atan2(
            track.ecef_points_km[i - 1].y,
            track.ecef_points_km[i - 1].x);
        const double current = std::atan2(
            track.ecef_points_km[i].y,
            track.ecef_points_km[i].x);
        maximum_segment_longitude_step = std::max(
            maximum_segment_longitude_step,
            std::abs(std::remainder(current - previous, 2.0 * std::numbers::pi_v<double>)));
    }
    INFO("USA 99 endpoint drift radians: " << endpoint_drift);
    INFO("USA 99 maximum segment longitude step: " << maximum_segment_longitude_step);
    CHECK(endpoint_drift > 0.05);
    CHECK(endpoint_drift < 0.15);
    CHECK(maximum_segment_longitude_step < 0.1);
}
