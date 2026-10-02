#pragma once

#include <draxul/session_model.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

std::filesystem::path session_state_directory();
std::string session_state_file_name(std::string_view session_id);
std::filesystem::path session_state_path(std::string_view session_id);
bool has_saved_session_state(
    std::string_view session_id, std::string* error = nullptr);
bool validate_session_snapshot(
    const SessionSnapshot& state, std::string* error = nullptr);
std::optional<SessionSnapshot> decode_session_state(
    std::string_view content, std::string* error = nullptr);
std::optional<std::string> encode_session_state(
    const SessionSnapshot& state, std::string* error = nullptr);
bool save_session_state(
    const SessionSnapshot& state, std::string* error = nullptr);
bool save_session_state_to_path(const SessionSnapshot& state,
    const std::filesystem::path& path, std::string* error = nullptr);

// Publish an already durable, same-directory staged checkpoint without deleting
// the previous destination on failure. Callers own publication authorization.
bool publish_staged_session_state(const std::filesystem::path& staged,
    const std::filesystem::path& destination, std::string* error = nullptr);
std::optional<SessionSnapshot> load_session_state_from_path(
    const std::filesystem::path& path, std::string* error = nullptr);
bool delete_session_state(
    std::string_view session_id, std::string* error = nullptr);
std::optional<SessionSnapshot> load_session_state(
    std::string_view session_id, std::string* error = nullptr);
std::optional<SessionSnapshot> load_session_state(
    std::string* error = nullptr);
std::vector<SessionSummary> list_saved_sessions(
    std::string* error = nullptr);

} // namespace draxul
