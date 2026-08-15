#include <draxul/satview/satview_filter.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
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

bool satview_population_visible(
    const SatViewFilterState& filter,
    SatellitePopulation population)
{
    switch (population)
    {
    case SatellitePopulation::ActivePayload:
        return filter.show_active_payloads;
    case SatellitePopulation::InactivePayload:
        return filter.show_inactive_payloads;
    case SatellitePopulation::RocketBody:
        return filter.show_rocket_bodies;
    case SatellitePopulation::Debris:
        return filter.show_debris;
    case SatellitePopulation::Unknown:
        return filter.show_unknown_population;
    }
    return filter.show_unknown_population;
}

bool satview_central_body_visible(
    const SatViewFilterState& filter,
    CentralBody central_body)
{
    switch (central_body)
    {
    case CentralBody::Earth:
        return filter.show_earth;
    case CentralBody::Moon:
        return filter.show_moon;
    case CentralBody::Mars:
        return filter.show_mars;
    case CentralBody::Other:
        return false;
    }
    return false;
}

void satview_select_central_body(
    SatViewFilterState& filter,
    CentralBody central_body)
{
    filter.show_earth = central_body == CentralBody::Earth;
    filter.show_moon = central_body == CentralBody::Moon;
    filter.show_mars = central_body == CentralBody::Mars;
}

bool satview_filter_matches(
    const SatViewFilterState& filter,
    const SatViewFilterCandidate& candidate)
{
    if (!satview_central_body_visible(filter, candidate.central_body))
        return false;
    if (!satview_orbit_class_visible(filter, candidate.orbit_class))
        return false;
    if (filter.sun_synchronous_only)
    {
        if (!candidate.sun_synchronous_candidate)
            return false;
        const bool class_visible = candidate.sun_synchronous_terminator
            ? filter.show_sun_synchronous_terminator
            : filter.show_sun_synchronous_other;
        if (!class_visible)
            return false;
    }
    if (!satview_population_visible(filter, candidate.population))
        return false;
    if (!filter.show_summary_estimates
        && candidate.solution_kind == OrbitSolutionKind::SatcatSummaryEstimate)
    {
        return false;
    }
    if (!filter.show_catalog_only
        && candidate.solution_kind == OrbitSolutionKind::CatalogOnly)
    {
        return false;
    }

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
            satellite_object_kind_name(candidate.object_kind),
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
    const SatelliteRecord& record,
    std::string_view)
{
    return SatViewFilterCandidate{
        .norad_catalog_id = record.norad_catalog_id,
        .object_name = record.object_name,
        .object_id = record.object_id,
        .object_type = record.object_type,
        .object_kind = record.object_kind,
        .classification_type = record.classification_type,
        .source_label = orbit_solution_source_name(record.solution_kind),
        .population = record.population,
        .solution_kind = record.solution_kind,
        .central_body = record.central_body,
        .orbit_class = record.orbit_class,
        .sun_synchronous_candidate = record.sun_synchronous_candidate,
        .sun_synchronous_terminator = satview_sun_synchronous_is_terminator(record),
        .minutes_since_epoch = std::numeric_limits<double>::quiet_NaN(),
    };
}

SatViewFilterCandidate make_satview_filter_candidate(
    const SatellitePropagationEntry& entry,
    std::string_view)
{
    return SatViewFilterCandidate{
        .norad_catalog_id = entry.norad_catalog_id,
        .object_name = entry.object_name,
        .object_id = entry.object_id,
        .object_type = entry.object_type,
        .object_kind = entry.object_kind,
        .classification_type = entry.classification_type,
        .source_label = orbit_solution_source_name(entry.solution_kind),
        .population = entry.population,
        .solution_kind = entry.solution_kind,
        .central_body = entry.central_body,
        .orbit_class = entry.orbit_class,
        .sun_synchronous_candidate = entry.sun_synchronous_candidate,
        .sun_synchronous_terminator = entry.sun_synchronous_terminator,
        .minutes_since_epoch = 0.0,
    };
}

SatViewFilterCandidate make_satview_filter_candidate(
    const SatellitePropagatedState& state,
    std::string_view)
{
    const SatelliteStaticMetadata* metadata = state.metadata.get();
    return SatViewFilterCandidate{
        .norad_catalog_id = state.norad_catalog_id,
        .object_name = metadata ? std::string_view(metadata->object_name) : std::string_view{},
        .object_id = metadata ? std::string_view(metadata->object_id) : std::string_view{},
        .object_type = metadata ? std::string_view(metadata->object_type) : std::string_view{},
        .object_kind = state.object_kind,
        .classification_type = metadata ? std::string_view(metadata->classification_type) : std::string_view{},
        .source_label = orbit_solution_source_name(state.solution_kind),
        .population = state.population,
        .solution_kind = state.solution_kind,
        .central_body = state.central_body,
        .orbit_class = state.orbit_class,
        .sun_synchronous_candidate = state.sun_synchronous_candidate,
        .sun_synchronous_terminator = state.sun_synchronous_terminator,
        .minutes_since_epoch = state.minutes_since_epoch,
    };
}

SatViewFilterCandidate make_satview_filter_candidate(
    const SatelliteOrbitTrack& track,
    std::string_view)
{
    return SatViewFilterCandidate{
        .norad_catalog_id = track.norad_catalog_id,
        .object_name = track.object_name,
        .object_id = track.object_id,
        .object_type = track.object_type,
        .object_kind = track.object_kind,
        .classification_type = track.classification_type,
        .source_label = orbit_solution_source_name(track.solution_kind),
        .population = track.population,
        .solution_kind = track.solution_kind,
        .central_body = track.central_body,
        .orbit_class = track.orbit_class,
        .sun_synchronous_candidate = track.sun_synchronous_candidate,
        .sun_synchronous_terminator = track.sun_synchronous_terminator,
        .minutes_since_epoch = track.minutes_since_epoch,
    };
}

} // namespace draxul::satview
