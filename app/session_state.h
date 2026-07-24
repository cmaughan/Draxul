#pragma once

#include "pane_manager.h"
#include "space_id.h"

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
    PaneManager::PaneLayoutSnapshot pane_layout;
};

struct SpaceSnapshot
{
    SpaceId id = kInvalidSpaceId;
    std::string name;
    std::filesystem::path root_directory;
    int active_tab_id = -1;
    int next_tab_id = 0;
    std::vector<TabSnapshot> tabs;
};

struct SessionSnapshot
{
    int version = 2;
    std::string session_id = "default";
    std::string session_name = "default";
    SpaceId active_space_id = kInvalidSpaceId;
    SpaceId next_space_id = kDefaultSpaceId;
    std::vector<SpaceSnapshot> spaces;
};

struct SessionSummary
{
    std::string session_id;
    std::string session_name;
    int space_count = 0;
    int tab_count = 0;
    int pane_count = 0;
    bool has_saved_state = false;
};

std::filesystem::path session_state_directory();
std::filesystem::path session_state_path(std::string_view session_id);
bool has_saved_session_state(std::string_view session_id, std::string* error = nullptr);
bool validate_session_snapshot(const SessionSnapshot& state, std::string* error = nullptr);
std::optional<SessionSnapshot> decode_session_state(
    std::string_view content, std::string* error = nullptr);
std::optional<std::string> encode_session_state(
    const SessionSnapshot& state, std::string* error = nullptr);
bool save_session_state(const SessionSnapshot& state, std::string* error = nullptr);
bool delete_session_state(std::string_view session_id, std::string* error = nullptr);
std::optional<SessionSnapshot> load_session_state(
    std::string_view session_id, std::string* error = nullptr);
std::optional<SessionSnapshot> load_session_state(std::string* error = nullptr);
std::vector<SessionSummary> list_saved_sessions(std::string* error = nullptr);

} // namespace draxul
