#pragma once

#include "session_state.h"

#include <string>
#include <vector>

namespace draxul
{

std::vector<SessionSummary> list_known_sessions(std::string* error = nullptr);
std::string session_entry_name(const SessionSummary& session);
std::string session_entry_hint(const SessionSummary& session);

} // namespace draxul
