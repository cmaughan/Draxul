#include <draxul/satview/satview_catalog.h>

#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/runtime_path.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace draxul::satview
{

namespace
{

constexpr double kEarthMuKm3PerS2 = 398600.4418;
constexpr double kEarthEquatorialRadiusKm = 6378.137;
constexpr double kSecondsPerDay = 86400.0;
constexpr double kMinutesPerDay = 1440.0;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kEarthJ2 = 1.08262668e-3;
constexpr double kMeanSolarNodalRateDegPerDay = 360.0 / 365.2421897;
constexpr double kSunSynchronousRateToleranceDegPerDay = 0.02;

bool is_sun_synchronous_candidate(
    double mean_motion_rev_per_day,
    double eccentricity,
    double inclination_deg)
{
    if (!std::isfinite(mean_motion_rev_per_day) || mean_motion_rev_per_day <= 0.0
        || !std::isfinite(eccentricity) || eccentricity < 0.0 || eccentricity >= 1.0
        || !std::isfinite(inclination_deg) || inclination_deg < 0.0 || inclination_deg > 180.0)
    {
        return false;
    }

    const double mean_motion_rad_s = mean_motion_rev_per_day * kTwoPi / kSecondsPerDay;
    const double semi_major_axis_km = std::cbrt(
        kEarthMuKm3PerS2 / (mean_motion_rad_s * mean_motion_rad_s));
    const double semi_latus_rectum_km = semi_major_axis_km * (1.0 - eccentricity * eccentricity);
    if (!std::isfinite(semi_latus_rectum_km) || semi_latus_rectum_km <= 0.0)
        return false;

    const double inclination_radians = inclination_deg * kTwoPi / 360.0;
    const double nodal_rate_rad_s = -1.5 * kEarthJ2 * mean_motion_rad_s
        * std::pow(kEarthEquatorialRadiusKm / semi_latus_rectum_km, 2.0)
        * std::cos(inclination_radians);
    const double nodal_rate_deg_per_day = nodal_rate_rad_s * kSecondsPerDay * 360.0 / kTwoPi;
    return std::isfinite(nodal_rate_deg_per_day)
        && std::abs(nodal_rate_deg_per_day - kMeanSolarNodalRateDegPerDay)
            <= kSunSynchronousRateToleranceDegPerDay;
}

SatellitePopulation population_for_kind(SatelliteObjectKind kind, bool active_payload)
{
    switch (kind)
    {
    case SatelliteObjectKind::Payload:
        return active_payload ? SatellitePopulation::ActivePayload : SatellitePopulation::InactivePayload;
    case SatelliteObjectKind::RocketBody:
        return SatellitePopulation::RocketBody;
    case SatelliteObjectKind::Debris:
        return SatellitePopulation::Debris;
    case SatelliteObjectKind::Unknown:
        return SatellitePopulation::Unknown;
    }
    return SatellitePopulation::Unknown;
}

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
    return lowercase_copy(haystack).find(lowercase_copy(needle)) != std::string::npos;
}

struct JsonValue
{
    enum class Type
    {
        Null,
        String,
        Number,
        Boolean
    };

    Type type = Type::Null;
    std::string string_value;
    double number_value = 0.0;
    bool bool_value = false;
};

using JsonObject = std::unordered_map<std::string, JsonValue>;

void append_utf8(std::string& out, uint32_t codepoint)
{
    if (codepoint <= 0x7F)
    {
        out.push_back(static_cast<char>(codepoint));
    }
    else if (codepoint <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
    else if (codepoint <= 0xFFFF)
    {
        out.push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
}

int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

class JsonReader
{
public:
    explicit JsonReader(std::string_view input)
        : input_(input)
    {
    }

    bool parse_object_array(std::vector<JsonObject>& objects, std::string& error)
    {
        skip_ws();
        if (consume('{'))
        {
            JsonObject object;
            if (!parse_object_body(object, error))
                return false;
            objects.push_back(std::move(object));
            skip_ws();
            if (!at_end())
                return fail(error, "unexpected content after JSON object");
            return true;
        }

        if (!consume('['))
            return fail(error, "expected JSON array or object");
        skip_ws();
        if (consume(']'))
            return true;

        for (;;)
        {
            if (!consume('{'))
                return fail(error, "expected object in JSON array");

            JsonObject object;
            if (!parse_object_body(object, error))
                return false;
            objects.push_back(std::move(object));

            skip_ws();
            if (consume(']'))
                break;
            if (!consume(','))
                return fail(error, "expected ',' or ']'");
        }

        skip_ws();
        if (!at_end())
            return fail(error, "unexpected content after JSON array");
        return true;
    }

private:
    bool parse_object_body(JsonObject& object, std::string& error)
    {
        skip_ws();
        if (consume('}'))
            return true;

        for (;;)
        {
            std::string key;
            if (!parse_string(key, error))
                return false;
            skip_ws();
            if (!consume(':'))
                return fail(error, "expected ':' after object key");

            JsonValue value;
            if (!parse_value(value, error))
                return false;
            object.insert_or_assign(std::move(key), std::move(value));

            skip_ws();
            if (consume('}'))
                return true;
            if (!consume(','))
                return fail(error, "expected ',' or '}'");
        }
    }

    bool parse_value(JsonValue& value, std::string& error)
    {
        skip_ws();
        if (at_end())
            return fail(error, "unexpected end of JSON value");

        if (peek() == '"')
        {
            value.type = JsonValue::Type::String;
            return parse_string(value.string_value, error);
        }

        if (peek() == '-' || (peek() >= '0' && peek() <= '9'))
        {
            value.type = JsonValue::Type::Number;
            return parse_number(value.number_value, error);
        }

        if (consume_literal("true"))
        {
            value.type = JsonValue::Type::Boolean;
            value.bool_value = true;
            return true;
        }
        if (consume_literal("false"))
        {
            value.type = JsonValue::Type::Boolean;
            value.bool_value = false;
            return true;
        }
        if (consume_literal("null"))
        {
            value.type = JsonValue::Type::Null;
            return true;
        }

        if (consume('{'))
            return skip_object(error);
        if (consume('['))
            return skip_array(error);

        return fail(error, "unsupported JSON value");
    }

    bool skip_object(std::string& error)
    {
        skip_ws();
        if (consume('}'))
            return true;
        for (;;)
        {
            std::string key;
            if (!parse_string(key, error))
                return false;
            skip_ws();
            if (!consume(':'))
                return fail(error, "expected ':' in skipped object");
            JsonValue ignored;
            if (!parse_value(ignored, error))
                return false;
            skip_ws();
            if (consume('}'))
                return true;
            if (!consume(','))
                return fail(error, "expected ',' or '}' in skipped object");
        }
    }

    bool skip_array(std::string& error)
    {
        skip_ws();
        if (consume(']'))
            return true;
        for (;;)
        {
            JsonValue ignored;
            if (!parse_value(ignored, error))
                return false;
            skip_ws();
            if (consume(']'))
                return true;
            if (!consume(','))
                return fail(error, "expected ',' or ']' in skipped array");
        }
    }

    bool parse_string(std::string& out, std::string& error)
    {
        if (!consume('"'))
            return fail(error, "expected string");

        out.clear();
        while (!at_end())
        {
            const char c = input_[pos_++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20u)
                return fail(error, "control character in string");
            if (c != '\\')
            {
                out.push_back(c);
                continue;
            }

            if (at_end())
                return fail(error, "unterminated string escape");
            const char esc = input_[pos_++];
            switch (esc)
            {
            case '"':
            case '\\':
            case '/':
                out.push_back(esc);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u':
            {
                uint32_t codepoint = 0;
                if (!parse_hex4(codepoint))
                    return fail(error, "invalid unicode escape");

                if (codepoint >= 0xD800u && codepoint <= 0xDBFFu)
                {
                    const size_t saved = pos_;
                    if (pos_ + 2u <= input_.size() && input_[pos_] == '\\' && input_[pos_ + 1u] == 'u')
                    {
                        pos_ += 2u;
                        uint32_t low = 0;
                        if (parse_hex4(low) && low >= 0xDC00u && low <= 0xDFFFu)
                        {
                            codepoint = 0x10000u + ((codepoint - 0xD800u) << 10u) + (low - 0xDC00u);
                        }
                        else
                        {
                            pos_ = saved;
                        }
                    }
                }

                append_utf8(out, codepoint);
                break;
            }
            default:
                return fail(error, "invalid string escape");
            }
        }
        return fail(error, "unterminated string");
    }

    bool parse_hex4(uint32_t& out)
    {
        if (pos_ + 4u > input_.size())
            return false;
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const int digit = hex_digit(input_[pos_ + static_cast<size_t>(i)]);
            if (digit < 0)
                return false;
            value = (value << 4u) | static_cast<uint32_t>(digit);
        }
        pos_ += 4u;
        out = value;
        return true;
    }

    bool parse_number(double& out, std::string& error)
    {
        const size_t start = pos_;
        if (peek() == '-')
            ++pos_;
        if (at_end())
            return fail(error, "incomplete number");

        if (peek() == '0')
        {
            ++pos_;
        }
        else if (peek() >= '1' && peek() <= '9')
        {
            while (!at_end() && peek() >= '0' && peek() <= '9')
                ++pos_;
        }
        else
        {
            return fail(error, "invalid number");
        }

        if (!at_end() && peek() == '.')
        {
            ++pos_;
            if (at_end() || peek() < '0' || peek() > '9')
                return fail(error, "invalid number fraction");
            while (!at_end() && peek() >= '0' && peek() <= '9')
                ++pos_;
        }

        if (!at_end() && (peek() == 'e' || peek() == 'E'))
        {
            ++pos_;
            if (!at_end() && (peek() == '+' || peek() == '-'))
                ++pos_;
            if (at_end() || peek() < '0' || peek() > '9')
                return fail(error, "invalid number exponent");
            while (!at_end() && peek() >= '0' && peek() <= '9')
                ++pos_;
        }

        const std::string token(input_.substr(start, pos_ - start));
        char* end = nullptr;
        out = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size() || !std::isfinite(out))
            return fail(error, "invalid numeric value");
        return true;
    }

    void skip_ws()
    {
        while (!at_end())
        {
            const char c = input_[pos_];
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
                break;
            ++pos_;
        }
    }

    bool consume(char c)
    {
        skip_ws();
        if (at_end() || input_[pos_] != c)
            return false;
        ++pos_;
        return true;
    }

    bool consume_literal(std::string_view literal)
    {
        skip_ws();
        if (input_.substr(pos_, literal.size()) != literal)
            return false;
        pos_ += literal.size();
        return true;
    }

    [[nodiscard]] bool at_end() const
    {
        return pos_ >= input_.size();
    }

    [[nodiscard]] char peek() const
    {
        return input_[pos_];
    }

    bool fail(std::string& error, std::string_view message) const
    {
        error = std::string(message) + " at byte " + std::to_string(pos_);
        return false;
    }

    std::string_view input_;
    size_t pos_ = 0;
};

std::optional<std::string> string_field(const JsonObject& object, const char* key)
{
    auto it = object.find(key);
    if (it == object.end() || it->second.type != JsonValue::Type::String)
        return std::nullopt;
    return it->second.string_value;
}

std::optional<double> number_field(const JsonObject& object, const char* key)
{
    auto it = object.find(key);
    if (it == object.end())
        return std::nullopt;
    if (it->second.type == JsonValue::Type::Number)
        return it->second.number_value;
    if (it->second.type == JsonValue::Type::String)
    {
        char* end = nullptr;
        const double value = std::strtod(it->second.string_value.c_str(), &end);
        if (end == it->second.string_value.c_str() + it->second.string_value.size() && std::isfinite(value))
            return value;
    }
    return std::nullopt;
}

std::optional<int> int_field(const JsonObject& object, const char* key)
{
    const auto value = number_field(object, key);
    if (!value.has_value())
        return std::nullopt;
    if (*value < static_cast<double>(std::numeric_limits<int>::min())
        || *value > static_cast<double>(std::numeric_limits<int>::max()))
        return std::nullopt;
    return static_cast<int>(std::llround(*value));
}

std::optional<std::int64_t> int64_field(const JsonObject& object, const char* key)
{
    const auto value = number_field(object, key);
    if (!value.has_value())
        return std::nullopt;
    if (*value < static_cast<double>(std::numeric_limits<std::int64_t>::min())
        || *value > static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    return static_cast<std::int64_t>(std::llround(*value));
}

bool require_number(const JsonObject& object, const char* key, double& out)
{
    const auto value = number_field(object, key);
    if (!value.has_value())
        return false;
    out = *value;
    return true;
}

void derive_orbit_shape(SatelliteRecord& record)
{
    record.sun_synchronous_candidate = false;
    if (record.mean_motion_rev_per_day <= 0.0)
    {
        record.orbit_class = OrbitClass::Other;
        return;
    }

    record.period_minutes = kMinutesPerDay / record.mean_motion_rev_per_day;
    const double mean_motion_rad_s = record.mean_motion_rev_per_day * kTwoPi / kSecondsPerDay;
    const double semi_major_axis_km = std::cbrt(kEarthMuKm3PerS2 / (mean_motion_rad_s * mean_motion_rad_s));
    const double eccentricity = std::clamp(record.eccentricity, 0.0, 0.999999);
    record.perigee_km = semi_major_axis_km * (1.0 - eccentricity) - kEarthEquatorialRadiusKm;
    record.apogee_km = semi_major_axis_km * (1.0 + eccentricity) - kEarthEquatorialRadiusKm;

    if (record.period_minutes > 1300.0 && record.period_minutes < 1600.0
        && record.perigee_km > 30000.0 && record.apogee_km < 45000.0)
    {
        record.orbit_class = OrbitClass::Geosynchronous;
    }
    else if (eccentricity > 0.25 || record.apogee_km >= 50000.0)
    {
        record.orbit_class = OrbitClass::HighlyElliptical;
    }
    else if (record.apogee_km < 2000.0)
    {
        record.orbit_class = OrbitClass::LowEarth;
    }
    else if (record.perigee_km >= 2000.0 && record.apogee_km < 30000.0)
    {
        record.orbit_class = OrbitClass::MediumEarth;
    }
    else
    {
        record.orbit_class = OrbitClass::Other;
    }

    record.sun_synchronous_candidate = is_sun_synchronous_candidate(
        record.mean_motion_rev_per_day,
        eccentricity,
        record.inclination_deg);
}

SatelliteObjectKind derive_object_kind(std::string_view object_type, std::string_view object_name)
{
    if (contains_case_insensitive(object_type, "debris")
        || contains_case_insensitive(object_name, " debris")
        || contains_case_insensitive(object_name, " deb")
        || contains_case_insensitive(object_name, "(deb")
        || contains_case_insensitive(object_name, " deb)"))
    {
        return SatelliteObjectKind::Debris;
    }

    if (contains_case_insensitive(object_type, "rocket")
        || contains_case_insensitive(object_type, "r/b")
        || contains_case_insensitive(object_name, " rocket body")
        || contains_case_insensitive(object_name, " r/b")
        || contains_case_insensitive(object_name, "(r/b")
        || contains_case_insensitive(object_name, " rb"))
    {
        return SatelliteObjectKind::RocketBody;
    }

    if (contains_case_insensitive(object_type, "payload")
        || contains_case_insensitive(object_type, "satellite")
        || contains_case_insensitive(object_type, "spacecraft"))
    {
        return SatelliteObjectKind::Payload;
    }

    return SatelliteObjectKind::Unknown;
}

std::optional<SatelliteRecord> make_record(const JsonObject& object)
{
    SatelliteRecord record;
    const auto catalog_id = int64_field(object, "NORAD_CAT_ID");
    const auto epoch = string_field(object, "EPOCH");
    if (!catalog_id.has_value() || !epoch.has_value())
        return std::nullopt;

    record.norad_catalog_id = *catalog_id;
    record.epoch_utc = *epoch;
    record.object_name = string_field(object, "OBJECT_NAME").value_or(
        "CATNR " + std::to_string(record.norad_catalog_id));
    record.object_id = string_field(object, "OBJECT_ID").value_or("");
    record.object_type = string_field(object, "OBJECT_TYPE").value_or("");
    record.object_kind = derive_object_kind(record.object_type, record.object_name);
    record.population = population_for_kind(record.object_kind, true);
    if (record.object_kind == SatelliteObjectKind::Unknown)
        record.population = SatellitePopulation::ActivePayload;
    record.solution_kind = OrbitSolutionKind::GeneralPerturbations;
    record.classification_type = string_field(object, "CLASSIFICATION_TYPE").value_or("");

    if (!require_number(object, "MEAN_MOTION", record.mean_motion_rev_per_day)
        || !require_number(object, "ECCENTRICITY", record.eccentricity)
        || !require_number(object, "INCLINATION", record.inclination_deg)
        || !require_number(object, "RA_OF_ASC_NODE", record.right_ascension_ascending_node_deg)
        || !require_number(object, "ARG_OF_PERICENTER", record.argument_of_pericenter_deg)
        || !require_number(object, "MEAN_ANOMALY", record.mean_anomaly_deg))
    {
        return std::nullopt;
    }

    record.bstar = number_field(object, "BSTAR").value_or(0.0);
    record.mean_motion_dot = number_field(object, "MEAN_MOTION_DOT").value_or(0.0);
    record.mean_motion_ddot = number_field(object, "MEAN_MOTION_DDOT").value_or(0.0);
    record.ephemeris_type = int_field(object, "EPHEMERIS_TYPE").value_or(0);
    record.element_set_no = int_field(object, "ELEMENT_SET_NO").value_or(0);
    record.revolution_at_epoch = int_field(object, "REV_AT_EPOCH").value_or(0);

    derive_orbit_shape(record);
    return record;
}

std::filesystem::path resolve_satview_catalog_path(const std::filesystem::path& relative_path)
{
    const auto bundled = bundled_asset_path(std::filesystem::path("assets/satview") / relative_path);
    if (std::filesystem::exists(bundled))
        return bundled;

#ifdef DRAXUL_REPO_ROOT
    const auto repo_path = std::filesystem::path(DRAXUL_REPO_ROOT) / "assets" / "satview" / relative_path;
    if (std::filesystem::exists(repo_path))
        return repo_path;
#endif

    return bundled;
}

std::optional<std::string> read_text_file(const std::filesystem::path& path, std::string& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        error = "failed to open " + path.string();
        return std::nullopt;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad())
    {
        error = "failed to read " + path.string();
        return std::nullopt;
    }
    return content;
}

using CsvRow = std::vector<std::string>;

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

    for (std::size_t i = 0; i < csv.size(); ++i)
    {
        const char c = csv[i];
        if (quoted)
        {
            if (c == '"')
            {
                if (i + 1 < csv.size() && csv[i + 1] == '"')
                {
                    field.push_back('"');
                    ++i;
                }
                else
                {
                    quoted = false;
                    quote_closed = true;
                }
            }
            else
            {
                field.push_back(c);
            }
            continue;
        }

        if (quote_closed && c != ',' && c != '\r' && c != '\n')
        {
            error = "unexpected data after quoted CSV field";
            return false;
        }
        if (c == '"')
        {
            if (!field.empty() || quote_closed)
            {
                error = "unexpected quote in CSV field";
                return false;
            }
            quoted = true;
        }
        else if (c == ',')
        {
            finish_field();
        }
        else if (c == '\n')
        {
            finish_row();
        }
        else if (c == '\r')
        {
            if (i + 1 >= csv.size() || csv[i + 1] != '\n')
                finish_row();
        }
        else
        {
            field.push_back(c);
        }
    }

    if (quoted)
    {
        error = "unterminated quoted CSV field";
        return false;
    }
    if (!field.empty() || !row.empty() || quote_closed)
        finish_row();
    while (!rows.empty() && rows.back().size() == 1 && rows.back().front().empty())
        rows.pop_back();
    return true;
}

std::string trim_ascii(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return std::string(text);
}

std::string uppercase_ascii(std::string_view text)
{
    std::string value = trim_ascii(text);
    for (char& c : value)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return value;
}

std::optional<std::int64_t> parse_csv_int64(std::string_view text)
{
    const std::string value = trim_ascii(text);
    if (value.empty())
        return std::nullopt;
    std::int64_t parsed = 0;
    const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec != std::errc{} || end != value.data() + value.size())
        return std::nullopt;
    return parsed;
}

std::optional<double> parse_csv_optional_double(std::string_view text, bool& invalid)
{
    const std::string value = trim_ascii(text);
    if (value.empty())
        return std::nullopt;
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end != value.c_str() + value.size() || !std::isfinite(parsed))
    {
        invalid = true;
        return std::nullopt;
    }
    return parsed;
}

SatelliteObjectKind satcat_object_kind(std::string_view object_type)
{
    const std::string value = uppercase_ascii(object_type);
    if (value == "PAY")
        return SatelliteObjectKind::Payload;
    if (value == "R/B")
        return SatelliteObjectKind::RocketBody;
    if (value == "DEB")
        return SatelliteObjectKind::Debris;
    return SatelliteObjectKind::Unknown;
}

void derive_satcat_orbit_shape(SatelliteRecord& record)
{
    if (!record.satcat_inclination_deg.has_value())
        return;
    record.inclination_deg = *record.satcat_inclination_deg;

    if (record.satcat_perigee_km.has_value() && record.satcat_apogee_km.has_value())
    {
        record.perigee_km = *record.satcat_perigee_km;
        record.apogee_km = *record.satcat_apogee_km;
        const double body_radius_km = record.central_body == CentralBody::Moon
            ? 1737.4
            : kEarthEquatorialRadiusKm;
        const double body_mu_km3_per_s2 = record.central_body == CentralBody::Moon
            ? 4902.800118
            : kEarthMuKm3PerS2;
        const double rp = body_radius_km + record.perigee_km;
        const double ra = body_radius_km + record.apogee_km;
        const double semi_major_axis = 0.5 * (rp + ra);
        record.eccentricity = (ra - rp) / (ra + rp);
        if (record.satcat_period_minutes.has_value())
            record.period_minutes = *record.satcat_period_minutes;
        else if (semi_major_axis > 0.0)
            record.period_minutes = kTwoPi * std::sqrt(
                semi_major_axis * semi_major_axis * semi_major_axis / body_mu_km3_per_s2) / 60.0;
    }
    else if (record.satcat_period_minutes.has_value())
    {
        record.period_minutes = *record.satcat_period_minutes;
        const double period_seconds = record.period_minutes * 60.0;
        const double body_mu_km3_per_s2 = record.central_body == CentralBody::Moon
            ? 4902.800118
            : kEarthMuKm3PerS2;
        const double body_radius_km = record.central_body == CentralBody::Moon
            ? 1737.4
            : kEarthEquatorialRadiusKm;
        const double semi_major_axis = std::cbrt(
            body_mu_km3_per_s2 * period_seconds * period_seconds / (kTwoPi * kTwoPi));
        record.perigee_km = semi_major_axis - body_radius_km;
        record.apogee_km = record.perigee_km;
        record.eccentricity = 0.0;
    }

    if (record.period_minutes > 0.0)
    {
        record.mean_motion_rev_per_day = kMinutesPerDay / record.period_minutes;
        if (record.central_body == CentralBody::Earth)
            derive_orbit_shape(record);
        else
        {
            record.orbit_class = OrbitClass::Other;
            record.sun_synchronous_candidate = false;
        }
    }
}

void overlay_satcat_metadata(SatelliteRecord& target, const SatelliteRecord& satcat)
{
    if (!satcat.object_name.empty())
        target.object_name = satcat.object_name;
    if (!satcat.object_id.empty())
        target.object_id = satcat.object_id;
    target.object_type = satcat.object_type;
    target.object_kind = satcat.object_kind;
    target.owner = satcat.owner;
    target.operational_status_code = satcat.operational_status_code;
    target.data_status_code = satcat.data_status_code;
    target.orbit_center = satcat.orbit_center;
    target.orbit_type = satcat.orbit_type;
    target.central_body = satcat.central_body;
    target.radar_cross_section_m2 = satcat.radar_cross_section_m2;
    target.satcat_period_minutes = satcat.satcat_period_minutes;
    target.satcat_inclination_deg = satcat.satcat_inclination_deg;
    target.satcat_perigee_km = satcat.satcat_perigee_km;
    target.satcat_apogee_km = satcat.satcat_apogee_km;
}

} // namespace

std::string_view orbit_class_name(OrbitClass orbit_class)
{
    switch (orbit_class)
    {
    case OrbitClass::LowEarth:
        return "LEO";
    case OrbitClass::MediumEarth:
        return "MEO";
    case OrbitClass::Geosynchronous:
        return "GEO";
    case OrbitClass::HighlyElliptical:
        return "HEO";
    case OrbitClass::Other:
        return "Other";
    }
    return "Other";
}

std::string_view satellite_object_kind_name(SatelliteObjectKind kind)
{
    switch (kind)
    {
    case SatelliteObjectKind::Payload:
        return "Payload";
    case SatelliteObjectKind::RocketBody:
        return "Rocket Body";
    case SatelliteObjectKind::Debris:
        return "Debris";
    case SatelliteObjectKind::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

std::string_view satellite_population_name(SatellitePopulation population)
{
    switch (population)
    {
    case SatellitePopulation::ActivePayload:
        return "Active Payloads";
    case SatellitePopulation::InactivePayload:
        return "Inactive Payloads";
    case SatellitePopulation::RocketBody:
        return "Rocket Bodies";
    case SatellitePopulation::Debris:
        return "Debris";
    case SatellitePopulation::Unknown:
        return "Unknown";
    }
    return "Unknown";
}

std::string_view central_body_name(CentralBody central_body)
{
    switch (central_body)
    {
    case CentralBody::Earth:
        return "Earth";
    case CentralBody::Moon:
        return "Moon";
    case CentralBody::Other:
        return "Other";
    }
    return "Other";
}

std::string_view orbit_solution_kind_name(OrbitSolutionKind kind)
{
    switch (kind)
    {
    case OrbitSolutionKind::GeneralPerturbations:
        return "GP / SGP4";
    case OrbitSolutionKind::SatcatSummaryEstimate:
        return "SATCAT summary estimate";
    case OrbitSolutionKind::CatalogOnly:
        return "Catalog only";
    case OrbitSolutionKind::SampledEphemeris:
        return "Sampled ephemeris";
    case OrbitSolutionKind::Sample:
        return "Bundled sample";
    }
    return "Unknown";
}

std::string_view orbit_solution_source_name(OrbitSolutionKind kind)
{
    switch (kind)
    {
    case OrbitSolutionKind::GeneralPerturbations:
        return "CelesTrak active GP";
    case OrbitSolutionKind::SatcatSummaryEstimate:
        return "CelesTrak SATCAT";
    case OrbitSolutionKind::CatalogOnly:
        return "CelesTrak SATCAT";
    case OrbitSolutionKind::SampledEphemeris:
        return "Horizons / SPICE ephemeris";
    case OrbitSolutionKind::Sample:
        return "Bundled sample";
    }
    return "Unknown";
}

bool satcat_status_is_active(std::string_view status_code)
{
    const std::string status = uppercase_ascii(status_code);
    return status == "+" || status == "P" || status == "B" || status == "S" || status == "X";
}

bool satellite_record_has_summary_orbit(const SatelliteRecord& record)
{
    if (!record.satcat_inclination_deg.has_value()
        || !std::isfinite(*record.satcat_inclination_deg)
        || *record.satcat_inclination_deg < 0.0
        || *record.satcat_inclination_deg > 180.0)
    {
        return false;
    }

    const bool have_perigee = record.satcat_perigee_km.has_value();
    const bool have_apogee = record.satcat_apogee_km.has_value();
    if (have_perigee != have_apogee)
        return false;

    if (have_perigee)
    {
        const double perigee = *record.satcat_perigee_km;
        const double apogee = *record.satcat_apogee_km;
        const double body_radius_km = record.central_body == CentralBody::Moon
            ? 1737.4
            : kEarthEquatorialRadiusKm;
        if (!std::isfinite(perigee) || !std::isfinite(apogee)
            || perigee <= -body_radius_km || apogee < perigee)
        {
            return false;
        }
    }
    else if (!record.satcat_period_minutes.has_value())
    {
        return false;
    }

    if (record.satcat_period_minutes.has_value()
        && (!std::isfinite(*record.satcat_period_minutes) || *record.satcat_period_minutes <= 0.0))
    {
        return false;
    }
    if (record.central_body == CentralBody::Moon
        && (!record.satcat_period_minutes.has_value() || !have_perigee || !have_apogee))
    {
        return false;
    }
    return true;
}

SatellitePopulationCounts satellite_population_counts(const SatelliteCatalog& catalog)
{
    SatellitePopulationCounts counts;
    for (const SatelliteRecord& record : catalog.objects)
    {
        switch (record.population)
        {
        case SatellitePopulation::ActivePayload:
            ++counts.active_payloads;
            break;
        case SatellitePopulation::InactivePayload:
            ++counts.inactive_payloads;
            break;
        case SatellitePopulation::RocketBody:
            ++counts.rocket_bodies;
            break;
        case SatellitePopulation::Debris:
            ++counts.debris;
            break;
        case SatellitePopulation::Unknown:
            ++counts.unknown;
            break;
        }
    }
    return counts;
}

std::size_t renderable_satellite_count(const SatelliteCatalog& catalog)
{
    return static_cast<std::size_t>(std::ranges::count_if(
        catalog.objects,
        [](const SatelliteRecord& record) { return record.renderable; }));
}

CatalogParseResult parse_celestrak_gp_json(
    std::string_view json,
    std::string_view source_label,
    std::string_view source_url)
{
    PERF_MEASURE();
    CatalogParseResult result;
    result.catalog.source_label = std::string(source_label);
    result.catalog.source_url = std::string(source_url);

    std::vector<JsonObject> objects;
    JsonReader reader(json);
    if (!reader.parse_object_array(objects, result.error))
        return result;

    result.catalog.objects.reserve(objects.size());
    for (const JsonObject& object : objects)
    {
        if (auto record = make_record(object))
        {
            result.catalog.objects.push_back(std::move(*record));
        }
        else
        {
            ++result.catalog.skipped_records;
            ++result.catalog.malformed_records;
        }
    }

    if (result.catalog.objects.empty() && !objects.empty())
        result.error = "no valid GP records found";
    return result;
}

CatalogParseResult parse_celestrak_satcat_csv(
    std::string_view csv,
    std::string_view source_label,
    std::string_view source_url)
{
    PERF_MEASURE();
    CatalogParseResult result;
    result.catalog.source_label = std::string(source_label);
    result.catalog.source_url = std::string(source_url);

    std::vector<CsvRow> rows;
    if (!parse_csv_rows(csv, rows, result.error))
        return result;
    if (rows.empty())
    {
        result.error = "empty SATCAT CSV";
        return result;
    }

    if (!rows.front().empty() && rows.front().front().starts_with("\xEF\xBB\xBF"))
        rows.front().front().erase(0, 3);

    std::unordered_map<std::string, std::size_t> columns;
    for (std::size_t i = 0; i < rows.front().size(); ++i)
        columns[uppercase_ascii(rows.front()[i])] = i;

    constexpr std::array<std::string_view, 6> required = {
        "OBJECT_NAME", "NORAD_CAT_ID", "OBJECT_TYPE", "DECAY_DATE", "ORBIT_CENTER", "ORBIT_TYPE"
    };
    for (std::string_view name : required)
    {
        if (!columns.contains(std::string(name)))
        {
            result.error = "SATCAT CSV missing required column " + std::string(name);
            return result;
        }
    }

    const auto field = [&](const CsvRow& row, std::string_view name) -> std::string_view {
        const auto it = columns.find(std::string(name));
        if (it == columns.end() || it->second >= row.size())
            return {};
        return row[it->second];
    };

    result.catalog.objects.reserve(rows.size() - 1);
    std::unordered_set<std::int64_t> seen_ids;
    for (std::size_t row_index = 1; row_index < rows.size(); ++row_index)
    {
        const CsvRow& row = rows[row_index];
        const auto catalog_id = parse_csv_int64(field(row, "NORAD_CAT_ID"));
        if (!catalog_id.has_value() || *catalog_id <= 0 || !seen_ids.insert(*catalog_id).second)
        {
            ++result.catalog.skipped_records;
            ++result.catalog.malformed_records;
            continue;
        }

        const std::string decay_date = trim_ascii(field(row, "DECAY_DATE"));
        const std::string orbit_center = uppercase_ascii(field(row, "ORBIT_CENTER"));
        const std::string orbit_type = uppercase_ascii(field(row, "ORBIT_TYPE"));
        if (!decay_date.empty()
            || (!orbit_center.empty() && orbit_center != "EA" && orbit_center != "MO")
            || orbit_type != "ORB")
        {
            ++result.catalog.excluded_records;
            continue;
        }

        SatelliteRecord record;
        record.norad_catalog_id = *catalog_id;
        record.object_name = trim_ascii(field(row, "OBJECT_NAME"));
        record.object_id = trim_ascii(field(row, "OBJECT_ID"));
        record.object_type = uppercase_ascii(field(row, "OBJECT_TYPE"));
        record.object_kind = satcat_object_kind(record.object_type);
        record.operational_status_code = uppercase_ascii(field(row, "OPS_STATUS_CODE"));
        record.population = population_for_kind(
            record.object_kind,
            satcat_status_is_active(record.operational_status_code));
        record.solution_kind = OrbitSolutionKind::SatcatSummaryEstimate;
        record.owner = trim_ascii(field(row, "OWNER"));
        record.data_status_code = uppercase_ascii(field(row, "DATA_STATUS_CODE"));
        record.orbit_center = orbit_center.empty() ? "EA" : orbit_center;
        record.orbit_type = orbit_type;
        record.central_body = record.orbit_center == "MO"
            ? CentralBody::Moon
            : CentralBody::Earth;

        bool invalid_orbit_numeric = false;
        record.satcat_period_minutes = parse_csv_optional_double(field(row, "PERIOD"), invalid_orbit_numeric);
        record.satcat_inclination_deg = parse_csv_optional_double(field(row, "INCLINATION"), invalid_orbit_numeric);
        record.satcat_apogee_km = parse_csv_optional_double(field(row, "APOGEE"), invalid_orbit_numeric);
        record.satcat_perigee_km = parse_csv_optional_double(field(row, "PERIGEE"), invalid_orbit_numeric);
        bool invalid_rcs = false;
        record.radar_cross_section_m2 = parse_csv_optional_double(field(row, "RCS"), invalid_rcs);
        if (invalid_rcs)
            ++result.catalog.malformed_records;
        // SATCAT's summary altitudes are Earth-oriented and are not a
        // Moon-relative state even when ORBIT_CENTER is MO. Lunar rows need
        // a sourced ephemeris before they can be rendered truthfully.
        record.renderable = record.central_body == CentralBody::Earth
            && !invalid_orbit_numeric
            && satellite_record_has_summary_orbit(record);
        record.solution_kind = record.renderable
            ? OrbitSolutionKind::SatcatSummaryEstimate
            : OrbitSolutionKind::CatalogOnly;
        if (!record.renderable)
            ++result.catalog.non_renderable_records;
        else
            derive_satcat_orbit_shape(record);

        result.catalog.objects.push_back(std::move(record));
    }

    return result;
}

std::size_t apply_sampled_ephemeris_csv(
    SatelliteCatalog& catalog,
    std::string_view csv,
    std::string* error)
{
    if (error)
        error->clear();
    std::vector<CsvRow> rows;
    std::string parse_error;
    if (!parse_csv_rows(csv, rows, parse_error))
    {
        if (error)
            *error = std::move(parse_error);
        return 0;
    }
    if (rows.empty())
        return 0;

    std::unordered_map<std::string, std::size_t> columns;
    for (std::size_t i = 0; i < rows.front().size(); ++i)
        columns[uppercase_ascii(rows.front()[i])] = i;
    constexpr std::array<std::string_view, 10> required = {
        "NORAD_CAT_ID", "SOURCE", "FRAME", "UNIX_SECONDS", "X_KM", "Y_KM", "Z_KM",
        "VX_KM_PER_S", "VY_KM_PER_S", "VZ_KM_PER_S"
    };
    for (const std::string_view name : required)
    {
        if (!columns.contains(std::string(name)))
        {
            if (error)
                *error = "ephemeris CSV missing required column " + std::string(name);
            return 0;
        }
    }
    const auto field = [&](const CsvRow& row, std::string_view name) -> std::string_view {
        const auto found = columns.find(std::string(name));
        return found == columns.end() || found->second >= row.size()
            ? std::string_view{}
            : std::string_view(row[found->second]);
    };

    std::unordered_map<std::int64_t, SatelliteRecord*> records;
    for (SatelliteRecord& record : catalog.objects)
    {
        if (record.central_body == CentralBody::Moon)
            records.emplace(record.norad_catalog_id, &record);
    }
    for (std::size_t row_index = 1; row_index < rows.size(); ++row_index)
    {
        const CsvRow& row = rows[row_index];
        const auto id = parse_csv_int64(field(row, "NORAD_CAT_ID"));
        if (!id.has_value())
            continue;
        const auto found = records.find(*id);
        if (found == records.end())
            continue;

        bool invalid = false;
        const auto time = parse_csv_optional_double(field(row, "UNIX_SECONDS"), invalid);
        const auto x = parse_csv_optional_double(field(row, "X_KM"), invalid);
        const auto y = parse_csv_optional_double(field(row, "Y_KM"), invalid);
        const auto z = parse_csv_optional_double(field(row, "Z_KM"), invalid);
        const auto vx = parse_csv_optional_double(field(row, "VX_KM_PER_S"), invalid);
        const auto vy = parse_csv_optional_double(field(row, "VY_KM_PER_S"), invalid);
        const auto vz = parse_csv_optional_double(field(row, "VZ_KM_PER_S"), invalid);
        if (invalid || !time || !x || !y || !z || !vx || !vy || !vz)
            continue;

        SatelliteRecord& record = *found->second;
        record.ephemeris_source = trim_ascii(field(row, "SOURCE"));
        record.ephemeris_frame = uppercase_ascii(field(row, "FRAME"));
        if (record.ephemeris_frame != "MOON_ICRF"
            && record.ephemeris_frame != "MOON_EQUATORIAL_J2000")
            continue;
        bool invalid_track_horizon = false;
        const auto track_horizon = parse_csv_optional_double(
            field(row, "TRACK_HORIZON_MINUTES"), invalid_track_horizon);
        if (invalid_track_horizon || (track_horizon.has_value() && *track_horizon <= 0.0))
            continue;
        if (track_horizon.has_value())
            record.ephemeris_track_horizon_minutes = *track_horizon;
        record.ephemeris_samples.push_back({ *time, *x, *y, *z, *vx, *vy, *vz });
    }

    std::size_t applied = 0;
    for (SatelliteRecord& record : catalog.objects)
    {
        auto& samples = record.ephemeris_samples;
        std::ranges::sort(samples, {}, &SampledEphemerisPoint::unix_seconds);
        const auto duplicate = std::adjacent_find(samples.begin(), samples.end(),
            [](const SampledEphemerisPoint& a, const SampledEphemerisPoint& b) {
                return a.unix_seconds == b.unix_seconds;
            });
        if (samples.size() < 2 || duplicate != samples.end())
        {
            samples.clear();
            continue;
        }
        record.solution_kind = OrbitSolutionKind::SampledEphemeris;
        record.renderable = true;
        ++applied;
    }
    catalog.non_renderable_records = static_cast<std::size_t>(std::ranges::count_if(
        catalog.objects,
        [](const SatelliteRecord& record) { return !record.renderable; }));
    return applied;
}

std::size_t load_bundled_sampled_ephemeris(SatelliteCatalog& catalog, std::string* error)
{
    std::string read_error;
    const auto path = resolve_satview_catalog_path("catalog/lunar_ephemeris.csv");
    const auto content = read_text_file(path, read_error);
    if (!content.has_value())
    {
        if (error)
            *error = std::move(read_error);
        return 0;
    }
    return apply_sampled_ephemeris_csv(catalog, *content, error);
}

std::size_t apply_lunar_disposition_csv(
    SatelliteCatalog& catalog,
    std::string_view csv,
    std::string* error)
{
    if (error)
        error->clear();
    std::vector<CsvRow> rows;
    std::string parse_error;
    if (!parse_csv_rows(csv, rows, parse_error))
    {
        if (error)
            *error = std::move(parse_error);
        return 0;
    }
    if (rows.empty())
        return 0;

    std::unordered_map<std::string, std::size_t> columns;
    for (std::size_t i = 0; i < rows.front().size(); ++i)
        columns[uppercase_ascii(rows.front()[i])] = i;
    if (!columns.contains("NORAD_CAT_ID") || !columns.contains("DISPOSITION"))
    {
        if (error)
            *error = "lunar disposition CSV missing NORAD_CAT_ID or DISPOSITION";
        return 0;
    }
    const auto field = [&](const CsvRow& row, std::string_view name) -> std::string_view {
        const auto found = columns.find(std::string(name));
        return found == columns.end() || found->second >= row.size()
            ? std::string_view{}
            : std::string_view(row[found->second]);
    };

    std::unordered_set<std::int64_t> non_orbiting_ids;
    for (std::size_t row_index = 1; row_index < rows.size(); ++row_index)
    {
        const auto id = parse_csv_int64(field(rows[row_index], "NORAD_CAT_ID"));
        const std::string disposition = uppercase_ascii(field(rows[row_index], "DISPOSITION"));
        if (id.has_value() && (disposition == "IMPACTED" || disposition == "SURFACE"))
            non_orbiting_ids.insert(*id);
    }

    const std::size_t before = catalog.objects.size();
    std::erase_if(catalog.objects, [&](const SatelliteRecord& record) {
        return record.central_body == CentralBody::Moon
            && non_orbiting_ids.contains(record.norad_catalog_id);
    });
    const std::size_t removed = before - catalog.objects.size();
    catalog.excluded_records += removed;
    catalog.non_renderable_records = static_cast<std::size_t>(std::ranges::count_if(
        catalog.objects,
        [](const SatelliteRecord& record) { return !record.renderable; }));
    return removed;
}

std::size_t load_bundled_lunar_dispositions(SatelliteCatalog& catalog, std::string* error)
{
    std::string read_error;
    const auto path = resolve_satview_catalog_path("catalog/lunar_dispositions.csv");
    const auto content = read_text_file(path, read_error);
    if (!content.has_value())
    {
        if (error)
            *error = std::move(read_error);
        return 0;
    }
    return apply_lunar_disposition_csv(catalog, *content, error);
}

SatelliteCatalog merge_satellite_catalogs(
    const SatelliteCatalog& active_gp,
    const SatelliteCatalog& satcat)
{
    PERF_MEASURE();
    SatelliteCatalog merged;
    merged.source_label = "CelesTrak active GP + SATCAT";
    merged.source_url = active_gp.source_url;
    if (!satcat.source_url.empty())
    {
        if (!merged.source_url.empty())
            merged.source_url += " | ";
        merged.source_url += satcat.source_url;
    }
    merged.skipped_records = active_gp.skipped_records + satcat.skipped_records;
    merged.malformed_records = active_gp.malformed_records + satcat.malformed_records;
    merged.excluded_records = active_gp.excluded_records + satcat.excluded_records;
    merged.objects.reserve(active_gp.objects.size() + satcat.objects.size());

    std::unordered_map<std::int64_t, std::size_t> by_catalog_id;
    for (const SatelliteRecord& source : active_gp.objects)
    {
        if (source.norad_catalog_id <= 0 || by_catalog_id.contains(source.norad_catalog_id))
        {
            ++merged.skipped_records;
            ++merged.malformed_records;
            continue;
        }
        SatelliteRecord record = source;
        record.solution_kind = source.solution_kind == OrbitSolutionKind::Sample
            ? OrbitSolutionKind::Sample
            : OrbitSolutionKind::GeneralPerturbations;
        record.renderable = true;
        if (record.object_kind == SatelliteObjectKind::Unknown)
        {
            record.object_kind = SatelliteObjectKind::Payload;
            if (record.object_type.empty())
                record.object_type = "PAY";
        }
        record.population = population_for_kind(record.object_kind, true);
        by_catalog_id.emplace(record.norad_catalog_id, merged.objects.size());
        merged.objects.push_back(std::move(record));
    }

    for (const SatelliteRecord& satcat_record : satcat.objects)
    {
        const auto found = by_catalog_id.find(satcat_record.norad_catalog_id);
        if (found != by_catalog_id.end())
        {
            SatelliteRecord& record = merged.objects[found->second];
            overlay_satcat_metadata(record, satcat_record);
            record.population = population_for_kind(
                record.object_kind,
                record.object_kind == SatelliteObjectKind::Payload);
            continue;
        }

        SatelliteRecord record = satcat_record;
        by_catalog_id.emplace(record.norad_catalog_id, merged.objects.size());
        merged.objects.push_back(std::move(record));
    }

    merged.non_renderable_records = static_cast<std::size_t>(std::ranges::count_if(
        merged.objects,
        [](const SatelliteRecord& record) { return !record.renderable; }));
    return merged;
}

CatalogParseResult load_sample_satellite_catalog()
{
    PERF_MEASURE();
    const auto path = resolve_satview_catalog_path("catalog/sample_gp.json");
    std::string error;
    auto content = read_text_file(path, error);
    if (!content.has_value())
    {
        CatalogParseResult result;
        result.catalog.source_label = "sample";
        result.error = error;
        DRAXUL_LOG_WARN(LogCategory::Renderer, "SatView: %s", error.c_str());
        return result;
    }

    CatalogParseResult result = parse_celestrak_gp_json(*content, "sample", path.string());
    if (result)
    {
        for (SatelliteRecord& record : result.catalog.objects)
            record.solution_kind = OrbitSolutionKind::Sample;
    }
    return result;
}

} // namespace draxul::satview
