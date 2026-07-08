#include <catch2/catch_test_macros.hpp>
#include <draxul/satview/satview_filter.h>
#include <memory>

using draxul::satview::CentralBody;
using draxul::satview::make_satview_filter_candidate;
using draxul::satview::OrbitClass;
using draxul::satview::OrbitSolutionKind;
using draxul::satview::SatelliteObjectKind;
using draxul::satview::SatellitePopulation;
using draxul::satview::SatellitePropagatedState;
using draxul::satview::satview_filter_matches;
using draxul::satview::satview_select_central_body;
using draxul::satview::SatViewFilterState;

namespace
{

SatellitePropagatedState make_state()
{
    SatellitePropagatedState state;
    state.norad_catalog_id = 25544;
    auto metadata = std::make_shared<draxul::satview::SatelliteStaticMetadata>();
    metadata->object_name = "ISS (ZARYA)";
    metadata->object_id = "1998-067A";
    metadata->object_type = "PAYLOAD";
    metadata->classification_type = "U";
    state.metadata = std::move(metadata);
    state.object_kind = SatelliteObjectKind::Payload;
    state.population = SatellitePopulation::ActivePayload;
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
    auto metadata = std::make_shared<draxul::satview::SatelliteStaticMetadata>(*state.metadata);
    metadata->object_type.clear();
    state.metadata = std::move(metadata);
    state.object_kind = SatelliteObjectKind::RocketBody;

    SatViewFilterState filter;
    filter.object_type_text = "rocket";
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state, "active")));

    filter.object_type_text = "payload";
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(state, "active")));
}

TEST_CASE("SatView filter gates derived sun-synchronous candidates independently", "[satview][filter]")
{
    SatellitePropagatedState state = make_state();

    SatViewFilterState filter;
    filter.sun_synchronous_only = true;
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(state)));

    state.sun_synchronous_candidate = true;
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state)));

    filter.show_low_earth = false;
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(state)));
}

TEST_CASE("SatView filter splits sun-synchronous orbits into terminator and other", "[satview][filter]")
{
    SatellitePropagatedState state = make_state();
    state.sun_synchronous_candidate = true;

    SatViewFilterState filter;
    filter.sun_synchronous_only = true;

    // Both sub-toggles default on: every sun-synchronous orbit passes.
    state.sun_synchronous_terminator = true;
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state)));
    state.sun_synchronous_terminator = false;
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state)));

    // Terminator only: dawn/dusk pass, others are hidden.
    filter.show_sun_synchronous_other = false;
    state.sun_synchronous_terminator = true;
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state)));
    state.sun_synchronous_terminator = false;
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(state)));

    // Other only: dawn/dusk hidden, the rest pass.
    filter.show_sun_synchronous_other = true;
    filter.show_sun_synchronous_terminator = false;
    state.sun_synchronous_terminator = true;
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(state)));
    state.sun_synchronous_terminator = false;
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state)));

    // The split only applies when the SSO restriction is active.
    filter.sun_synchronous_only = false;
    state.sun_synchronous_terminator = true;
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state)));
}

TEST_CASE("SatView filter gates populations and summary estimates independently", "[satview][filter]")
{
    SatellitePropagatedState state = make_state();
    state.population = SatellitePopulation::Debris;
    state.solution_kind = OrbitSolutionKind::SatcatSummaryEstimate;

    SatViewFilterState filter;
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state)));

    filter.show_debris = false;
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(state)));

    filter.show_debris = true;
    filter.show_summary_estimates = false;
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(state)));

    filter.show_summary_estimates = true;
    filter.source_text = "satcat";
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(state)));
}

TEST_CASE("SatView filter composes central body and catalog-only fidelity", "[satview][filter][moon]")
{
    draxul::satview::SatelliteRecord record;
    record.norad_catalog_id = 81001;
    record.object_name = "LUNAR CATALOG ONLY";
    record.central_body = CentralBody::Moon;
    record.solution_kind = OrbitSolutionKind::CatalogOnly;

    SatViewFilterState filter;
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(record)));

    filter.show_earth = false;
    filter.show_moon = true;
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(record)));

    filter.show_catalog_only = false;
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(record)));
}

TEST_CASE("SatView POV body selection replaces the object body filter", "[satview][filter][moon]")
{
    SatViewFilterState filter;
    satview_select_central_body(filter, CentralBody::Moon);
    CHECK_FALSE(filter.show_earth);
    CHECK(filter.show_moon);
    CHECK_FALSE(filter.show_mars);

    satview_select_central_body(filter, CentralBody::Earth);
    CHECK(filter.show_earth);
    CHECK_FALSE(filter.show_moon);
    CHECK_FALSE(filter.show_mars);

    satview_select_central_body(filter, CentralBody::Mars);
    CHECK_FALSE(filter.show_earth);
    CHECK_FALSE(filter.show_moon);
    CHECK(filter.show_mars);
}

TEST_CASE("SatView filter composes Mars central body and catalog-only fidelity", "[satview][filter][mars]")
{
    draxul::satview::SatelliteRecord record;
    record.norad_catalog_id = 82001;
    record.object_name = "MARS CATALOG ONLY";
    record.central_body = CentralBody::Mars;
    record.solution_kind = OrbitSolutionKind::CatalogOnly;

    SatViewFilterState filter;
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(record)));

    satview_select_central_body(filter, CentralBody::Mars);
    CHECK(satview_filter_matches(filter, make_satview_filter_candidate(record)));

    filter.show_catalog_only = false;
    CHECK_FALSE(satview_filter_matches(filter, make_satview_filter_candidate(record)));
}
