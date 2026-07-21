#pragma once

#include "host_manager.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

struct WorkspaceSessionState
{
    int id = -1;
    std::string name;
    bool name_user_set = false;
    HostManager::SessionState host_manager;
};

struct AppSessionState
{
    int version = 1;
    std::string session_id = "default";
    std::string session_name = "default";
    int active_workspace_id = -1;
    int next_workspace_id = 0;
    std::vector<WorkspaceSessionState> workspaces;
};

struct SessionSummary
{
    std::string session_id;
    std::string session_name;
    int workspace_count = 0;
    int pane_count = 0;
    bool has_saved_state = false;
};

std::filesystem::path session_state_directory();
std::filesystem::path session_state_path(std::string_view session_id);
bool has_saved_session_state(std::string_view session_id, std::string* error = nullptr);
bool save_session_state(const AppSessionState& state, std::string* error = nullptr);
bool delete_session_state(std::string_view session_id, std::string* error = nullptr);
std::optional<AppSessionState> load_session_state(
    std::string_view session_id, std::string* error = nullptr);
std::optional<AppSessionState> load_session_state(std::string* error = nullptr);
std::vector<SessionSummary> list_saved_sessions(std::string* error = nullptr);

} // namespace draxul
