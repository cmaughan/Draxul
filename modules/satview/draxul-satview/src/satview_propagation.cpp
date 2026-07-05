#include <draxul/satview/satview_propagation.h>

#include "satview_object_style.h"
#include "satview_moon_ephemeris.h"

#include "SGP4.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <thread>

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
constexpr double kEarthMuKm3PerS2 = 398600.4418;
constexpr double kMoonMuKm3PerS2 = 4902.800118;
constexpr double kSummaryReferenceUnixSeconds = 946728000.0; // J2000 noon UTC.

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
    struct Summary
    {
        double semi_major_axis_km = 0.0;
        double eccentricity = 0.0;
        double inclination_radians = 0.0;
        double right_ascension_radians = 0.0;
        double argument_perigee_radians = 0.0;
        double reference_mean_anomaly_radians = 0.0;
        double period_seconds = 0.0;
    };

    OrbitSolutionKind solution_kind = OrbitSolutionKind::GeneralPerturbations;
    CentralBody central_body = CentralBody::Earth;
    elsetrec satrec{};
    Summary summary;
    std::vector<SampledEphemerisPoint> samples;
};

std::uint64_t mix_catalog_id(std::int64_t catalog_id, std::uint64_t salt)
{
    std::uint64_t value = static_cast<std::uint64_t>(catalog_id) + salt + 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

double stable_angle(std::int64_t catalog_id, std::uint64_t salt)
{
    constexpr double kInverse53Bits = 1.0 / 9007199254740992.0;
    const std::uint64_t value = mix_catalog_id(catalog_id, salt) >> 11u;
    return static_cast<double>(value) * kInverse53Bits * kTwoPi;
}

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
    if (record.central_body != CentralBody::Earth)
        return std::nullopt;
    if (!is_valid_record_for_sgp4(record))
        return std::nullopt;

    ParsedUtc epoch;
    if (!parse_utc(record.epoch_utc, epoch))
        return std::nullopt;

    const SatViewJulianDate epoch_jd = julian_date_from_utc(epoch);
    const std::string satnum = sgp4_satellite_number(record.norad_catalog_id);

    CompiledOrbit orbit;
    orbit.solution_kind = record.solution_kind;
    orbit.central_body = record.central_body;
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

std::optional<CompiledOrbit> compile_summary_record(const SatelliteRecord& record)
{
    if (!record.renderable || !satellite_record_has_summary_orbit(record))
        return std::nullopt;

    CompiledOrbit orbit;
    orbit.solution_kind = OrbitSolutionKind::SatcatSummaryEstimate;
    orbit.central_body = record.central_body;
    auto& summary = orbit.summary;

    const double body_radius_km = record.central_body == CentralBody::Moon
        ? kSatViewMoonMeanRadiusKm
        : kSatViewEarthEquatorialRadiusKm;
    const double body_mu_km3_per_s2 = record.central_body == CentralBody::Moon
        ? kMoonMuKm3PerS2
        : kEarthMuKm3PerS2;

    if (record.satcat_perigee_km.has_value() && record.satcat_apogee_km.has_value())
    {
        const double rp = body_radius_km + *record.satcat_perigee_km;
        const double ra = body_radius_km + *record.satcat_apogee_km;
        summary.semi_major_axis_km = 0.5 * (rp + ra);
        summary.eccentricity = (ra - rp) / (ra + rp);
    }
    else if (record.satcat_period_minutes.has_value())
    {
        const double period_seconds = *record.satcat_period_minutes * 60.0;
        summary.semi_major_axis_km = std::cbrt(
            body_mu_km3_per_s2 * period_seconds * period_seconds / (kTwoPi * kTwoPi));
        summary.eccentricity = 0.0;
    }

    summary.period_seconds = record.satcat_period_minutes.has_value()
        ? *record.satcat_period_minutes * 60.0
        : kTwoPi * std::sqrt(
              summary.semi_major_axis_km * summary.semi_major_axis_km * summary.semi_major_axis_km
              / body_mu_km3_per_s2);
    summary.inclination_radians = *record.satcat_inclination_deg * kPi / 180.0;
    summary.right_ascension_radians = stable_angle(record.norad_catalog_id, 0x3c6ef372fe94f82bull);
    summary.argument_perigee_radians = stable_angle(record.norad_catalog_id, 0xa54ff53a5f1d36f1ull);
    summary.reference_mean_anomaly_radians = stable_angle(record.norad_catalog_id, 0x510e527fade682d1ull);

    if (!std::isfinite(summary.semi_major_axis_km)
        || summary.semi_major_axis_km <= body_radius_km * 0.1
        || !std::isfinite(summary.eccentricity)
        || summary.eccentricity < 0.0
        || summary.eccentricity >= 1.0
        || !std::isfinite(summary.period_seconds)
        || summary.period_seconds <= 0.0)
    {
        return std::nullopt;
    }
    return orbit;
}

std::optional<CompiledOrbit> compile_sampled_ephemeris_record(const SatelliteRecord& record)
{
    if (record.central_body != CentralBody::Moon || record.ephemeris_samples.size() < 2)
        return std::nullopt;
    CompiledOrbit orbit;
    orbit.solution_kind = OrbitSolutionKind::SampledEphemeris;
    orbit.central_body = record.central_body;
    orbit.samples = record.ephemeris_samples;
    return orbit;
}

std::size_t limited_count(std::size_t available, std::size_t limit)
{
    return limit == 0 ? available : std::min(available, limit);
}

void populate_state_metadata(
    const SatellitePropagationEntry& entry,
    double simulation_unix_seconds,
    SatellitePropagatedState& out)
{
    out.norad_catalog_id = entry.norad_catalog_id;
    out.object_prefix_hash = entry.object_prefix_hash;
    out.metadata = entry.metadata;
    out.object_kind = entry.object_kind;
    out.population = entry.population;
    out.solution_kind = entry.solution_kind;
    out.central_body = entry.central_body;
    out.orbit_class = entry.orbit_class;
    out.sun_synchronous_candidate = entry.sun_synchronous_candidate;
    out.period_minutes = entry.period_minutes;
    out.minutes_since_epoch = std::isfinite(entry.epoch_unix_seconds)
        ? (simulation_unix_seconds - entry.epoch_unix_seconds) / 60.0
        : std::numeric_limits<double>::quiet_NaN();
}

glm::dvec3 rotate_perifocal(
    const glm::dvec3& value,
    double right_ascension,
    double inclination,
    double argument_perigee)
{
    const double co = std::cos(right_ascension);
    const double so = std::sin(right_ascension);
    const double ci = std::cos(inclination);
    const double si = std::sin(inclination);
    const double cw = std::cos(argument_perigee);
    const double sw = std::sin(argument_perigee);
    return glm::dvec3(
        (co * cw - so * sw * ci) * value.x + (-co * sw - so * cw * ci) * value.y,
        (so * cw + co * sw * ci) * value.x + (-so * sw + co * cw * ci) * value.y,
        sw * si * value.x + cw * si * value.y);
}

bool propagate_summary(
    const CompiledOrbit::Summary& summary,
    double simulation_unix_seconds,
    glm::dvec3& position_km,
    glm::dvec3& velocity_km_per_s)
{
    const double mean_motion_rad_s = kTwoPi / summary.period_seconds;
    const double elapsed = simulation_unix_seconds - kSummaryReferenceUnixSeconds;
    const double mean_anomaly = std::remainder(
        summary.reference_mean_anomaly_radians + elapsed * mean_motion_rad_s,
        kTwoPi);

    double eccentric_anomaly = mean_anomaly;
    for (int i = 0; i < 12; ++i)
    {
        const double f = eccentric_anomaly
            - summary.eccentricity * std::sin(eccentric_anomaly)
            - mean_anomaly;
        const double derivative = 1.0 - summary.eccentricity * std::cos(eccentric_anomaly);
        eccentric_anomaly -= f / derivative;
    }

    const double cosine = std::cos(eccentric_anomaly);
    const double sine = std::sin(eccentric_anomaly);
    const double minor_scale = std::sqrt(1.0 - summary.eccentricity * summary.eccentricity);
    const double eccentric_rate = mean_motion_rad_s
        / (1.0 - summary.eccentricity * cosine);
    const glm::dvec3 perifocal_position(
        summary.semi_major_axis_km * (cosine - summary.eccentricity),
        summary.semi_major_axis_km * minor_scale * sine,
        0.0);
    const glm::dvec3 perifocal_velocity(
        -summary.semi_major_axis_km * sine * eccentric_rate,
        summary.semi_major_axis_km * minor_scale * cosine * eccentric_rate,
        0.0);
    position_km = rotate_perifocal(
        perifocal_position,
        summary.right_ascension_radians,
        summary.inclination_radians,
        summary.argument_perigee_radians);
    velocity_km_per_s = rotate_perifocal(
        perifocal_velocity,
        summary.right_ascension_radians,
        summary.inclination_radians,
        summary.argument_perigee_radians);
    return std::isfinite(position_km.x) && std::isfinite(position_km.y) && std::isfinite(position_km.z)
        && std::isfinite(velocity_km_per_s.x) && std::isfinite(velocity_km_per_s.y)
        && std::isfinite(velocity_km_per_s.z);
}

bool propagate_sampled_ephemeris(
    const CompiledOrbit& orbit,
    double simulation_unix_seconds,
    glm::dvec3& position_km,
    glm::dvec3& velocity_km_per_s)
{
    if (orbit.samples.size() < 2
        || simulation_unix_seconds < orbit.samples.front().unix_seconds
        || simulation_unix_seconds > orbit.samples.back().unix_seconds)
    {
        return false;
    }
    const auto upper = std::ranges::upper_bound(
        orbit.samples,
        simulation_unix_seconds,
        {},
        &SampledEphemerisPoint::unix_seconds);
    const auto second = upper == orbit.samples.end() ? std::prev(upper) : upper;
    const auto first = second == orbit.samples.begin() ? second : std::prev(second);
    const double span = second->unix_seconds - first->unix_seconds;
    const double alpha = span > 0.0
        ? (simulation_unix_seconds - first->unix_seconds) / span
        : 0.0;
    const glm::dvec3 p0(first->x_km, first->y_km, first->z_km);
    const glm::dvec3 p1(second->x_km, second->y_km, second->z_km);
    const glm::dvec3 v0(first->vx_km_per_s, first->vy_km_per_s, first->vz_km_per_s);
    const glm::dvec3 v1(second->vx_km_per_s, second->vy_km_per_s, second->vz_km_per_s);
    const double alpha2 = alpha * alpha;
    const double alpha3 = alpha2 * alpha;
    position_km = (2.0 * alpha3 - 3.0 * alpha2 + 1.0) * p0
        + (alpha3 - 2.0 * alpha2 + alpha) * span * v0
        + (-2.0 * alpha3 + 3.0 * alpha2) * p1
        + (alpha3 - alpha2) * span * v1;
    velocity_km_per_s = ((6.0 * alpha2 - 6.0 * alpha) / span) * p0
        + (3.0 * alpha2 - 4.0 * alpha + 1.0) * v0
        + ((-6.0 * alpha2 + 6.0 * alpha) / span) * p1
        + (3.0 * alpha2 - 2.0 * alpha) * v1;
    return true;
}

bool propagate_one(
    const CompiledOrbit& orbit,
    const SatellitePropagationEntry& entry,
    const SatViewJulianDate& jd,
    double simulation_unix_seconds,
    SatellitePropagatedState& out)
{
    populate_state_metadata(entry, simulation_unix_seconds, out);
    if (orbit.solution_kind == OrbitSolutionKind::SatcatSummaryEstimate
        || orbit.solution_kind == OrbitSolutionKind::SampledEphemeris)
    {
        const bool propagated = orbit.solution_kind == OrbitSolutionKind::SampledEphemeris
            ? propagate_sampled_ephemeris(
                  orbit,
                  simulation_unix_seconds,
                  out.teme_position_km,
                  out.teme_velocity_km_per_s)
            : propagate_summary(
                  orbit.summary,
                  simulation_unix_seconds,
                  out.teme_position_km,
                  out.teme_velocity_km_per_s);
        if (!propagated)
        {
            return false;
        }
        if (orbit.central_body == CentralBody::Moon)
        {
            const SatViewMoonPosition moon = satview_moon_position(simulation_unix_seconds);
            constexpr double kVelocitySampleSeconds = 1.0;
            const glm::dvec3 moon_next = satview_moon_position(
                simulation_unix_seconds + kVelocitySampleSeconds)
                                             .equatorial_position_km;
            out.teme_position_km += moon.equatorial_position_km;
            out.teme_velocity_km_per_s +=
                (moon_next - moon.equatorial_position_km) / kVelocitySampleSeconds;
        }
    }
    else
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
        out.teme_position_km = make_dvec3(r);
        out.teme_velocity_km_per_s = make_dvec3(v);
    }
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
    track.object_prefix_hash = entry.object_prefix_hash;
    track.object_name = entry.object_name;
    track.object_id = entry.object_id;
    track.object_type = entry.object_type;
    track.object_kind = entry.object_kind;
    track.population = entry.population;
    track.solution_kind = entry.solution_kind;
    track.central_body = entry.central_body;
    track.classification_type = entry.classification_type;
    track.owner = entry.owner;
    track.operational_status_code = entry.operational_status_code;
    track.data_status_code = entry.data_status_code;
    track.radar_cross_section_m2 = entry.radar_cross_section_m2;
    track.orbit_class = entry.orbit_class;
    track.sun_synchronous_candidate = entry.sun_synchronous_candidate;
    track.minutes_since_epoch = std::isfinite(entry.epoch_unix_seconds)
        ? (simulation_unix_seconds - entry.epoch_unix_seconds) / 60.0
        : std::numeric_limits<double>::quiet_NaN();
    track.sample_center_unix_seconds = simulation_unix_seconds;
    track.teme_points_km.reserve(settings.track_sample_count);
    track.ecef_points_km.reserve(settings.track_sample_count);
    track.render_teme_points_earth_radii.reserve(settings.track_sample_count);
    track.render_points_earth_radii.reserve(settings.track_sample_count);

    if (settings.track_sample_count == 0)
        return;

    const double horizon_minutes = settings.track_horizon_minutes > 0.0
        ? settings.track_horizon_minutes
        : std::max(1.0,
              entry.track_horizon_minutes > 0.0
                  ? entry.track_horizon_minutes
                  : entry.period_minutes);
    track.sample_horizon_minutes = horizon_minutes;
    const double center_minutes = horizon_minutes * 0.5;
    const double divisor = settings.track_sample_count > 1
        ? static_cast<double>(settings.track_sample_count - 1)
        : 1.0;
    const std::optional<glm::dvec3> track_center = entry.central_body == CentralBody::Moon
        ? std::optional<glm::dvec3>(
              satview_moon_position(simulation_unix_seconds).equatorial_position_km)
        : std::nullopt;

    for (std::size_t i = 0; i < settings.track_sample_count; ++i)
    {
        const double offset_minutes = horizon_minutes * (static_cast<double>(i) / divisor) - center_minutes;
        const double sample_unix_seconds = simulation_unix_seconds + offset_minutes * 60.0;
        const SatViewJulianDate jd = julian_date_from_unix_seconds(sample_unix_seconds);

        SatellitePropagatedState state;
        if (!propagate_one(orbit, entry, jd, sample_unix_seconds, state))
            continue;
        glm::dvec3 display_teme_position_km = state.teme_position_km;
        if (track_center.has_value())
        {
            // A Moon POV renders the Moon at its position at the simulation time. Keep
            // every historical/future sample relative to that same center; otherwise
            // the Moon's translation during the sampling window distorts the orbit.
            display_teme_position_km += *track_center
                - satview_moon_position(sample_unix_seconds).equatorial_position_km;
        }
        const glm::dvec3 display_ecef_position_km =
            teme_to_ecef_km(display_teme_position_km, jd);
        track.teme_points_km.push_back(display_teme_position_km);
        track.ecef_points_km.push_back(display_ecef_position_km);
        track.render_teme_points_earth_radii.push_back(
            teme_position_to_render_earth_radii(display_teme_position_km));
        track.render_points_earth_radii.push_back(
            display_ecef_position_km / kSatViewEarthEquatorialRadiusKm);
    }
}

} // namespace

struct SatellitePropagationExecutor::State
{
    explicit State(std::size_t requested_concurrency)
    {
        if (requested_concurrency == 0)
        {
            constexpr std::size_t kMaximumAutomaticConcurrency = 16;
            const unsigned hardware_threads = std::thread::hardware_concurrency();
            const std::size_t available_concurrency = hardware_threads > 2
                ? static_cast<std::size_t>(hardware_threads - 2)
                : std::max<std::size_t>(1, hardware_threads);
            concurrency = std::min(available_concurrency, kMaximumAutomaticConcurrency);
        }
        else
        {
            concurrency = std::max<std::size_t>(1, requested_concurrency);
        }

        workers.reserve(concurrency - 1);
        for (std::size_t i = 1; i < concurrency; ++i)
            workers.emplace_back([this]() { worker_loop(); });
    }

    ~State()
    {
        std::lock_guard dispatch_lock(dispatch_mutex);
        {
            std::lock_guard job_lock(job_mutex);
            stopping = true;
        }
        job_cv.notify_all();
        workers.clear();
    }

    void run_chunks()
    {
        for (;;)
        {
            const std::size_t begin = next_index.fetch_add(chunk_size, std::memory_order_relaxed);
            if (begin >= item_count)
                return;
            function(begin, std::min(begin + chunk_size, item_count));
        }
    }

    void worker_loop()
    {
        std::uint64_t observed_generation = 0;
        for (;;)
        {
            {
                std::unique_lock lock(job_mutex);
                job_cv.wait(lock, [&]() {
                    return stopping || generation != observed_generation;
                });
                if (stopping)
                    return;
                observed_generation = generation;
            }

            run_chunks();

            {
                std::lock_guard lock(job_mutex);
                --workers_remaining;
            }
            done_cv.notify_one();
        }
    }

    std::size_t concurrency = 1;
    std::vector<std::jthread> workers;
    std::mutex dispatch_mutex;
    std::mutex job_mutex;
    std::condition_variable job_cv;
    std::condition_variable done_cv;
    bool stopping = false;
    std::uint64_t generation = 0;
    std::size_t workers_remaining = 0;
    std::size_t item_count = 0;
    std::size_t chunk_size = 1;
    std::atomic<std::size_t> next_index{ 0 };
    std::function<void(std::size_t, std::size_t)> function;
};

SatellitePropagationExecutor::SatellitePropagationExecutor(std::size_t concurrency)
    : state_(std::make_unique<State>(concurrency))
{
}

SatellitePropagationExecutor::~SatellitePropagationExecutor() = default;

std::size_t SatellitePropagationExecutor::concurrency() const
{
    return state_->concurrency;
}

void SatellitePropagationExecutor::parallel_for(
    std::size_t item_count,
    std::size_t minimum_chunk_size,
    const std::function<void(std::size_t, std::size_t)>& function)
{
    if (item_count == 0)
        return;
    if (state_->workers.empty() || item_count <= minimum_chunk_size * 2)
    {
        function(0, item_count);
        return;
    }

    std::lock_guard dispatch_lock(state_->dispatch_mutex);
    {
        std::lock_guard job_lock(state_->job_mutex);
        state_->item_count = item_count;
        const std::size_t target_chunks = state_->concurrency * 4;
        const std::size_t even_chunk_size = (item_count + target_chunks - 1) / target_chunks;
        state_->chunk_size = std::max<std::size_t>(minimum_chunk_size, even_chunk_size);
        state_->next_index.store(0, std::memory_order_relaxed);
        state_->function = function;
        state_->workers_remaining = state_->workers.size();
        ++state_->generation;
    }
    state_->job_cv.notify_all();

    state_->run_chunks();

    {
        std::unique_lock lock(state_->job_mutex);
        state_->done_cv.wait(lock, [&]() { return state_->workers_remaining == 0; });
        state_->function = {};
    }
}

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

double greenwich_sidereal_angle_radians(double unix_seconds)
{
    return SGP4Funcs::gstime_SGP4(julian_date_from_unix_seconds(unix_seconds).value());
}

glm::dvec3 solar_direction_teme(double unix_seconds)
{
    const double days_since_j2000 = julian_date_from_unix_seconds(unix_seconds).value() - 2451545.0;
    const double mean_longitude_degrees = std::fmod(280.460 + 0.9856474 * days_since_j2000, 360.0);
    const double mean_anomaly = (357.528 + 0.9856003 * days_since_j2000) * kPi / 180.0;
    const double ecliptic_longitude = (mean_longitude_degrees
                                          + 1.915 * std::sin(mean_anomaly)
                                          + 0.020 * std::sin(2.0 * mean_anomaly))
        * kPi / 180.0;
    const double obliquity = (23.439 - 0.0000004 * days_since_j2000) * kPi / 180.0;

    return glm::normalize(glm::dvec3(
        std::cos(ecliptic_longitude),
        std::cos(obliquity) * std::sin(ecliptic_longitude),
        std::sin(obliquity) * std::sin(ecliptic_longitude)));
}

glm::dvec3 solar_direction_render(double unix_seconds)
{
    const glm::dvec3 teme = solar_direction_teme(unix_seconds);
    return glm::normalize(glm::dvec3(-teme.y, teme.z, -teme.x));
}

glm::dvec3 teme_position_to_render_earth_radii(const glm::dvec3& teme_position_km)
{
    const glm::dvec3 earth_radii = teme_position_km / kSatViewEarthEquatorialRadiusKm;
    return glm::dvec3(-earth_radii.y, earth_radii.z, -earth_radii.x);
}

glm::dvec3 satellite_track_anchor_offset_km(
    const SatelliteOrbitTrack& track,
    double simulation_unix_seconds)
{
    if (track.central_body != CentralBody::Moon
        || !std::isfinite(track.sample_center_unix_seconds)
        || !std::isfinite(simulation_unix_seconds))
    {
        return glm::dvec3(0.0);
    }
    return satview_moon_position(simulation_unix_seconds).equatorial_position_km
        - satview_moon_position(track.sample_center_unix_seconds).equatorial_position_km;
}

SatellitePropagationBuildResult build_satellite_propagation_model(const SatelliteCatalog& catalog)
{
    SatellitePropagationBuildResult result;
    result.model = SatellitePropagationBuilderAccess::make_model();
    SatellitePropagationBuilderAccess::reserve(result.model, catalog.objects.size());

    std::size_t skipped = catalog.skipped_records;
    for (const SatelliteRecord& record : catalog.objects)
    {
        if (!record.renderable)
        {
            ++skipped;
            continue;
        }

        std::optional<double> epoch_unix_seconds;
        std::optional<CompiledOrbit> compiled;
        if (record.solution_kind == OrbitSolutionKind::SatcatSummaryEstimate)
        {
            epoch_unix_seconds = std::numeric_limits<double>::quiet_NaN();
            compiled = compile_summary_record(record);
        }
        else if (record.solution_kind == OrbitSolutionKind::SampledEphemeris)
        {
            epoch_unix_seconds = record.ephemeris_samples.empty()
                ? std::optional<double>{}
                : std::optional<double>{ record.ephemeris_samples.front().unix_seconds };
            compiled = compile_sampled_ephemeris_record(record);
        }
        else
        {
            epoch_unix_seconds = parse_celestrak_epoch_utc(record.epoch_utc);
            compiled = compile_record(record);
        }
        if (!epoch_unix_seconds.has_value() || !compiled.has_value())
        {
            ++skipped;
            continue;
        }

        SatellitePropagationEntry entry;
        entry.norad_catalog_id = record.norad_catalog_id;
        entry.object_prefix_hash = satellite_prefix_hash(record.object_name);
        entry.object_name = record.object_name;
        entry.object_id = record.object_id;
        entry.object_type = record.object_type;
        entry.object_kind = record.object_kind;
        entry.population = record.population;
        entry.solution_kind = record.solution_kind;
        entry.central_body = record.central_body;
        entry.classification_type = record.classification_type;
        entry.owner = record.owner;
        entry.operational_status_code = record.operational_status_code;
        entry.data_status_code = record.data_status_code;
        entry.radar_cross_section_m2 = record.radar_cross_section_m2;
        auto metadata = std::make_shared<SatelliteStaticMetadata>();
        metadata->object_name = record.object_name;
        metadata->object_id = record.object_id;
        metadata->object_type = record.object_type;
        metadata->classification_type = record.classification_type;
        metadata->owner = record.owner;
        metadata->operational_status_code = record.operational_status_code;
        metadata->data_status_code = record.data_status_code;
        metadata->ephemeris_source = record.ephemeris_source;
        metadata->ephemeris_frame = record.ephemeris_frame;
        if (!record.ephemeris_samples.empty())
        {
            metadata->ephemeris_start_unix_seconds = record.ephemeris_samples.front().unix_seconds;
            metadata->ephemeris_end_unix_seconds = record.ephemeris_samples.back().unix_seconds;
        }
        metadata->radar_cross_section_m2 = record.radar_cross_section_m2;
        entry.metadata = std::move(metadata);
        entry.orbit_class = record.orbit_class;
        entry.sun_synchronous_candidate = record.sun_synchronous_candidate;
        entry.epoch_unix_seconds = *epoch_unix_seconds;
        entry.period_minutes = record.period_minutes;
        entry.track_horizon_minutes = record.ephemeris_track_horizon_minutes;
        SatellitePropagationBuilderAccess::append(result.model, std::move(entry), std::move(*compiled));
    }

    SatellitePropagationBuilderAccess::finish(result.model, catalog, skipped);
    result.compiled_records = result.model.size();
    result.skipped_records = skipped;
    if (result.compiled_records == 0 && !catalog.objects.empty())
        result.error = "no renderable orbit records found";
    return result;
}

SatellitePropagationResult propagate_satellites(
    const SatellitePropagationModel& model,
    double simulation_unix_seconds,
    const SatellitePropagationSettings& settings,
    SatellitePropagationExecutor* executor)
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
    const auto& entries = model.entries();
    const auto& orbits = SatellitePropagationRunnerAccess::orbits(model);

    result.states.resize(count);
    std::vector<unsigned char> state_valid(count, 0);
    const auto propagate_state_range = [&](std::size_t begin, std::size_t end) {
        for (std::size_t i = begin; i < end; ++i)
        {
            state_valid[i] = static_cast<unsigned char>(propagate_one(
                orbits[i],
                entries[i],
                result.simulation_julian_date,
                simulation_unix_seconds,
                result.states[i]));
        }
    };
    if (executor)
        executor->parallel_for(count, 64, propagate_state_range);
    else
        propagate_state_range(0, count);

    std::size_t output_state_index = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!state_valid[i])
        {
            ++result.failed_propagations;
            continue;
        }
        if (output_state_index != i)
            result.states[output_state_index] = std::move(result.states[i]);
        ++output_state_index;
    }
    result.states.resize(output_state_index);

    std::vector<std::size_t> eligible_track_indices;
    if (settings.track_sample_count != 0)
    {
        eligible_track_indices.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (!settings.track_central_body.has_value()
                || entries[i].central_body == *settings.track_central_body)
            {
                eligible_track_indices.push_back(i);
            }
        }
    }
    const std::size_t track_count = limited_count(
        eligible_track_indices.size(), settings.track_satellite_limit);
    std::optional<std::size_t> selected_track_index;
    if (track_count != 0 && settings.selected_track_norad_catalog_id.has_value())
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (entries[i].norad_catalog_id == *settings.selected_track_norad_catalog_id
                && (!settings.track_central_body.has_value()
                    || entries[i].central_body == *settings.track_central_body))
            {
                selected_track_index = i;
                break;
            }
        }
    }

    const auto selected_position = selected_track_index.has_value()
        ? std::ranges::find(eligible_track_indices, *selected_track_index)
        : eligible_track_indices.end();
    const bool selected_outside_track_budget = selected_position != eligible_track_indices.end()
        && static_cast<std::size_t>(selected_position - eligible_track_indices.begin()) >= track_count;
    const std::size_t general_track_count = track_count
        - (selected_outside_track_budget ? 1 : 0);
    std::vector<std::size_t> track_indices;
    track_indices.reserve(track_count);
    for (std::size_t i = 0; i < general_track_count; ++i)
        track_indices.push_back(eligible_track_indices[i]);

    if (selected_outside_track_budget)
        track_indices.push_back(*selected_track_index);

    result.tracks.resize(track_indices.size());
    std::vector<unsigned char> track_valid(track_indices.size(), 0);
    const auto propagate_track_range = [&](std::size_t begin, std::size_t end) {
        for (std::size_t output_index = begin; output_index < end; ++output_index)
        {
            const std::size_t model_index = track_indices[output_index];
            append_track_samples(
                orbits[model_index],
                entries[model_index],
                simulation_unix_seconds,
                settings,
                result.tracks[output_index]);
            track_valid[output_index] = static_cast<unsigned char>(
                !result.tracks[output_index].ecef_points_km.empty());
        }
    };
    if (executor)
        executor->parallel_for(track_indices.size(), 8, propagate_track_range);
    else
        propagate_track_range(0, track_indices.size());

    std::size_t output_track_index = 0;
    for (std::size_t i = 0; i < result.tracks.size(); ++i)
    {
        if (!track_valid[i])
            continue;
        if (output_track_index != i)
            result.tracks[output_track_index] = std::move(result.tracks[i]);
        ++output_track_index;
    }
    result.tracks.resize(output_track_index);

    return result;
}

std::optional<SatelliteOrbitTrack> propagate_satellite_track(
    const SatellitePropagationModel& model,
    std::int64_t norad_catalog_id,
    double simulation_unix_seconds,
    std::size_t sample_count)
{
    if (!std::isfinite(simulation_unix_seconds) || sample_count == 0)
        return std::nullopt;

    const auto& entries = model.entries();
    const auto& orbits = SatellitePropagationRunnerAccess::orbits(model);
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        if (entries[i].norad_catalog_id != norad_catalog_id)
            continue;

        SatellitePropagationSettings settings;
        settings.track_sample_count = sample_count;
        SatelliteOrbitTrack track;
        append_track_samples(orbits[i], entries[i], simulation_unix_seconds, settings, track);
        if (track.ecef_points_km.empty())
            return std::nullopt;
        return track;
    }
    return std::nullopt;
}

} // namespace draxul::satview
