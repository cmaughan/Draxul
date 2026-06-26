#pragma once

#include <cstdint>
#include <draxul/satview/satview_catalog.h>
#include <draxul/satview/satview_propagation.h>
#include <string>
#include <string_view>

namespace draxul::satview
{

struct SatViewFilterState
{
    std::string search_text;
    std::string object_type_text;
    std::string source_text;
    bool show_low_earth = true;
    bool show_medium_earth = true;
    bool show_geosynchronous = true;
    bool show_highly_elliptical = true;
    bool show_other = true;

    // Zero or negative disables the age filter.
    double max_epoch_age_days = 0.0;
};

struct SatViewFilterCandidate
{
    std::int64_t norad_catalog_id = 0;
    std::string_view object_name;
    std::string_view object_id;
    std::string_view object_type;
    SatelliteObjectKind object_kind = SatelliteObjectKind::Unknown;
    std::string_view classification_type;
    std::string_view source_label;
    OrbitClass orbit_class = OrbitClass::Other;
    double minutes_since_epoch = 0.0;
};

[[nodiscard]] bool satview_orbit_class_visible(
    const SatViewFilterState& filter,
    OrbitClass orbit_class);

[[nodiscard]] bool satview_filter_matches(
    const SatViewFilterState& filter,
    const SatViewFilterCandidate& candidate);

[[nodiscard]] SatViewFilterCandidate make_satview_filter_candidate(
    const SatellitePropagationEntry& entry,
    std::string_view source_label = {});

[[nodiscard]] SatViewFilterCandidate make_satview_filter_candidate(
    const SatellitePropagatedState& state,
    std::string_view source_label = {});

[[nodiscard]] SatViewFilterCandidate make_satview_filter_candidate(
    const SatelliteOrbitTrack& track,
    std::string_view source_label = {});

} // namespace draxul::satview
