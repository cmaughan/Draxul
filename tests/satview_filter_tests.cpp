#include <catch2/catch_test_macros.hpp>
#include <draxul/satview/satview_filter.h>

using draxul::satview::OrbitClass;
using draxul::satview::SatellitePropagatedState;
using draxul::satview::SatelliteObjectKind;
using draxul::satview::SatViewFilterState;
using draxul::satview::make_satview_filter_candidate;
using draxul::satview::satview_filter_matches;

namespace
{

SatellitePropagatedState make_state()
{
    SatellitePropagatedState state;
    state.norad_catalog_id = 25544;
    state.object_name = "ISS (ZARYA)";
    state.object_id = "1998-067A";
    state.object_type = "PAYLOAD";
    state.object_kind = SatelliteObjectKind::Payload;
    state.classification_type = "U";
    state.orbit_class = OrbitClass::LowEarth;
    state.minutes_since_epoch = 90.0;
    return state;
}

} // namespace

TEST_CASE("SatView filter matches search text case-insensitively", "[satview][filter]")
{
    const SatellitePropagatedState state = make_state();

    SatViewFilterState filter;
    filter.search_text = "zarya";
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state, "active")));

    filter.search_text = "25544";
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state, "active")));

    filter.search_text = "1998";
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state, "active")));

    filter.search_text = "starlink";
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(state, "active")));
}

TEST_CASE("SatView filter gates orbit classes", "[satview][filter]")
{
    const SatellitePropagatedState state = make_state();

    SatViewFilterState filter;
    filter.show_low_earth = false;
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(state, "active")));

    filter.show_low_earth = true;
    filter.show_geosynchronous = false;
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state, "active")));
}

TEST_CASE("SatView filter matches optional metadata and epoch age", "[satview][filter]")
{
    const SatellitePropagatedState state = make_state();

    SatViewFilterState filter;
    filter.object_type_text = "payload";
    filter.source_text = "act";
    filter.max_epoch_age_days = 1.0;
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state, "CelesTrak active")));

    filter.max_epoch_age_days = 0.01;
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(state, "CelesTrak active")));

    filter.max_epoch_age_days = 1.0;
    filter.object_type_text = "debris";
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(state, "CelesTrak active")));
}

TEST_CASE("SatView filter matches normalized object kind", "[satview][filter]")
{
    SatellitePropagatedState state = make_state();
    state.object_type.clear();
    state.object_kind = SatelliteObjectKind::RocketBody;

    SatViewFilterState filter;
    filter.object_type_text = "rocket";
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state, "active")));

    filter.object_type_text = "payload";
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(state, "active")));
}
