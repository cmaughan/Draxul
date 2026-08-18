#include "control_codec.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>

namespace draxul::control_detail
{

uint64_t fnv1a(std::string_view text)
{
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : text)
    {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string endpoint_key(std::string_view session_id)
{
    std::ostringstream out;
    out << std::hex << fnv1a(session_id);
    return out.str();
}

std::string session_key(std::string_view session_id)
{
    std::string slug;
    for (unsigned char ch : session_id)
    {
        if (std::isalnum(ch) || ch == '-' || ch == '_')
            slug.push_back(static_cast<char>(ch));
        else
            slug.push_back('_');
        if (slug.size() == 32)
            break;
    }
    if (slug.empty())
        slug = "default";

    std::ostringstream out;
    out << std::hex << fnv1a(session_id) << "-" << slug;
    return out.str();
}

std::string normalized_runtime_key(
    const std::filesystem::path& runtime_directory)
{
    std::error_code path_error;
    auto normalized = std::filesystem::weakly_canonical(
        runtime_directory, path_error);
    if (path_error)
    {
        path_error.clear();
        normalized = std::filesystem::absolute(
            runtime_directory, path_error);
        if (path_error)
            normalized = runtime_directory;
    }
    std::string value = normalized.lexically_normal().generic_string();
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
#endif
    return value;
}

std::string random_token()
{
    std::random_device random;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 32; ++i)
        out << std::setw(2) << (random() & 0xff);
    return out.str();
}

bool depth_within_limit(std::string_view text)
{
    size_t depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (char ch : text)
    {
        if (in_string)
        {
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                in_string = false;
            continue;
        }
        if (ch == '"')
        {
            in_string = true;
            continue;
        }
        if (ch == '{' || ch == '[')
        {
            if (++depth > kControlMaxJsonDepth)
                return false;
        }
        else if (ch == '}' || ch == ']')
        {
            if (depth == 0)
                return false;
            --depth;
        }
    }
    return !in_string && depth == 0;
}

std::array<uint8_t, 4> frame_prefix(size_t size)
{
    const uint32_t value = static_cast<uint32_t>(size);
    return {
        static_cast<uint8_t>(value & 0xff),
        static_cast<uint8_t>((value >> 8) & 0xff),
        static_cast<uint8_t>((value >> 16) & 0xff),
        static_cast<uint8_t>((value >> 24) & 0xff),
    };
}

size_t frame_size(const std::array<uint8_t, 4>& prefix)
{
    return static_cast<size_t>(prefix[0])
        | (static_cast<size_t>(prefix[1]) << 8)
        | (static_cast<size_t>(prefix[2]) << 16)
        | (static_cast<size_t>(prefix[3]) << 24);
}

nlohmann::json response_json(
    std::string_view id, const ControlMethodResult& result)
{
    nlohmann::json response = {
        { "version", kControlProtocolVersion },
        { "id", id },
        { "ok", result.ok },
    };
    if (result.ok)
        response["result"] = result.value;
    else
    {
        response["error"] = {
            { "code", result.error_code },
            { "message", result.error_message },
        };
    }
    return response;
}

std::string dump_wire_json(const nlohmann::json& value)
{
    return value.dump(-1, ' ', false,
        nlohmann::detail::error_handler_t::replace);
}

ControlMethodResult parse_request(std::string_view bytes,
    std::string_view expected_token, ControlRequest& request)
{
    if (bytes.empty() || bytes.size() > kControlMaxMessageBytes
        || !depth_within_limit(bytes))
    {
        return ControlMethodResult::error(
            "invalid_message", "Control message is empty or exceeds structural limits.");
    }

    const nlohmann::json envelope
        = nlohmann::json::parse(bytes, nullptr, false, true);
    if (envelope.is_discarded() || !envelope.is_object())
        return ControlMethodResult::error("invalid_json", "Control message is not valid JSON.");
    if (envelope.value("version", 0) != kControlProtocolVersion)
    {
        return ControlMethodResult::error(
            "unsupported_version", "Unsupported control protocol version.");
    }
    if (!envelope.contains("id") || !envelope["id"].is_string()
        || envelope["id"].get_ref<const std::string&>().empty()
        || envelope["id"].get_ref<const std::string&>().size() > 64)
    {
        return ControlMethodResult::error("invalid_request", "Request id is invalid.");
    }
    request.id = envelope["id"].get<std::string>();
    if (!envelope.contains("token") || !envelope["token"].is_string()
        || envelope["token"].get_ref<const std::string&>() != expected_token)
    {
        return ControlMethodResult::error("authentication_failed", "Authentication failed.");
    }
    if (!envelope.contains("method") || !envelope["method"].is_string()
        || envelope["method"].get_ref<const std::string&>().empty()
        || envelope["method"].get_ref<const std::string&>().size() > 64)
    {
        return ControlMethodResult::error("invalid_request", "Method name is invalid.");
    }
    request.method = envelope["method"].get<std::string>();
    if (envelope.contains("params") && !envelope["params"].is_object())
        return ControlMethodResult::error("invalid_request", "Request params must be an object.");

    request.params = envelope.value("params", nlohmann::json::object());
    if (envelope.contains("timeout_ms"))
    {
        if (!envelope["timeout_ms"].is_number_integer())
        {
            return ControlMethodResult::error(
                "invalid_request", "Request timeout is invalid.");
        }
        const int64_t timeout_ms = envelope["timeout_ms"].get<int64_t>();
        constexpr int64_t maximum_timeout_ms
            = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::hours(1))
                  .count();
        if (timeout_ms <= 0 || timeout_ms > maximum_timeout_ms)
        {
            return ControlMethodResult::error(
                "invalid_request", "Request timeout is out of range.");
        }
        request.expires_at = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeout_ms);
    }
    return ControlMethodResult::success(nullptr);
}

std::string encode_request(std::string_view id, std::string_view token,
    std::string_view method, const nlohmann::json& params,
    std::chrono::milliseconds timeout)
{
    return nlohmann::json{
        { "version", kControlProtocolVersion },
        { "token", token },
        { "id", id },
        { "method", method },
        { "params", params },
        { "timeout_ms", timeout.count() },
    }
        .dump();
}

ControlClientResult parse_response(
    std::string_view bytes, std::string_view expected_id)
{
    if (!depth_within_limit(bytes))
    {
        return { false, nullptr, "invalid_response",
            "Control response exceeds the JSON nesting limit." };
    }

    const auto response = nlohmann::json::parse(bytes, nullptr, false, true);
    if (response.is_discarded() || !response.is_object())
    {
        return { false, nullptr, "invalid_response",
            "Control response is not a JSON object." };
    }
    if (response.value("version", 0) != kControlProtocolVersion)
    {
        return { false, nullptr, "invalid_response",
            "Control response has an unsupported version." };
    }
    if (response.value("id", std::string{}) != expected_id)
    {
        return { false, nullptr, "invalid_response",
            "Control response does not match the request id." };
    }
    if (!response.contains("ok") || !response["ok"].is_boolean())
    {
        return { false, nullptr, "invalid_response",
            "Control response has no valid result discriminator." };
    }
    if (response["ok"].get<bool>())
        return { true, response.value("result", nlohmann::json{}), {}, {} };
    if (!response.contains("error") || !response["error"].is_object())
        return { false, nullptr, "invalid_response", "Control response is invalid." };
    return {
        false,
        nullptr,
        response["error"].value("code", "unknown_error"),
        response["error"].value("message", "Control request failed."),
    };
}

namespace
{

TransportStatus invalid_frame(TransportStage stage)
{
    return TransportStatus::failure({
        .stage = stage,
        .classification = FailureClass::IoError,
        .message = "Invalid control frame.",
    });
}

} // namespace

TransportStatus read_control_frame(
    const ExactRead& read_exact, std::string& bytes)
{
    std::array<uint8_t, 4> prefix{};
    auto status = read_exact(
        prefix.data(), prefix.size(), TransportStage::ReadPrefix);
    if (!status.ok)
        return status;
    const size_t size = frame_size(prefix);
    if (size == 0 || size > kControlMaxMessageBytes)
        return invalid_frame(TransportStage::ReadPrefix);
    bytes.resize(size);
    return read_exact(
        bytes.data(), bytes.size(), TransportStage::ReadPayload);
}

TransportStatus write_control_frame(
    const ExactWrite& write_exact, std::string_view bytes)
{
    if (bytes.empty() || bytes.size() > kControlMaxMessageBytes)
        return invalid_frame(TransportStage::WritePrefix);
    const auto prefix = frame_prefix(bytes.size());
    auto status = write_exact(
        prefix.data(), prefix.size(), TransportStage::WritePrefix);
    if (!status.ok)
        return status;
    return write_exact(
        bytes.data(), bytes.size(), TransportStage::WritePayload);
}

} // namespace draxul::control_detail
