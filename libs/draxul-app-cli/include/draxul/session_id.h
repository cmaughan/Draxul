#pragma once

#include <cstdint>
#include <draxul/result.h>
#include <string>
#include <string_view>

namespace draxul
{

std::string make_session_id_slug(std::string_view text);
std::string format_session_id_timestamp(int64_t unix_seconds);
std::string make_session_id_base(std::string_view display_name, int64_t unix_seconds);
std::string make_session_id_candidate(std::string_view base, int suffix);

Result<bool, Error> session_id_exists(std::string_view session_id);
Result<std::string, Error> make_unique_session_id(
    std::string_view display_name,
    int64_t unix_seconds);

} // namespace draxul
