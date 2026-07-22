#pragma once

#include "pane_manager.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

struct TabSnapshot
{
    int id = -1;
    std::string name;
    bool name_user_set = false;
    PaneManager::PaneLayoutSnapshot pane_manager;
};

struct SessionSnapshot
{
    int version = 1;
    std::string session_id = "default";
    std::string session_name = "default";
    int active_tab_id = -1;
    int next_tab_id = 0;
    std::vector<TabSnapshot> tabs;
};

struct SessionSummary
{
    std::string session_id;
    std::string session_name;
    int tab_count = 0;
    int pane_count = 0;
    bool has_saved_state = false;
};

std::filesystem::path session_state_directory();
std::filesystem::path session_state_path(std::string_view session_id);
bool has_saved_session_state(std::string_view session_id, std::string* error = nullptr);
bool save_session_state(const SessionSnapshot& state, std::string* error = nullptr);
bool delete_session_state(std::string_view session_id, std::string* error = nullptr);
std::optional<SessionSnapshot> load_session_state(
    std::string_view session_id, std::string* error = nullptr);
std::optional<SessionSnapshot> load_session_state(std::string* error = nullptr);
std::vector<SessionSummary> list_saved_sessions(std::string* error = nullptr);

} // namespace draxul
