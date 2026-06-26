#include <draxul/satview/satview_catalog.h>

#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/runtime_path.h>

#include <algorithm>
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

namespace draxul::satview
{

namespace
{

constexpr double kEarthMuKm3PerS2 = 398600.4418;
constexpr double kEarthEquatorialRadiusKm = 6378.137;
constexpr double kSecondsPerDay = 86400.0;
constexpr double kMinutesPerDay = 1440.0;
constexpr double kTwoPi = 6.28318530717958647692;

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
        }
    }

    if (result.catalog.objects.empty() && !objects.empty())
        result.error = "no valid GP records found";
    return result;
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

    return parse_celestrak_gp_json(*content, "sample", path.string());
}

} // namespace draxul::satview
