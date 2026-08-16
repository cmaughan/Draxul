#pragma once

#include <draxul/server_control_channel.h>

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace draxul
{

// Shared refresh/poll revision policy for the server's revisioned snapshot
// endpoints ("<prefix>.snapshot" / "<prefix>.poll"), collapsing the
// previously duplicated AgentClient/TopologyClient bodies. A Derived client
// provides its variation points as:
//
//   static constexpr std::string_view kMethodPrefix;          // "agent"
//   static constexpr std::string_view kStaleRevisionErrorCode;
//   static std::optional<Snapshot> parse_snapshot(
//       const nlohmann::json& value, std::string& error);
//
// and Snapshot exposes an unsigned `revision` member.
template <typename Derived, typename Snapshot>
class RevisionPolledClient
{
public:
    bool refresh(std::string& error)
    {
        nlohmann::json result;
        if (!request(method("snapshot"), nlohmann::json::object(),
                result, error))
        {
            return false;
        }
        auto parsed = Derived::parse_snapshot(result, error);
        if (!parsed)
        {
            last_error_code_ = "invalid_response";
            return false;
        }
        snapshot_ = std::move(*parsed);
        return true;
    }

    bool poll(bool& changed, std::string& error)
    {
        changed = false;
        nlohmann::json result;
        if (!request(method("poll"),
                { { "after_revision", snapshot_.revision } },
                result, error))
        {
            if (last_error_code_ == Derived::kStaleRevisionErrorCode)
            {
                if (!refresh(error))
                    return false;
                changed = true;
                return true;
            }
            return false;
        }
        if (!result.is_object()
            || !result.contains("changed")
            || !result["changed"].is_boolean()
            || !result.contains("revision")
            || !result["revision"].is_number_unsigned())
        {
            last_error_code_ = "invalid_response";
            error = "Invalid " + std::string(Derived::kMethodPrefix)
                + " poll result.";
            return false;
        }
        changed = result["changed"].get<bool>();
        if (!changed)
            return true;
        if (!result.contains("snapshot"))
        {
            last_error_code_ = "invalid_response";
            error = "Changed " + std::string(Derived::kMethodPrefix)
                + " poll has no snapshot.";
            return false;
        }
        auto parsed
            = Derived::parse_snapshot(result["snapshot"], error);
        if (!parsed)
        {
            last_error_code_ = "invalid_response";
            return false;
        }
        snapshot_ = std::move(*parsed);
        return true;
    }

    const Snapshot& snapshot() const noexcept
    {
        return snapshot_;
    }

    const std::string& last_error_code() const noexcept
    {
        return last_error_code_;
    }

protected:
    explicit RevisionPolledClient(ServerControlChannelOptions options)
        : channel_(std::move(options))
    {
    }

    static std::string method(std::string_view operation)
    {
        return std::string(Derived::kMethodPrefix) + "."
            + std::string(operation);
    }

    bool request(std::string_view method_name, nlohmann::json params,
        nlohmann::json& result, std::string& error)
    {
        auto response
            = channel_.request(method_name, std::move(params));
        if (!response.ok)
        {
            last_error_code_ = std::move(response.error_code);
            error = std::move(response.error_message);
            return false;
        }
        last_error_code_.clear();
        result = std::move(response.result);
        return true;
    }

    ServerControlChannel channel_;
    Snapshot snapshot_;
    std::string last_error_code_;
};

} // namespace draxul
