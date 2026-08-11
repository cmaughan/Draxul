#pragma once

#include <draxul/terminal_snapshot.h>

#include <cstddef>
#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

inline constexpr std::string_view kFakeRemotePaneId = "fake-pane-1";
inline constexpr std::string_view kFakeRemoteTerminalId = "fake-terminal-1";
inline constexpr std::string_view kServerShellPaneId = "server-shell-pane-1";
inline constexpr std::string_view kServerShellTerminalId = "server-shell-terminal-1";
inline constexpr size_t kRemoteTerminalQueueLimit = 32;
inline constexpr size_t kRemoteTerminalSubscriberQueueByteLimit
    = 2 * 1024 * 1024;
inline constexpr size_t kRemoteTerminalMaxEventsPerPoll = 64;
inline constexpr size_t kRemoteTerminalMaxScrollbackPageRows = 200;
// The PTY/ConPTY adapters use the same bounds. Keeping the wire dimensions
// aligned avoids reporting a grid larger than the child process actually owns
// and keeps a complete compact snapshot comfortably inside the IPC frame.
inline constexpr int kRemoteTerminalMaxColumns = 320;
inline constexpr int kRemoteTerminalMaxRows = 200;
inline constexpr size_t kRemoteTerminalMaxCells
    = static_cast<size_t>(kRemoteTerminalMaxColumns)
    * static_cast<size_t>(kRemoteTerminalMaxRows);

struct RemotePaneDescriptor
{
    std::string pane_id;
    std::string terminal_id;
    std::string name;
    std::string execution_domain;
    uint64_t process_id = 0;
    // Defaults live for compatibility with servers predating explicit
    // process-state publication.
    bool process_running = true;
    std::optional<int> exit_code;

    bool operator==(const RemotePaneDescriptor&) const = default;
};

struct RemoteTerminalVersion
{
    std::string server_epoch;
    std::string terminal_id;
    uint64_t generation = 0;
    uint64_t sequence = 0;

    bool operator==(const RemoteTerminalVersion&) const = default;
};

enum class RemoteTerminalEventKind
{
    Snapshot,
    Delta,
    Controller,
    Clipboard,
};

struct RemoteTerminalEvent
{
    RemoteTerminalEventKind kind = RemoteTerminalEventKind::Snapshot;
    RemoteTerminalVersion version;
    uint64_t process_id = 0;
    // Carried on every newly emitted event so a retained final pane can show
    // its exited state without ending the terminal transport subscription.
    bool process_running = true;
    std::optional<int> exit_code;
    std::string controller_client_id;
    std::optional<TerminalSemanticSnapshot> snapshot;
    std::optional<TerminalDirtySnapshot> delta;
    std::optional<std::string> clipboard;

    bool operator==(const RemoteTerminalEvent&) const = default;
};

struct RemoteTerminalAttach
{
    RemotePaneDescriptor pane;
    RemoteTerminalEvent state;

    bool operator==(const RemoteTerminalAttach&) const = default;
};

struct RemoteTerminalScrollbackPage
{
    RemoteTerminalVersion version;
    uint64_t total_rows = 0;
    uint64_t offset_from_live = 0;
    int cols = 0;
    std::optional<TerminalSemanticSnapshot> snapshot;

    bool operator==(const RemoteTerminalScrollbackPage&) const = default;
};

std::string_view to_string(RemoteTerminalEventKind kind);
std::optional<RemoteTerminalEventKind> parse_remote_terminal_event_kind(
    std::string_view value);

nlohmann::json terminal_semantic_snapshot_to_json(
    const TerminalSemanticSnapshot& snapshot);
std::optional<TerminalSemanticSnapshot>
terminal_semantic_snapshot_from_json(
    const nlohmann::json& value, std::string& error);
nlohmann::json terminal_dirty_snapshot_to_json(
    const TerminalDirtySnapshot& snapshot);
std::optional<TerminalDirtySnapshot> terminal_dirty_snapshot_from_json(
    const nlohmann::json& value, std::string& error);

nlohmann::json remote_terminal_event_to_json(
    const RemoteTerminalEvent& event);
std::optional<RemoteTerminalEvent> remote_terminal_event_from_json(
    const nlohmann::json& value, std::string& error);
nlohmann::json remote_terminal_attach_to_json(
    const RemoteTerminalAttach& attach);
std::optional<RemoteTerminalAttach> remote_terminal_attach_from_json(
    const nlohmann::json& value, std::string& error);
nlohmann::json remote_terminal_scrollback_page_to_json(
    const RemoteTerminalScrollbackPage& page);
std::optional<RemoteTerminalScrollbackPage>
remote_terminal_scrollback_page_from_json(
    const nlohmann::json& value, std::string& error);

} // namespace draxul
