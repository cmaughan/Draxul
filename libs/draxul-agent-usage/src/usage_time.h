#pragma once

#include <draxul/agent_usage.h>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string_view>

namespace draxul
{

// Parses ISO-8601 UTC timestamps as written by Codex and Claude, for example
// "2026-08-10T06:18:31.803Z". A numeric "+hh:mm"/"-hh:mm" suffix is applied;
// a missing zone is treated as UTC.
std::optional<UsageTimestamp> parse_usage_timestamp(std::string_view text);

UsageTimestamp file_time_to_usage(std::filesystem::file_time_type time);

} // namespace draxul
