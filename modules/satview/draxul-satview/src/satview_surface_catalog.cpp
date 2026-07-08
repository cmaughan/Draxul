#include "satview_surface_catalog.h"

#include "satview_texture_assets.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

namespace draxul::satview
{

namespace
{

using CsvRow = std::vector<std::string>;

std::string_view surface_catalog_label(CentralBody body)
{
    return central_body_name(body);
}

bool parse_csv_rows(std::string_view csv, std::vector<CsvRow>& rows, std::string& error)
{
    CsvRow row;
    std::string field;
    bool quoted = false;
    bool quote_closed = false;

    auto finish_field = [&]() {
        row.push_back(std::move(field));
        field.clear();
        quote_closed = false;
    };
    auto finish_row = [&]() {
        finish_field();
        rows.push_back(std::move(row));
        row.clear();
    };

    for (std::size_t index = 0; index < csv.size(); ++index)
    {
        const char value = csv[index];
        if (quoted)
        {
            if (value == '"')
            {
                if (index + 1 < csv.size() && csv[index + 1] == '"')
                {
                    field.push_back('"');
                    ++index;
                }
                else
                {
                    quoted = false;
                    quote_closed = true;
                }
            }
            else
            {
                field.push_back(value);
            }
            continue;
        }

        if (quote_closed && value != ',' && value != '\r' && value != '\n')
        {
            error = "unexpected data after quoted surface CSV field";
            return false;
        }
        if (value == '"')
        {
            if (!field.empty() || quote_closed)
            {
                error = "unexpected quote in surface CSV field";
                return false;
            }
            quoted = true;
        }
        else if (value == ',')
        {
            finish_field();
        }
        else if (value == '\n')
        {
            finish_row();
        }
        else if (value == '\r')
        {
            if (index + 1 >= csv.size() || csv[index + 1] != '\n')
                finish_row();
        }
        else
        {
            field.push_back(value);
        }
    }
    if (quoted)
    {
        error = "unterminated quoted surface CSV field";
        return false;
    }
    if (!field.empty() || !row.empty() || quote_closed)
        finish_row();
    while (!rows.empty() && rows.back().size() == 1 && rows.back().front().empty())
        rows.pop_back();
    return true;
}

std::optional<double> parse_optional_double(std::string_view text)
{
    if (text.empty())
        return std::nullopt;
    const std::string value(text);
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end != value.c_str() + value.size() || !std::isfinite(parsed))
        return std::nullopt;
    return parsed;
}

std::optional<int> parse_optional_int(std::string_view text)
{
    if (text.empty())
        return std::nullopt;
    int parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size())
        return std::nullopt;
    return parsed;
}

SatViewSurfaceKind parse_kind(std::string_view value)
{
    if (value == "lander")
        return SatViewSurfaceKind::Lander;
    if (value == "rover")
        return SatViewSurfaceKind::Rover;
    if (value == "deployed_instrument")
        return SatViewSurfaceKind::DeployedInstrument;
    if (value == "retroreflector")
        return SatViewSurfaceKind::Retroreflector;
    if (value == "impact_site")
        return SatViewSurfaceKind::ImpactSite;
    if (value == "rocket_stage")
        return SatViewSurfaceKind::RocketStage;
    if (value == "crewed_artifact")
        return SatViewSurfaceKind::CrewedArtifact;
    return SatViewSurfaceKind::Unknown;
}

SatViewSurfaceLocationQuality parse_quality(std::string_view value)
{
    if (value == "confirmed")
        return SatViewSurfaceLocationQuality::Confirmed;
    if (value == "reported")
        return SatViewSurfaceLocationQuality::Reported;
    if (value == "approximate")
        return SatViewSurfaceLocationQuality::Approximate;
    if (value == "site_only")
        return SatViewSurfaceLocationQuality::SiteOnly;
    return SatViewSurfaceLocationQuality::Unknown;
}

std::vector<std::string> split_references(std::string_view value)
{
    std::vector<std::string> references;
    while (!value.empty())
    {
        const std::size_t separator = value.find(" | ");
        const std::string_view reference = value.substr(0, separator);
        if (!reference.empty())
            references.emplace_back(reference);
        if (separator == std::string_view::npos)
            break;
        value.remove_prefix(separator + 3);
    }
    return references;
}

} // namespace

bool SatViewSurfaceObject::renderable() const
{
    return location_quality != SatViewSurfaceLocationQuality::SiteOnly
        && location_quality != SatViewSurfaceLocationQuality::Unknown
        && std::isfinite(latitude_degrees)
        && std::isfinite(longitude_east_degrees)
        && latitude_degrees >= -90.0
        && latitude_degrees <= 90.0
        && longitude_east_degrees >= -180.0
        && longitude_east_degrees <= 180.0;
}

std::string_view satview_surface_kind_name(SatViewSurfaceKind kind)
{
    switch (kind)
    {
    case SatViewSurfaceKind::Lander:
        return "Lander";
    case SatViewSurfaceKind::Rover:
        return "Rover";
    case SatViewSurfaceKind::DeployedInstrument:
        return "Deployed instrument";
    case SatViewSurfaceKind::Retroreflector:
        return "Retroreflector";
    case SatViewSurfaceKind::ImpactSite:
        return "Impact site";
    case SatViewSurfaceKind::RocketStage:
        return "Rocket stage";
    case SatViewSurfaceKind::CrewedArtifact:
        return "Crewed artifact";
    case SatViewSurfaceKind::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

std::string_view satview_surface_location_quality_name(
    SatViewSurfaceLocationQuality quality)
{
    switch (quality)
    {
    case SatViewSurfaceLocationQuality::Confirmed:
        return "Confirmed";
    case SatViewSurfaceLocationQuality::Reported:
        return "Reported";
    case SatViewSurfaceLocationQuality::Approximate:
        return "Approximate";
    case SatViewSurfaceLocationQuality::SiteOnly:
        return "Site only";
    case SatViewSurfaceLocationQuality::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

SatViewSurfaceCatalog parse_satview_surface_catalog(
    std::string_view csv,
    CentralBody body)
{
    SatViewSurfaceCatalog result;
    result.body = body;
    std::vector<CsvRow> rows;
    if (!parse_csv_rows(csv, rows, result.error))
        return result;
    if (rows.empty())
    {
        result.error = std::string(surface_catalog_label(body)) + " surface catalogue is empty";
        return result;
    }

    std::unordered_map<std::string, std::size_t> columns;
    for (std::size_t index = 0; index < rows.front().size(); ++index)
        columns.emplace(rows.front()[index], index);
    const auto required_column = [&](std::string_view name) -> std::optional<std::size_t> {
        const auto found = columns.find(std::string(name));
        if (found == columns.end())
        {
            result.error = std::string(surface_catalog_label(body))
                + " surface catalogue missing required column " + std::string(name);
            return std::nullopt;
        }
        return found->second;
    };
    const auto id_column = required_column("ID");
    const auto mission_id_column = required_column("MISSION_ID");
    const auto mission_name_column = required_column("MISSION_NAME");
    const auto site_column = required_column("PARENT_SITE_ID");
    const auto name_column = required_column("DISPLAY_NAME");
    const auto kind_column = required_column("KIND");
    const auto latitude_column = required_column("LATITUDE_DEGREES");
    const auto longitude_column = required_column("LONGITUDE_EAST_DEGREES");
    const auto quality_column = required_column("LOCATION_QUALITY");
    if (!id_column || !mission_id_column || !mission_name_column || !site_column
        || !name_column || !kind_column || !latitude_column || !longitude_column
        || !quality_column)
    {
        return result;
    }

    const auto field = [&](const CsvRow& row, std::string_view name) -> std::string_view {
        const auto found = columns.find(std::string(name));
        if (found == columns.end() || found->second >= row.size())
            return {};
        return row[found->second];
    };
    std::unordered_set<std::string> ids;
    std::unordered_set<std::string> sites;
    result.objects.reserve(rows.size() - 1);
    for (std::size_t row_index = 1; row_index < rows.size(); ++row_index)
    {
        const CsvRow& row = rows[row_index];
        SatViewSurfaceObject object;
        object.body = body;
        object.id = std::string(field(row, "ID"));
        object.mission_id = std::string(field(row, "MISSION_ID"));
        object.mission_name = std::string(field(row, "MISSION_NAME"));
        object.parent_site_id = std::string(field(row, "PARENT_SITE_ID"));
        object.display_name = std::string(field(row, "DISPLAY_NAME"));
        object.vehicle_name = std::string(field(row, "VEHICLE_NAME"));
        object.kind = parse_kind(field(row, "KIND"));
        object.source_kind = std::string(field(row, "SOURCE_KIND"));
        object.status = std::string(field(row, "STATUS"));
        const auto latitude = parse_optional_double(field(row, "LATITUDE_DEGREES"));
        const auto longitude = parse_optional_double(field(row, "LONGITUDE_EAST_DEGREES"));
        object.coordinate_uncertainty_m = parse_optional_double(field(row, "COORDINATE_UNCERTAINTY_M"));
        object.radial_distance_m = parse_optional_double(field(row, "RADIAL_DISTANCE_M"));
        object.arrival_date = std::string(field(row, "ARRIVAL_DATE"));
        object.location_quality = parse_quality(field(row, "LOCATION_QUALITY"));
        object.coordinate_source = std::string(field(row, "COORDINATE_SOURCE"));
        object.radial_source = std::string(field(row, "RADIUS_SOURCE"));
        object.cospar_id = std::string(field(row, "COSPAR_ID"));
        object.gcat_id = std::string(field(row, "GCAT_ID"));
        object.owner = std::string(field(row, "OWNER"));
        object.country = std::string(field(row, "COUNTRY"));
        object.references = split_references(field(row, "REFERENCES"));
        object.display_rank = parse_optional_int(field(row, "DISPLAY_RANK")).value_or(0);
        object.site_representative = parse_optional_int(field(row, "SITE_REPRESENTATIVE")).value_or(0) != 0;
        object.crewed_mission = parse_optional_int(field(row, "CREWED_MISSION")).value_or(0) != 0;
        if (object.id.empty() || object.mission_id.empty() || object.parent_site_id.empty()
            || object.display_name.empty())
        {
            result.error = "invalid " + std::string(surface_catalog_label(body))
                + " surface catalogue row " + std::to_string(row_index + 1);
            result.objects.clear();
            return result;
        }
        if (latitude && longitude)
        {
            object.latitude_degrees = *latitude;
            object.longitude_east_degrees = *longitude;
        }
        else if (object.location_quality != SatViewSurfaceLocationQuality::SiteOnly
            && object.location_quality != SatViewSurfaceLocationQuality::Unknown)
        {
            result.error = "renderable " + std::string(surface_catalog_label(body))
                + " surface object lacks coordinates " + object.id;
            result.objects.clear();
            return result;
        }
        if ((!object.renderable()
                && object.location_quality != SatViewSurfaceLocationQuality::SiteOnly
                && object.location_quality != SatViewSurfaceLocationQuality::Unknown)
            || !ids.insert(object.id).second)
        {
            result.error = "invalid or duplicate " + std::string(surface_catalog_label(body))
                + " surface object " + object.id;
            result.objects.clear();
            return result;
        }
        sites.insert(object.parent_site_id);
        result.objects.push_back(std::move(object));
    }
    result.site_count = sites.size();
    return result;
}

SatViewSurfaceCatalog load_satview_surface_catalog(
    std::string_view asset_path,
    CentralBody body)
{
    const auto path = resolve_satview_asset_path(std::filesystem::path(asset_path));
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
        return { body, {}, 0, "failed to open " + path.string() };
    const std::string content{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    if (input.bad())
        return { body, {}, 0, "failed to read " + path.string() };
    return parse_satview_surface_catalog(content, body);
}

SatViewSurfaceCatalog load_satview_mars_surface_catalog()
{
    return load_satview_surface_catalog("catalog/mars_surface_objects.csv", CentralBody::Mars);
}

} // namespace draxul::satview
