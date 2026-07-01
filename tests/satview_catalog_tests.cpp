#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <draxul/satview/satview_catalog.h>

using draxul::satview::OrbitClass;
using draxul::satview::OrbitSolutionKind;
using draxul::satview::SatelliteObjectKind;
using draxul::satview::SatellitePopulation;
using draxul::satview::merge_satellite_catalogs;
using draxul::satview::parse_celestrak_gp_json;
using draxul::satview::parse_celestrak_satcat_csv;
using draxul::satview::satcat_status_is_active;

TEST_CASE("SatView catalog parses CelesTrak GP JSON records", "[satview][catalog]")
{
    const std::string json = R"json([
      {
        "OBJECT_NAME": "ISS (ZARYA)",
        "OBJECT_ID": "1998-067A",
        "OBJECT_TYPE": "PAYLOAD",
        "EPOCH": "2026-06-26T03:43:15.671136",
        "MEAN_MOTION": 15.49434804,
        "ECCENTRICITY": 0.00043588,
        "INCLINATION": 51.6325,
        "RA_OF_ASC_NODE": 255.7018,
        "ARG_OF_PERICENTER": 233.1468,
        "MEAN_ANOMALY": 126.9121,
        "EPHEMERIS_TYPE": 0,
        "CLASSIFICATION_TYPE": "U",
        "NORAD_CAT_ID": 25544,
        "ELEMENT_SET_NO": 999,
        "REV_AT_EPOCH": 57314,
        "BSTAR": 0.00017696555,
        "MEAN_MOTION_DOT": 9.461e-5,
        "MEAN_MOTION_DDOT": 0
      },
      {
        "OBJECT_NAME": "SAMPLE GEO R/B",
        "OBJECT_ID": "2026-001A",
        "EPOCH": "2026-06-26T00:00:00.000000",
        "MEAN_MOTION": 1.0027,
        "ECCENTRICITY": 0.0002,
        "INCLINATION": 0.1,
        "RA_OF_ASC_NODE": 5.0,
        "ARG_OF_PERICENTER": 220.0,
        "MEAN_ANOMALY": 140.0,
        "NORAD_CAT_ID": 900003
      }
    ])json";

    const auto result = parse_celestrak_gp_json(json, "stations", "https://celestrak.org/example");

    REQUIRE(result);
    REQUIRE(result.catalog.objects.size() == 2);
    CHECK(result.catalog.source_label == "stations");
    CHECK(result.catalog.source_url == "https://celestrak.org/example");

    const auto& iss = result.catalog.objects[0];
    CHECK(iss.object_name == "ISS (ZARYA)");
    CHECK(iss.object_id == "1998-067A");
    CHECK(iss.object_type == "PAYLOAD");
    CHECK(iss.object_kind == SatelliteObjectKind::Payload);
    CHECK(iss.norad_catalog_id == 25544);
    CHECK(iss.mean_motion_rev_per_day == Catch::Approx(15.49434804));
    CHECK(iss.bstar == Catch::Approx(0.00017696555));
    CHECK(iss.period_minutes == Catch::Approx(92.94).margin(0.1));
    CHECK(iss.orbit_class == OrbitClass::LowEarth);

    const auto& geo = result.catalog.objects[1];
    CHECK(geo.norad_catalog_id == 900003);
    CHECK(geo.orbit_class == OrbitClass::Geosynchronous);
    CHECK(geo.object_kind == SatelliteObjectKind::RocketBody);
    CHECK(geo.period_minutes == Catch::Approx(1436.13).margin(0.5));
}

TEST_CASE("SatView catalog skips malformed GP objects", "[satview][catalog]")
{
    const std::string json = R"json([
      {
        "OBJECT_NAME": "MISSING ELEMENTS",
        "NORAD_CAT_ID": 1
      },
      {
        "OBJECT_NAME": "VALID MEO",
        "OBJECT_ID": "2026-002A",
        "EPOCH": "2026-06-26T00:00:00.000000",
        "MEAN_MOTION": "2.0056",
        "ECCENTRICITY": 0.01,
        "INCLINATION": 55.0,
        "RA_OF_ASC_NODE": 45.0,
        "ARG_OF_PERICENTER": 12.0,
        "MEAN_ANOMALY": 348.0,
        "NORAD_CAT_ID": "900002"
      }
    ])json";

    const auto result = parse_celestrak_gp_json(json);

    REQUIRE(result);
    REQUIRE(result.catalog.objects.size() == 1);
    CHECK(result.catalog.skipped_records == 1);
    CHECK(result.catalog.objects[0].norad_catalog_id == 900002);
    CHECK(result.catalog.objects[0].orbit_class == OrbitClass::MediumEarth);
}

TEST_CASE("SatView sample catalog fixture loads offline", "[satview][catalog]")
{
    const auto result = draxul::satview::load_sample_satellite_catalog();

    REQUIRE(result);
    REQUIRE(result.catalog.objects.size() == 4);
    CHECK(result.catalog.objects[0].orbit_class == OrbitClass::LowEarth);
    CHECK(result.catalog.objects[0].object_kind == SatelliteObjectKind::Payload);
    CHECK(result.catalog.objects[1].orbit_class == OrbitClass::MediumEarth);
    CHECK(result.catalog.objects[1].object_kind == SatelliteObjectKind::RocketBody);
    CHECK(result.catalog.objects[2].orbit_class == OrbitClass::Geosynchronous);
    CHECK(result.catalog.objects[2].object_kind == SatelliteObjectKind::Debris);
    CHECK(result.catalog.objects[3].orbit_class == OrbitClass::HighlyElliptical);
    CHECK(result.catalog.objects[3].object_kind == SatelliteObjectKind::Payload);
}

TEST_CASE("SatView catalog derives sun-synchronous candidates from GP and SATCAT orbits", "[satview][catalog]")
{
    const std::string gp_json = R"json([
      {
        "OBJECT_NAME": "LANDSAT 8",
        "OBJECT_ID": "2013-008A",
        "EPOCH": "2026-07-01T04:05:55.000000",
        "MEAN_MOTION": 14.57103921,
        "ECCENTRICITY": 0.0001347,
        "INCLINATION": 98.2298,
        "RA_OF_ASC_NODE": 252.1397,
        "ARG_OF_PERICENTER": 93.4482,
        "MEAN_ANOMALY": 266.6871,
        "NORAD_CAT_ID": 39084
      },
      {
        "OBJECT_NAME": "ISS (ZARYA)",
        "OBJECT_ID": "1998-067A",
        "EPOCH": "2026-07-01T02:54:32.000000",
        "MEAN_MOTION": 15.49499881,
        "ECCENTRICITY": 0.0004254,
        "INCLINATION": 51.6311,
        "RA_OF_ASC_NODE": 231.115,
        "ARG_OF_PERICENTER": 253.5517,
        "MEAN_ANOMALY": 106.5005,
        "NORAD_CAT_ID": 25544
      }
    ])json";
    const auto gp = parse_celestrak_gp_json(gp_json);
    REQUIRE(gp);
    REQUIRE(gp.catalog.objects.size() == 2);
    CHECK(gp.catalog.objects[0].sun_synchronous_candidate);
    CHECK_FALSE(gp.catalog.objects[1].sun_synchronous_candidate);

    const std::string satcat_csv =
        "OBJECT_NAME,NORAD_CAT_ID,OBJECT_TYPE,OPS_STATUS_CODE,DECAY_DATE,PERIOD,INCLINATION,"
        "APOGEE,PERIGEE,ORBIT_CENTER,ORBIT_TYPE\n"
        "SUMMARY SSO,900001,PAY,+,,98.8,98.2,710,690,EA,ORB\n";
    const auto satcat = parse_celestrak_satcat_csv(satcat_csv);
    REQUIRE(satcat);
    REQUIRE(satcat.catalog.objects.size() == 1);
    CHECK(satcat.catalog.objects[0].sun_synchronous_candidate);
}

TEST_CASE("SatView SATCAT parser handles RFC 4180 fields and population metadata", "[satview][catalog]")
{
    const std::string csv =
        "OWNER,OBJECT_NAME,NORAD_CAT_ID,OBJECT_ID,OBJECT_TYPE,OPS_STATUS_CODE,DECAY_DATE,"
        "PERIOD,INCLINATION,APOGEE,PERIGEE,RCS,DATA_STATUS_CODE,ORBIT_CENTER,ORBIT_TYPE\r\n"
        "US,\"PAYLOAD, \"\"ALPHA\"\"\",123456789,2026-001A,PAY,+,,100,51.6,700,680,12.5,,EA,ORB\r\n"
        "CIS,OLD PAYLOAD,200,1990-001A,PAY,-,,120,65,1200,1000,,,EA,ORB\r\n"
        "US,ROCKET BODY,201,1990-001B,R/B,,,130,28,1500,900,,,EA,ORB\r\n"
        "US,DEBRIS,202,1990-001C,DEB,,,140,28,1800,800,,,EA,ORB\r\n"
        "US,UNKNOWN,203,1990-001D,UNK,,,,,,,,,EA,ORB\r\n";

    const auto result = parse_celestrak_satcat_csv(csv, "SATCAT", "https://example/satcat.csv");

    REQUIRE(result);
    REQUIRE(result.catalog.objects.size() == 5);
    CHECK(result.catalog.non_renderable_records == 1);
    const auto& active = result.catalog.objects[0];
    CHECK(active.object_name == "PAYLOAD, \"ALPHA\"");
    CHECK(active.norad_catalog_id == 123456789);
    CHECK(active.population == SatellitePopulation::ActivePayload);
    CHECK(active.solution_kind == OrbitSolutionKind::SatcatSummaryEstimate);
    REQUIRE(active.radar_cross_section_m2.has_value());
    CHECK(*active.radar_cross_section_m2 == Catch::Approx(12.5));
    CHECK(active.renderable);
    CHECK(result.catalog.objects[1].population == SatellitePopulation::InactivePayload);
    CHECK(result.catalog.objects[2].population == SatellitePopulation::RocketBody);
    CHECK(result.catalog.objects[3].population == SatellitePopulation::Debris);
    CHECK(result.catalog.objects[4].population == SatellitePopulation::Unknown);
    CHECK_FALSE(result.catalog.objects[4].renderable);
}

TEST_CASE("SatView SATCAT operational status mapping matches active population rules", "[satview][catalog]")
{
    CHECK(satcat_status_is_active("+"));
    CHECK(satcat_status_is_active("p"));
    CHECK(satcat_status_is_active("B"));
    CHECK(satcat_status_is_active("S"));
    CHECK(satcat_status_is_active("X"));
    CHECK_FALSE(satcat_status_is_active("-"));
    CHECK_FALSE(satcat_status_is_active("D"));
    CHECK_FALSE(satcat_status_is_active(""));
}

TEST_CASE("SatView SATCAT parser separates malformed excluded and non-renderable rows", "[satview][catalog]")
{
    const std::string csv =
        "OBJECT_NAME,NORAD_CAT_ID,OBJECT_TYPE,OPS_STATUS_CODE,DECAY_DATE,PERIOD,INCLINATION,"
        "APOGEE,PERIGEE,ORBIT_CENTER,ORBIT_TYPE\n"
        "BAD ID,nope,PAY,+,,90,51,500,480,EA,ORB\n"
        "DECAYED,10,DEB,,2020-01-01,90,51,500,480,EA,ORB\n"
        "MOON,11,PAY,+,,90,51,500,480,MO,ORB\n"
        "LANDED,12,PAY,+,,90,51,500,480,EA,LAN\n"
        "BAD SUMMARY,13,PAY,+,,oops,51,,,EA,ORB\n"
        "VALID PERIOD,14,PAY,+,,1436,0.1,,,EA,ORB\n";

    const auto result = parse_celestrak_satcat_csv(csv);

    REQUIRE(result);
    CHECK(result.catalog.malformed_records == 1);
    CHECK(result.catalog.excluded_records == 3);
    CHECK(result.catalog.non_renderable_records == 1);
    REQUIRE(result.catalog.objects.size() == 2);
    CHECK_FALSE(result.catalog.objects[0].renderable);
    CHECK(result.catalog.objects[1].renderable);
}

TEST_CASE("SatView catalog merge enriches GP rows and appends SATCAT estimates", "[satview][catalog]")
{
    const std::string gp_json = R"json([
      {"OBJECT_NAME":"MATCHED","OBJECT_ID":"2026-001A","EPOCH":"2026-06-26T00:00:00.000000",
       "MEAN_MOTION":15.5,"ECCENTRICITY":0.0005,"INCLINATION":51.6,"RA_OF_ASC_NODE":120,
       "ARG_OF_PERICENTER":87,"MEAN_ANOMALY":273,"NORAD_CAT_ID":100},
      {"OBJECT_NAME":"GP ONLY","OBJECT_ID":"2026-002A","EPOCH":"2026-06-26T00:00:00.000000",
       "MEAN_MOTION":15.4,"ECCENTRICITY":0.0004,"INCLINATION":52,"RA_OF_ASC_NODE":121,
       "ARG_OF_PERICENTER":88,"MEAN_ANOMALY":272,"NORAD_CAT_ID":101}
    ])json";
    const std::string satcat_csv =
        "OBJECT_NAME,NORAD_CAT_ID,OBJECT_ID,OBJECT_TYPE,OPS_STATUS_CODE,OWNER,DECAY_DATE,PERIOD,"
        "INCLINATION,APOGEE,PERIGEE,RCS,DATA_STATUS_CODE,ORBIT_CENTER,ORBIT_TYPE\n"
        "MATCHED,100,2026-001A,PAY,-,US,,92.9,51.6,430,410,100,,EA,ORB\n"
        "SATCAT ONLY,102,2026-003B,R/B,,CIS,,120,65,1200,900,25,,EA,ORB\n";

    const auto gp = parse_celestrak_gp_json(gp_json, "active", "gp-url");
    const auto satcat = parse_celestrak_satcat_csv(satcat_csv, "SATCAT", "satcat-url");
    REQUIRE(gp);
    REQUIRE(satcat);

    const auto merged = merge_satellite_catalogs(gp.catalog, satcat.catalog);
    REQUIRE(merged.objects.size() == 3);
    CHECK(merged.objects[0].norad_catalog_id == 100);
    CHECK(merged.objects[0].solution_kind == OrbitSolutionKind::GeneralPerturbations);
    CHECK(merged.objects[0].population == SatellitePopulation::ActivePayload);
    CHECK(merged.objects[0].owner == "US");
    CHECK(merged.objects[1].norad_catalog_id == 101);
    CHECK(merged.objects[1].population == SatellitePopulation::ActivePayload);
    CHECK(merged.objects[2].norad_catalog_id == 102);
    CHECK(merged.objects[2].solution_kind == OrbitSolutionKind::SatcatSummaryEstimate);
    CHECK(merged.objects[2].population == SatellitePopulation::RocketBody);
}
