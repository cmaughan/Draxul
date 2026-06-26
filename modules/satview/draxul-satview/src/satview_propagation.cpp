#include <draxul/satview/satview_propagation.h>

#include "SGP4.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace draxul::satview
{

namespace
{

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kMinutesPerDay = 1440.0;
constexpr double kSecondsPerDay = 86400.0;
constexpr double kUnixEpochJulianDate = 2440587.5;
constexpr double kSgp4EpochJulianDate = 2433281.5;
constexpr double kReferenceMeanMotionScale = kMinutesPerDay / kTwoPi;

struct ParsedUtc
{
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    double second = 0.0;
};

struct CompiledOrbit
{
    elsetrec satrec{};
};

bool parse_fixed_uint(std::string_view text, std::size_t offset, std::size_t count, int& out)
{
    if (offset + count > text.size())
        return false;
    int value = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const char c = text[offset + i];
        if (c < '0' || c > '9')
            return false;
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

bool is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int days_in_month(int year, int month)
{
    static constexpr std::array<int, 12> kDays = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && is_leap_year(year))
        return 29;
    return kDays[static_cast<std::size_t>(month - 1)];
}

std::int64_t days_from_civil(int year, int month, int day)
{
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153u * static_cast<unsigned>(month + (month > 2 ? -3 : 9)) + 2u) / 5u
        + static_cast<unsigned>(day) - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

bool parse_utc(std::string_view text, ParsedUtc& out)
{
    // CelesTrak GP JSON uses ISO UTC timestamps such as
    // 2026-06-26T03:43:15.671136. Accept a trailing Z if present.
    if (text.size() < 19 || text[4] != '-' || text[7] != '-'
        || (text[10] != 'T' && text[10] != ' ') || text[13] != ':' || text[16] != ':')
    {
        return false;
    }

    int second_integer = 0;
    if (!parse_fixed_uint(text, 0, 4, out.year)
        || !parse_fixed_uint(text, 5, 2, out.month)
        || !parse_fixed_uint(text, 8, 2, out.day)
        || !parse_fixed_uint(text, 11, 2, out.hour)
        || !parse_fixed_uint(text, 14, 2, out.minute)
        || !parse_fixed_uint(text, 17, 2, second_integer))
    {
        return false;
    }

    double fractional = 0.0;
    double scale = 0.1;
    std::size_t pos = 19;
    if (pos < text.size() && text[pos] == '.')
    {
        ++pos;
        if (pos >= text.size() || text[pos] < '0' || text[pos] > '9')
            return false;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
        {
            fractional += static_cast<double>(text[pos] - '0') * scale;
            scale *= 0.1;
            ++pos;
        }
    }

    if (pos < text.size() && text[pos] == 'Z')
        ++pos;
    if (pos != text.size())
        return false;

    if (out.month < 1 || out.month > 12)
        return false;
    if (out.day < 1 || out.day > days_in_month(out.year, out.month))
        return false;
    if (out.hour < 0 || out.hour > 23 || out.minute < 0 || out.minute > 59
        || second_integer < 0 || second_integer >= 60)
    {
        return false;
    }

    out.second = static_cast<double>(second_integer) + fractional;
    return true;
}

double unix_seconds_from_utc(const ParsedUtc& utc)
{
    const std::int64_t days = days_from_civil(utc.year, utc.month, utc.day);
    return static_cast<double>(days) * kSecondsPerDay
        + static_cast<double>(utc.hour * 3600 + utc.minute * 60)
        + utc.second;
}

SatViewJulianDate julian_date_from_utc(const ParsedUtc& utc)
{
    SatViewJulianDate result;
    SGP4Funcs::jday_SGP4(utc.year, utc.month, utc.day, utc.hour, utc.minute, utc.second,
        result.day, result.fraction);
    return result;
}

glm::dvec3 make_dvec3(const double values[3])
{
    return glm::dvec3(values[0], values[1], values[2]);
}

glm::dvec3 teme_to_ecef_km(const glm::dvec3& teme_km, const SatViewJulianDate& jd)
{
    const double theta = SGP4Funcs::gstime_SGP4(jd.value());
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    return glm::dvec3(
        c * teme_km.x + s * teme_km.y,
        -s * teme_km.x + c * teme_km.y,
        teme_km.z);
}

bool is_valid_record_for_sgp4(const SatelliteRecord& record)
{
    return record.mean_motion_rev_per_day > 0.0
        && record.eccentricity >= 0.0
        && record.eccentricity < 1.0
        && std::isfinite(record.mean_motion_rev_per_day)
        && std::isfinite(record.eccentricity)
        && std::isfinite(record.inclination_deg)
        && std::isfinite(record.right_ascension_ascending_node_deg)
        && std::isfinite(record.argument_of_pericenter_deg)
        && std::isfinite(record.mean_anomaly_deg);
}

std::string sgp4_satellite_number(std::int64_t catalog_id)
{
    // Vallado's 2020 C++ struct still stores satnum in a five-character field.
    // The identifier is not used in the propagation math, so keep it bounded.
    const auto bounded = static_cast<long long>(std::llabs(catalog_id) % 100000);
    char buffer[6]{};
    std::snprintf(buffer, sizeof(buffer), "%05lld", bounded);
    return std::string(buffer);
}

std::optional<CompiledOrbit> compile_record(const SatelliteRecord& record)
{
    if (!is_valid_record_for_sgp4(record))
        return std::nullopt;

    ParsedUtc epoch;
    if (!parse_utc(record.epoch_utc, epoch))
        return std::nullopt;

    const SatViewJulianDate epoch_jd = julian_date_from_utc(epoch);
    const std::string satnum = sgp4_satellite_number(record.norad_catalog_id);

    CompiledOrbit orbit;
    orbit.satrec.classification = record.classification_type.empty()
        ? 'U'
        : record.classification_type.front();
    std::strncpy(orbit.satrec.intldesg, record.object_id.c_str(), sizeof(orbit.satrec.intldesg) - 1);
    orbit.satrec.intldesg[sizeof(orbit.satrec.intldesg) - 1] = '\0';
    orbit.satrec.ephtype = record.ephemeris_type;
    orbit.satrec.elnum = record.element_set_no;
    orbit.satrec.revnum = record.revolution_at_epoch;

    const double deg_to_rad = kPi / 180.0;
    const double mean_motion_rad_per_minute = record.mean_motion_rev_per_day / kReferenceMeanMotionScale;
    const double mean_motion_dot_rad_per_minute2 =
        record.mean_motion_dot / (kReferenceMeanMotionScale * kMinutesPerDay);
    const double mean_motion_ddot_rad_per_minute3 =
        record.mean_motion_ddot / (kReferenceMeanMotionScale * kMinutesPerDay * kMinutesPerDay);

    SGP4Funcs::sgp4init(
        wgs72,
        'a',
        satnum.c_str(),
        epoch_jd.value() - kSgp4EpochJulianDate,
        record.bstar,
        mean_motion_dot_rad_per_minute2,
        mean_motion_ddot_rad_per_minute3,
        record.eccentricity,
        record.argument_of_pericenter_deg * deg_to_rad,
        record.inclination_deg * deg_to_rad,
        record.mean_anomaly_deg * deg_to_rad,
        mean_motion_rad_per_minute,
        record.right_ascension_ascending_node_deg * deg_to_rad,
        orbit.satrec);

    if (orbit.satrec.error != 0)
        return std::nullopt;
    orbit.satrec.jdsatepoch = epoch_jd.day;
    orbit.satrec.jdsatepochF = epoch_jd.fraction;
    return orbit;
}

std::size_t limited_count(std::size_t available, std::size_t limit)
{
    return limit == 0 ? available : std::min(available, limit);
}

bool propagate_one(
    const CompiledOrbit& orbit,
    const SatellitePropagationEntry& entry,
    const SatViewJulianDate& jd,
    double simulation_unix_seconds,
    SatellitePropagatedState& out)
{
    elsetrec satrec = orbit.satrec;
    const double tsince_minutes =
        (jd.day - satrec.jdsatepoch) * kMinutesPerDay
        + (jd.fraction - satrec.jdsatepochF) * kMinutesPerDay;

    double r[3]{};
    double v[3]{};
    SGP4Funcs::sgp4(satrec, tsince_minutes, r, v);
    if (satrec.error != 0)
    {
        out.sgp4_error = satrec.error;
        return false;
    }

    out.norad_catalog_id = entry.norad_catalog_id;
    out.object_name = entry.object_name;
    out.object_id = entry.object_id;
    out.object_type = entry.object_type;
    out.object_kind = entry.object_kind;
    out.classification_type = entry.classification_type;
    out.orbit_class = entry.orbit_class;
    out.period_minutes = entry.period_minutes;
    out.minutes_since_epoch = (simulation_unix_seconds - entry.epoch_unix_seconds) / 60.0;
    out.teme_position_km = make_dvec3(r);
    out.teme_velocity_km_per_s = make_dvec3(v);
    out.ecef_position_km = teme_to_ecef_km(out.teme_position_km, jd);
    out.render_position_earth_radii = out.ecef_position_km / kSatViewEarthEquatorialRadiusKm;
    return true;
}

void append_track_samples(
    const CompiledOrbit& orbit,
    const SatellitePropagationEntry& entry,
    double simulation_unix_seconds,
    const SatellitePropagationSettings& settings,
    SatelliteOrbitTrack& track)
{
    track.norad_catalog_id = entry.norad_catalog_id;
    track.object_name = entry.object_name;
    track.object_id = entry.object_id;
    track.object_type = entry.object_type;
    track.object_kind = entry.object_kind;
    track.classification_type = entry.classification_type;
    track.orbit_class = entry.orbit_class;
    track.minutes_since_epoch = (simulation_unix_seconds - entry.epoch_unix_seconds) / 60.0;
    track.teme_points_km.reserve(settings.track_sample_count);
    track.ecef_points_km.reserve(settings.track_sample_count);
    track.render_teme_points_earth_radii.reserve(settings.track_sample_count);
    track.render_points_earth_radii.reserve(settings.track_sample_count);

    if (settings.track_sample_count == 0)
        return;

    const double horizon_minutes = settings.track_horizon_minutes > 0.0
        ? settings.track_horizon_minutes
        : std::max(1.0, entry.period_minutes);
    const double center_minutes = horizon_minutes * 0.5;
    const double divisor = settings.track_sample_count > 1
        ? static_cast<double>(settings.track_sample_count - 1)
        : 1.0;

    for (std::size_t i = 0; i < settings.track_sample_count; ++i)
    {
        const double offset_minutes = horizon_minutes * (static_cast<double>(i) / divisor) - center_minutes;
        const double sample_unix_seconds = simulation_unix_seconds + offset_minutes * 60.0;
        const SatViewJulianDate jd = julian_date_from_unix_seconds(sample_unix_seconds);

        SatellitePropagatedState state;
        if (!propagate_one(orbit, entry, jd, sample_unix_seconds, state))
            continue;
        track.teme_points_km.push_back(state.teme_position_km);
        track.ecef_points_km.push_back(state.ecef_position_km);
        track.render_teme_points_earth_radii.push_back(
            state.teme_position_km / kSatViewEarthEquatorialRadiusKm);
        track.render_points_earth_radii.push_back(state.render_position_earth_radii);
    }
}

} // namespace

struct SatellitePropagationModel::State
{
    std::vector<CompiledOrbit> orbits;
};

struct SatellitePropagationBuilderAccess
{
    static SatellitePropagationModel make_model()
    {
        SatellitePropagationModel model;
        model.state_ = std::make_unique<SatellitePropagationModel::State>();
        return model;
    }

    static void reserve(SatellitePropagationModel& model, std::size_t count)
    {
        model.entries_.reserve(count);
        model.state_->orbits.reserve(count);
    }

    static void append(
        SatellitePropagationModel& model,
        SatellitePropagationEntry entry,
        CompiledOrbit orbit)
    {
        model.entries_.push_back(std::move(entry));
        model.state_->orbits.push_back(std::move(orbit));
    }

    static void finish(
        SatellitePropagationModel& model,
        const SatelliteCatalog& catalog,
        std::size_t skipped_records)
    {
        model.source_label_ = catalog.source_label;
        model.source_url_ = catalog.source_url;
        model.skipped_records_ = skipped_records;
    }
};

struct SatellitePropagationRunnerAccess
{
    static const std::vector<CompiledOrbit>& orbits(const SatellitePropagationModel& model)
    {
        return model.state_->orbits;
    }
};

SatellitePropagationModel::SatellitePropagationModel()
    : state_(std::make_unique<State>())
{
}

SatellitePropagationModel::~SatellitePropagationModel() = default;

SatellitePropagationModel::SatellitePropagationModel(SatellitePropagationModel&&) noexcept = default;

SatellitePropagationModel& SatellitePropagationModel::operator=(SatellitePropagationModel&&) noexcept = default;

bool SatellitePropagationModel::empty() const
{
    return entries_.empty();
}

std::size_t SatellitePropagationModel::size() const
{
    return entries_.size();
}

std::size_t SatellitePropagationModel::skipped_records() const
{
    return skipped_records_;
}

std::string_view SatellitePropagationModel::source_label() const
{
    return source_label_;
}

std::string_view SatellitePropagationModel::source_url() const
{
    return source_url_;
}

const std::vector<SatellitePropagationEntry>& SatellitePropagationModel::entries() const
{
    return entries_;
}

std::optional<double> parse_celestrak_epoch_utc(std::string_view epoch_utc)
{
    ParsedUtc parsed;
    if (!parse_utc(epoch_utc, parsed))
        return std::nullopt;
    return unix_seconds_from_utc(parsed);
}

SatViewJulianDate julian_date_from_unix_seconds(double unix_seconds)
{
    SatViewJulianDate result;
    const double jd = kUnixEpochJulianDate + unix_seconds / kSecondsPerDay;
    result.day = std::floor(jd);
    result.fraction = jd - result.day;
    return result;
}

SatellitePropagationBuildResult build_satellite_propagation_model(const SatelliteCatalog& catalog)
{
    SatellitePropagationBuildResult result;
    result.model = SatellitePropagationBuilderAccess::make_model();
    SatellitePropagationBuilderAccess::reserve(result.model, catalog.objects.size());

    std::size_t skipped = catalog.skipped_records;
    for (const SatelliteRecord& record : catalog.objects)
    {
        auto epoch_unix_seconds = parse_celestrak_epoch_utc(record.epoch_utc);
        auto compiled = compile_record(record);
        if (!epoch_unix_seconds.has_value() || !compiled.has_value())
        {
            ++skipped;
            continue;
        }

        SatellitePropagationEntry entry;
        entry.norad_catalog_id = record.norad_catalog_id;
        entry.object_name = record.object_name;
        entry.object_id = record.object_id;
        entry.object_type = record.object_type;
        entry.object_kind = record.object_kind;
        entry.classification_type = record.classification_type;
        entry.orbit_class = record.orbit_class;
        entry.epoch_unix_seconds = *epoch_unix_seconds;
        entry.period_minutes = record.period_minutes;
        SatellitePropagationBuilderAccess::append(result.model, std::move(entry), std::move(*compiled));
    }

    SatellitePropagationBuilderAccess::finish(result.model, catalog, skipped);
    result.compiled_records = result.model.size();
    result.skipped_records = skipped;
    if (result.compiled_records == 0 && !catalog.objects.empty())
        result.error = "no valid SGP4 records found";
    return result;
}

SatellitePropagationResult propagate_satellites(
    const SatellitePropagationModel& model,
    double simulation_unix_seconds,
    const SatellitePropagationSettings& settings)
{
    SatellitePropagationResult result;
    result.simulation_unix_seconds = simulation_unix_seconds;
    result.simulation_julian_date = julian_date_from_unix_seconds(simulation_unix_seconds);
    result.skipped_model_records = model.skipped_records();

    if (!std::isfinite(simulation_unix_seconds))
    {
        result.error = "simulation time is not finite";
        return result;
    }

    const std::size_t count = limited_count(model.size(), settings.max_satellites);
    result.states.reserve(count);
    const auto& entries = model.entries();
    const auto& orbits = SatellitePropagationRunnerAccess::orbits(model);

    for (std::size_t i = 0; i < count; ++i)
    {
        SatellitePropagatedState state;
        if (propagate_one(orbits[i], entries[i], result.simulation_julian_date, simulation_unix_seconds, state))
        {
            result.states.push_back(std::move(state));
        }
        else
        {
            ++result.failed_propagations;
        }
    }

    const std::size_t track_count = settings.track_sample_count == 0
        ? 0
        : limited_count(count, settings.track_satellite_limit);
    result.tracks.reserve(track_count + (settings.selected_track_norad_catalog_id.has_value() ? 1 : 0));
    for (std::size_t i = 0; i < track_count; ++i)
    {
        SatelliteOrbitTrack track;
        append_track_samples(orbits[i], entries[i], simulation_unix_seconds, settings, track);
        if (!track.ecef_points_km.empty())
            result.tracks.push_back(std::move(track));
    }

    if (settings.track_sample_count != 0 && settings.selected_track_norad_catalog_id.has_value())
    {
        for (std::size_t i = track_count; i < count; ++i)
        {
            if (entries[i].norad_catalog_id != *settings.selected_track_norad_catalog_id)
                continue;

            SatelliteOrbitTrack track;
            append_track_samples(orbits[i], entries[i], simulation_unix_seconds, settings, track);
            if (!track.ecef_points_km.empty())
                result.tracks.push_back(std::move(track));
            break;
        }
    }

    return result;
}

} // namespace draxul::satview
