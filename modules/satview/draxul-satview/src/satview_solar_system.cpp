#include "satview_solar_system.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace draxul::satview
{

namespace
{

constexpr double kSecondsPerDay = 86400.0;
constexpr double kJ2000UnixSeconds = 946728000.0;

// Mean physical and osculating-orbit values are intentionally presentation
// data, not a navigation ephemeris. Natural-satellite values are relative to
// their parent and planet values are heliocentric. JPL Horizons remains the
// source of truth when a sampled mission/object ephemeris is available.
constexpr std::array kBodies = {
    SatViewSolarSystemBody{ SatViewCameraPov::Sun, "Sun", "sun", "Solar system", std::nullopt,
        695700.0, 695700.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 609.12,
        { 1.0f, 0.55f, 0.12f }, "textures/sun_solar_system_scope_4k.jpg", true },
    SatViewSolarSystemBody{ SatViewCameraPov::Mercury, "Mercury", "mercury", "Solar system", SatViewCameraPov::Sun,
        2439.7, 2439.7, 57909227.0, 0.20563, 87.9691, 7.005, 48.331, 174.796, 1407.6,
        { 0.57f, 0.55f, 0.52f }, "textures/mercury_solar_system_scope_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Venus, "Venus", "venus", "Solar system", SatViewCameraPov::Sun,
        6051.8, 6051.8, 108209475.0, 0.00677, 224.701, 3.3946, 76.680, 50.115, -5832.5,
        { 0.90f, 0.71f, 0.42f }, "textures/venus_solar_system_scope_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Earth, "Earth", "earth", "Earth system", SatViewCameraPov::Sun,
        6378.137, 6356.752, 149598262.0, 0.01671, 365.256, 0.000, -11.261, 357.517, 23.9345,
        { 0.25f, 0.55f, 0.95f }, "textures/earth_day_8k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Moon, "Moon", "moon", "Earth system", SatViewCameraPov::Earth,
        1738.1, 1736.0, 384400.0, 0.0549, 27.321661, 5.145, 125.08, 135.27, 655.728,
        { 0.72f, 0.72f, 0.70f }, "textures/moon_lroc_8k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Mars, "Mars", "mars", "Mars system", SatViewCameraPov::Sun,
        3396.2, 3376.2, 227943824.0, 0.0934, 686.980, 1.850, 49.558, 19.373, 24.6229,
        { 0.82f, 0.34f, 0.20f }, "textures/mars_solar_system_scope_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Phobos, "Phobos", "phobos", "Mars system", SatViewCameraPov::Mars,
        13.0, 9.1, 9376.0, 0.0151, 0.31891, 1.093, 0.0, 35.0, 7.6538,
        { 0.48f, 0.43f, 0.38f }, "textures/phobos_usgs_1k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Deimos, "Deimos", "deimos", "Mars system", SatViewCameraPov::Mars,
        7.8, 5.1, 23463.2, 0.0002, 1.26244, 0.93, 0.0, 170.0, 30.2986,
        { 0.58f, 0.54f, 0.49f }, "textures/deimos_procedural_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Jupiter, "Jupiter", "jupiter", "Jupiter system", SatViewCameraPov::Sun,
        71492.0, 66854.0, 778340821.0, 0.0489, 4332.59, 1.304, 100.464, 20.020, 9.925,
        { 0.79f, 0.62f, 0.46f }, "textures/jupiter_solar_system_scope_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Io, "Io", "io", "Jupiter system", SatViewCameraPov::Jupiter,
        1829.4, 1815.7, 421700.0, 0.0041, 1.76914, 0.036, 0.0, 40.0, 42.459,
        { 0.91f, 0.76f, 0.30f }, "textures/io_usgs_1k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Europa, "Europa", "europa", "Jupiter system", SatViewCameraPov::Jupiter,
        1562.6, 1559.5, 671034.0, 0.009, 3.55118, 0.466, 0.0, 120.0, 85.228,
        { 0.76f, 0.69f, 0.57f }, "textures/europa_usgs_1k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Ganymede, "Ganymede", "ganymede", "Jupiter system", SatViewCameraPov::Jupiter,
        2634.1, 2631.2, 1070412.0, 0.0013, 7.15455, 0.177, 0.0, 230.0, 171.709,
        { 0.56f, 0.51f, 0.44f }, "textures/ganymede_usgs_1k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Callisto, "Callisto", "callisto", "Jupiter system", SatViewCameraPov::Jupiter,
        2410.3, 2409.2, 1882709.0, 0.0074, 16.6890, 0.192, 0.0, 310.0, 400.536,
        { 0.39f, 0.36f, 0.32f }, "textures/callisto_usgs_1k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Saturn, "Saturn", "saturn", "Saturn system", SatViewCameraPov::Sun,
        60268.0, 54364.0, 1426666422.0, 0.0565, 10759.22, 2.485, 113.665, 317.020, 10.656,
        { 0.88f, 0.77f, 0.55f }, "textures/saturn_solar_system_scope_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Mimas, "Mimas", "mimas", "Saturn system", SatViewCameraPov::Saturn,
        207.4, 190.6, 185539.0, 0.0196, 0.94242, 1.574, 0.0, 15.0, 22.618,
        { 0.70f, 0.70f, 0.68f }, "textures/mimas_procedural_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Enceladus, "Enceladus", "enceladus", "Saturn system", SatViewCameraPov::Saturn,
        256.6, 248.3, 238037.0, 0.0047, 1.37022, 0.009, 0.0, 65.0, 32.885,
        { 0.85f, 0.88f, 0.90f }, "textures/enceladus_usgs_1k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Tethys, "Tethys", "tethys", "Saturn system", SatViewCameraPov::Saturn,
        538.4, 526.3, 294672.0, 0.0001, 1.88780, 1.091, 0.0, 125.0, 45.307,
        { 0.75f, 0.76f, 0.76f }, "textures/tethys_procedural_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Dione, "Dione", "dione", "Saturn system", SatViewCameraPov::Saturn,
        563.8, 558.3, 377415.0, 0.0022, 2.73692, 0.028, 0.0, 185.0, 65.686,
        { 0.72f, 0.73f, 0.74f }, "textures/dione_procedural_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Rhea, "Rhea", "rhea", "Saturn system", SatViewCameraPov::Saturn,
        765.0, 762.4, 527068.0, 0.0010, 4.51821, 0.345, 0.0, 245.0, 108.437,
        { 0.67f, 0.67f, 0.66f }, "textures/rhea_procedural_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Titan, "Titan", "titan", "Saturn system", SatViewCameraPov::Saturn,
        2575.2, 2574.3, 1221870.0, 0.0288, 15.9454, 0.349, 0.0, 300.0, 382.69,
        { 0.80f, 0.55f, 0.20f }, "textures/titan_usgs_1k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Iapetus, "Iapetus", "iapetus", "Saturn system", SatViewCameraPov::Saturn,
        746.0, 712.0, 3560820.0, 0.0283, 79.3215, 15.47, 0.0, 350.0, 1903.7,
        { 0.52f, 0.48f, 0.43f }, "textures/iapetus_usgs_1k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Uranus, "Uranus", "uranus", "Uranus system", SatViewCameraPov::Sun,
        25559.0, 24973.0, 2870658186.0, 0.0457, 30688.5, 0.772, 74.006, 142.238, -17.24,
        { 0.48f, 0.82f, 0.86f }, "textures/uranus_solar_system_scope_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Miranda, "Miranda", "miranda", "Uranus system", SatViewCameraPov::Uranus,
        240.4, 232.9, 129390.0, 0.0013, 1.41348, 4.338, 0.0, 45.0, -33.923,
        { 0.69f, 0.70f, 0.70f }, "textures/miranda_procedural_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Ariel, "Ariel", "ariel", "Uranus system", SatViewCameraPov::Uranus,
        581.1, 577.7, 191020.0, 0.0012, 2.52038, 0.041, 0.0, 110.0, -60.489,
        { 0.72f, 0.74f, 0.75f }, "textures/ariel_procedural_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Umbriel, "Umbriel", "umbriel", "Uranus system", SatViewCameraPov::Uranus,
        584.7, 584.7, 266300.0, 0.0039, 4.14418, 0.128, 0.0, 180.0, -99.460,
        { 0.37f, 0.37f, 0.38f }, "textures/umbriel_procedural_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Titania, "Titania", "titania", "Uranus system", SatViewCameraPov::Uranus,
        788.9, 788.9, 435910.0, 0.0011, 8.70587, 0.079, 0.0, 250.0, -208.94,
        { 0.62f, 0.62f, 0.62f }, "textures/titania_procedural_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Oberon, "Oberon", "oberon", "Uranus system", SatViewCameraPov::Uranus,
        761.4, 761.4, 583520.0, 0.0014, 13.4632, 0.068, 0.0, 320.0, -323.12,
        { 0.50f, 0.47f, 0.45f }, "textures/oberon_procedural_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Neptune, "Neptune", "neptune", "Neptune system", SatViewCameraPov::Sun,
        24764.0, 24341.0, 4498396441.0, 0.0113, 60182.0, 1.770, 131.784, 256.228, 16.11,
        { 0.23f, 0.39f, 0.92f }, "textures/neptune_solar_system_scope_2k.jpg" },
    SatViewSolarSystemBody{ SatViewCameraPov::Triton, "Triton", "triton", "Neptune system", SatViewCameraPov::Neptune,
        1353.4, 1352.6, 354759.0, 0.000016, 5.87685, 156.865, 0.0, 75.0, -141.044,
        { 0.70f, 0.64f, 0.60f }, "textures/triton_procedural_2k.jpg" },
};

constexpr std::array<SatViewRingBand, 7> kSaturnRingBands = {
    SatViewRingBand{ 1.22, 1.43, { 0.76f, 0.67f, 0.50f, 0.36f } },
    SatViewRingBand{ 1.46, 1.66, { 0.92f, 0.84f, 0.65f, 0.52f } },
    SatViewRingBand{ 1.68, 1.92, { 0.72f, 0.60f, 0.45f, 0.16f } },
    SatViewRingBand{ 1.95, 2.02, { 0.12f, 0.09f, 0.07f, 0.10f } },
    SatViewRingBand{ 2.05, 2.25, { 0.95f, 0.88f, 0.70f, 0.58f } },
    SatViewRingBand{ 2.28, 2.38, { 0.70f, 0.58f, 0.42f, 0.22f } },
    SatViewRingBand{ 2.42, 2.55, { 0.96f, 0.90f, 0.75f, 0.32f } },
};

double solve_eccentric_anomaly(double mean_anomaly, double eccentricity)
{
    double eccentric_anomaly = mean_anomaly;
    for (int iteration = 0; iteration < 8; ++iteration)
    {
        const double residual = eccentric_anomaly
            - eccentricity * std::sin(eccentric_anomaly)
            - mean_anomaly;
        eccentric_anomaly -= residual / (1.0 - eccentricity * std::cos(eccentric_anomaly));
    }
    return eccentric_anomaly;
}

glm::dvec3 orbital_position(const SatViewSolarSystemBody& body, double mean_anomaly)
{
    const double eccentric_anomaly = solve_eccentric_anomaly(mean_anomaly, body.eccentricity);
    const double x = body.semi_major_axis_km
        * (std::cos(eccentric_anomaly) - body.eccentricity);
    const double z = body.semi_major_axis_km
        * std::sqrt(1.0 - body.eccentricity * body.eccentricity)
        * std::sin(eccentric_anomaly);
    const double inclination = glm::radians(body.inclination_degrees);
    const double node = glm::radians(body.ascending_node_degrees);
    const double ci = std::cos(inclination);
    const double si = std::sin(inclination);
    const double cn = std::cos(node);
    const double sn = std::sin(node);
    return glm::dvec3(
        cn * x - sn * ci * z,
        si * z,
        sn * x + cn * ci * z);
}

} // namespace

std::span<const SatViewSolarSystemBody> satview_solar_system_bodies()
{
    return kBodies;
}

const SatViewSolarSystemBody& satview_solar_system_body(SatViewCameraPov id)
{
    const auto found = std::ranges::find(kBodies, id, &SatViewSolarSystemBody::id);
    return found == kBodies.end() ? kBodies.front() : *found;
}

std::optional<SatViewCameraPov> satview_camera_pov_from_config_name(std::string_view name)
{
    const auto found = std::ranges::find(kBodies, name, &SatViewSolarSystemBody::config_name);
    if (found == kBodies.end())
        return std::nullopt;
    return found->id;
}

bool satview_uses_generic_body_view(SatViewCameraPov id)
{
    return id != SatViewCameraPov::Earth && id != SatViewCameraPov::Moon;
}

std::vector<const SatViewSolarSystemBody*> satview_child_bodies(SatViewCameraPov parent)
{
    std::vector<const SatViewSolarSystemBody*> children;
    for (const SatViewSolarSystemBody& body : kBodies)
    {
        if (body.parent == parent)
            children.push_back(&body);
    }
    return children;
}

glm::dvec3 satview_body_position_in_parent_frame(
    const SatViewSolarSystemBody& body,
    double unix_seconds)
{
    if (!body.parent.has_value() || body.orbital_period_days == 0.0)
        return glm::dvec3(0.0);
    const double elapsed_days = (unix_seconds - kJ2000UnixSeconds) / kSecondsPerDay;
    const double phase = glm::radians(body.phase_degrees)
        + 2.0 * std::numbers::pi * elapsed_days / body.orbital_period_days;
    return orbital_position(body, std::remainder(phase, 2.0 * std::numbers::pi));
}

glm::dvec3 satview_body_position_in_solar_frame(
    SatViewCameraPov id,
    double unix_seconds)
{
    const SatViewSolarSystemBody& body = satview_solar_system_body(id);
    if (!body.parent.has_value())
        return glm::dvec3(0.0);
    return satview_body_position_in_solar_frame(*body.parent, unix_seconds)
        + satview_body_position_in_parent_frame(body, unix_seconds);
}

std::optional<SatViewContextBodyProxy> satview_context_body_proxy(
    SatViewCameraPov focus,
    SatViewCameraPov target,
    const glm::dvec3& observer_position_focus_radii,
    double unix_seconds,
    double proxy_distance_focus_radii)
{
    if (focus == target || proxy_distance_focus_radii <= 0.0)
        return std::nullopt;

    const SatViewSolarSystemBody& focus_body = satview_solar_system_body(focus);
    const SatViewSolarSystemBody& target_body = satview_solar_system_body(target);
    const glm::dvec3 target_position_focus_radii = (satview_body_position_in_solar_frame(target, unix_seconds)
                                                       - satview_body_position_in_solar_frame(focus, unix_seconds))
        / focus_body.equatorial_radius_km;
    const glm::dvec3 observer_to_target = target_position_focus_radii - observer_position_focus_radii;
    const double observer_distance = glm::length(observer_to_target);
    const double target_radius_focus_radii = target_body.equatorial_radius_km / focus_body.equatorial_radius_km;
    if (observer_distance <= target_radius_focus_radii)
        return std::nullopt;

    const double sine_angular_radius = std::clamp(
        target_radius_focus_radii / observer_distance,
        0.0,
        1.0);
    SatViewContextBodyProxy proxy;
    proxy.position_focus_radii = observer_position_focus_radii
        + observer_to_target / observer_distance * proxy_distance_focus_radii;
    proxy.radius_focus_radii = proxy_distance_focus_radii * sine_angular_radius;
    proxy.angular_radius_radians = std::asin(sine_angular_radius);
    return proxy;
}

std::vector<glm::dvec3> satview_body_orbit_in_parent_frame(
    const SatViewSolarSystemBody& body,
    std::size_t segment_count)
{
    std::vector<glm::dvec3> points;
    if (!body.parent.has_value() || body.semi_major_axis_km <= 0.0 || segment_count < 3)
        return points;
    points.reserve(segment_count);
    for (std::size_t segment = 0; segment < segment_count; ++segment)
    {
        const double anomaly = 2.0 * std::numbers::pi
            * static_cast<double>(segment) / static_cast<double>(segment_count);
        points.push_back(orbital_position(body, anomaly));
    }
    return points;
}

std::vector<SatViewBodyOrbitTrack> satview_child_orbit_tracks(
    SatViewCameraPov parent_id,
    const SatViewPlanetTrackConfig& planet_tracks,
    std::size_t segment_count)
{
    std::vector<SatViewBodyOrbitTrack> tracks;
    const SatViewSolarSystemBody& parent = satview_solar_system_body(parent_id);
    for (const SatViewSolarSystemBody* child : satview_child_bodies(parent_id))
    {
        if (parent_id == SatViewCameraPov::Sun
            && !satview_planet_track_enabled(planet_tracks, child->id))
        {
            continue;
        }
        auto points = satview_body_orbit_in_parent_frame(
            *child,
            std::max<std::size_t>(segment_count, 3));
        if (points.size() < 3)
            continue;
        for (glm::dvec3& point : points)
            point /= parent.equatorial_radius_km;
        const double apoapsis = child->semi_major_axis_km * (1.0 + child->eccentricity)
            / parent.equatorial_radius_km;
        tracks.push_back({
            child->id,
            std::move(points),
            apoapsis,
            glm::vec4(child->display_color, 0.72f),
        });
    }
    return tracks;
}

std::vector<SatViewBodyRenderInstance> satview_child_body_instances(
    SatViewCameraPov parent_id,
    double unix_seconds)
{
    std::vector<SatViewBodyRenderInstance> instances;
    const SatViewSolarSystemBody& parent = satview_solar_system_body(parent_id);
    for (const SatViewSolarSystemBody* child : satview_child_bodies(parent_id))
    {
        instances.push_back({
            child->id,
            satview_body_position_in_parent_frame(*child, unix_seconds)
                / parent.equatorial_radius_km,
            child->equatorial_radius_km / parent.equatorial_radius_km,
            satview_body_rotation_radians(*child, unix_seconds),
            child->polar_radius_km / child->equatorial_radius_km,
            child->emissive,
        });
    }
    return instances;
}

std::span<const SatViewRingBand> satview_planetary_ring_bands(SatViewCameraPov body)
{
    if (body == SatViewCameraPov::Saturn)
        return kSaturnRingBands;
    return {};
}

glm::dvec3 satview_body_sun_direction(SatViewCameraPov id, double unix_seconds)
{
    const SatViewSolarSystemBody* body = &satview_solar_system_body(id);
    while (body->parent.has_value() && *body->parent != SatViewCameraPov::Sun)
        body = &satview_solar_system_body(*body->parent);
    if (!body->parent.has_value())
        return glm::dvec3(1.0, 0.0, 0.0);
    const glm::dvec3 heliocentric = satview_body_position_in_parent_frame(*body, unix_seconds);
    const double length = glm::length(heliocentric);
    return length > 0.0 ? -heliocentric / length : glm::dvec3(1.0, 0.0, 0.0);
}

double satview_body_rotation_radians(
    const SatViewSolarSystemBody& body,
    double unix_seconds)
{
    if (body.rotation_period_hours == 0.0)
        return 0.0;
    const double elapsed_hours = (unix_seconds - kJ2000UnixSeconds) / 3600.0;
    return std::remainder(
        2.0 * std::numbers::pi * elapsed_hours / body.rotation_period_hours,
        2.0 * std::numbers::pi);
}

} // namespace draxul::satview
