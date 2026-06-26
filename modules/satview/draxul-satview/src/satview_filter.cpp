#include <draxul/satview/satview_filter.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>

namespace draxul::satview
{

namespace
{

std::string lowercase_copy(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const unsigned char c : text)
        result.push_back(static_cast<char>(std::tolower(c)));
    return result;
}

bool contains_case_insensitive(std::string_view haystack, std::string_view needle)
{
    if (needle.empty())
        return true;
    if (haystack.empty())
        return false;
    const std::string lower_haystack = lowercase_copy(haystack);
    const std::string lower_needle = lowercase_copy(needle);
    return lower_haystack.find(lower_needle) != std::string::npos;
}

bool id_contains(std::int64_t id, std::string_view needle)
{
    if (needle.empty())
        return true;
    return std::to_string(id).find(std::string(needle)) != std::string::npos;
}

bool text_filter_matches(
    std::string_view filter_text,
    std::string_view primary,
    std::string_view secondary = {},
    std::string_view tertiary = {})
{
    if (filter_text.empty())
        return true;
    return contains_case_insensitive(primary, filter_text)
        || contains_case_insensitive(secondary, filter_text)
        || contains_case_insensitive(tertiary, filter_text);
}

} // namespace

bool satview_orbit_class_visible(
    const SatViewFilterState& filter,
    OrbitClass orbit_class)
{
    switch (orbit_class)
    {
    case OrbitClass::LowEarth:
        return filter.show_low_earth;
    case OrbitClass::MediumEarth:
        return filter.show_medium_earth;
    case OrbitClass::Geosynchronous:
        return filter.show_geosynchronous;
    case OrbitClass::HighlyElliptical:
        return filter.show_highly_elliptical;
    case OrbitClass::Other:
        return filter.show_other;
    }
    return filter.show_other;
}

bool satview_filter_matches(
    const SatViewFilterState& filter,
    const SatViewFilterCandidate& candidate)
{
    if (!satview_orbit_class_visible(filter, candidate.orbit_class))
        return false;

    if (!filter.search_text.empty()
        && !text_filter_matches(
            filter.search_text,
            candidate.object_name,
            candidate.object_id,
            std::to_string(candidate.norad_catalog_id))
        && !id_contains(candidate.norad_catalog_id, filter.search_text))
    {
        return false;
    }

    if (!text_filter_matches(
        filter.object_type_text,
        candidate.object_type,
        candidate.classification_type))
    {
        return false;
    }

    if (!text_filter_matches(filter.source_text, candidate.source_label))
        return false;

    if (filter.max_epoch_age_days > 0.0)
    {
        const double max_minutes = filter.max_epoch_age_days * 24.0 * 60.0;
        if (!std::isfinite(candidate.minutes_since_epoch)
            || std::abs(candidate.minutes_since_epoch) > max_minutes)
        {
            return false;
        }
    }

    return true;
}

SatViewFilterCandidate make_satview_filter_candidate(
    const SatellitePropagationEntry& entry,
    std::string_view source_label)
{
    return SatViewFilterCandidate{
        .norad_catalog_id = entry.norad_catalog_id,
        .object_name = entry.object_name,
        .object_id = entry.object_id,
        .object_type = entry.object_type,
        .classification_type = entry.classification_type,
        .source_label = source_label,
        .orbit_class = entry.orbit_class,
        .minutes_since_epoch = 0.0,
    };
}

SatViewFilterCandidate make_satview_filter_candidate(
    const SatellitePropagatedState& state,
    std::string_view source_label)
{
    return SatViewFilterCandidate{
        .norad_catalog_id = state.norad_catalog_id,
        .object_name = state.object_name,
        .object_id = state.object_id,
        .object_type = state.object_type,
        .classification_type = state.classification_type,
        .source_label = source_label,
        .orbit_class = state.orbit_class,
        .minutes_since_epoch = state.minutes_since_epoch,
    };
}

SatViewFilterCandidate make_satview_filter_candidate(
    const SatelliteOrbitTrack& track,
    std::string_view source_label)
{
    return SatViewFilterCandidate{
        .norad_catalog_id = track.norad_catalog_id,
        .object_name = track.object_name,
        .object_id = track.object_id,
        .object_type = track.object_type,
        .classification_type = track.classification_type,
        .source_label = source_label,
        .orbit_class = track.orbit_class,
        .minutes_since_epoch = track.minutes_since_epoch,
    };
}

} // namespace draxul::satview
