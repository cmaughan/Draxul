#include <draxul/remote_terminal_client.h>

#include <draxul/control_plane.h>
#include <draxul/server_protocol.h>

#include <nlohmann/json.hpp>
#include <utility>

namespace draxul
{

bool RemoteTerminalProjection::attach(
    const RemoteTerminalAttach& attach, std::string& error)
{
    if (attach.pane.pane_id.empty()
        || attach.pane.terminal_id.empty()
        || attach.pane.terminal_id != attach.state.version.terminal_id)
    {
        error = "Remote pane identity does not match its terminal state.";
        return false;
    }
    if (attach.state.kind != RemoteTerminalEventKind::Snapshot
        || !attach.state.snapshot)
    {
        error = "Remote terminal attach did not contain a complete snapshot.";
        return false;
    }

    attached_ = false;
    pane_ = attach.pane;
    version_ = {};
    snapshot_ = {};
    controller_client_id_.clear();
    pending_clipboard_write_.reset();
    if (!apply_snapshot(attach.state, false, error))
        return false;
    attached_ = true;
    return true;
}

bool RemoteTerminalProjection::apply(
    const RemoteTerminalEvent& event, std::string& error)
{
    if (!attached_)
    {
        error = "Remote terminal projection is not attached.";
        return false;
    }
    if (event.version.server_epoch != version_.server_epoch)
    {
        error = "Remote terminal event has a stale server epoch.";
        return false;
    }
    if (event.version.terminal_id != version_.terminal_id)
    {
        error = "Remote terminal event has a stale terminal identity.";
        return false;
    }
    if (event.version.generation != version_.generation
        && event.kind != RemoteTerminalEventKind::Snapshot)
    {
        error = "Remote terminal event has a stale generation.";
        return false;
    }

    if (event.kind == RemoteTerminalEventKind::Snapshot)
        return apply_snapshot(event, true, error);
    if (event.version.sequence != version_.sequence + 1)
    {
        error = "Remote terminal event sequence is not contiguous.";
        return false;
    }
    if (event.kind == RemoteTerminalEventKind::Delta)
    {
        if (!event.delta || !apply_delta(*event.delta, error))
            return false;
    }
    else if (event.kind == RemoteTerminalEventKind::Controller)
    {
        if (event.snapshot || event.delta || event.clipboard)
        {
            error = "Controller event contains terminal cell state.";
            return false;
        }
    }
    else if (event.kind == RemoteTerminalEventKind::Clipboard)
    {
        if (!event.clipboard || event.snapshot || event.delta)
        {
            error = "Clipboard event contains invalid terminal state.";
            return false;
        }
        pending_clipboard_write_ = event.clipboard;
    }
    else
    {
        error = "Remote terminal event kind is unsupported.";
        return false;
    }
    version_ = event.version;
    controller_client_id_ = event.controller_client_id;
    return true;
}

bool RemoteTerminalProjection::attached() const
{
    return attached_;
}

const RemotePaneDescriptor& RemoteTerminalProjection::pane() const
{
    return pane_;
}

const RemoteTerminalVersion& RemoteTerminalProjection::version() const
{
    return version_;
}

const TerminalSemanticSnapshot& RemoteTerminalProjection::snapshot() const
{
    return snapshot_;
}

const std::string& RemoteTerminalProjection::controller_client_id() const
{
    return controller_client_id_;
}

bool RemoteTerminalProjection::is_controller(
    std::string_view client_id) const
{
    return attached_ && controller_client_id_ == client_id;
}

std::optional<std::string>
RemoteTerminalProjection::take_clipboard_write()
{
    return std::exchange(pending_clipboard_write_, std::nullopt);
}

bool RemoteTerminalProjection::apply_snapshot(
    const RemoteTerminalEvent& event, bool allow_resync, std::string& error)
{
    if (event.kind != RemoteTerminalEventKind::Snapshot || !event.snapshot)
    {
        error = "Remote terminal snapshot event is incomplete.";
        return false;
    }
    if (event.version.server_epoch.empty()
        || event.version.terminal_id.empty()
        || event.version.generation == 0)
    {
        error = "Remote terminal snapshot version is invalid.";
        return false;
    }
    if (allow_resync
        && event.version.generation < version_.generation)
    {
        error = "Remote terminal resync snapshot has a stale generation.";
        return false;
    }
    if (allow_resync
        && event.version.generation == version_.generation
        && event.version.sequence < version_.sequence)
    {
        error = "Remote terminal resync snapshot is stale.";
        return false;
    }
    const size_t expected_cells
        = static_cast<size_t>(event.snapshot->cols) * event.snapshot->rows;
    if (event.snapshot->cols <= 0 || event.snapshot->rows <= 0
        || event.snapshot->cells.size() != expected_cells)
    {
        error = "Remote terminal snapshot dimensions are invalid.";
        return false;
    }
    snapshot_ = *event.snapshot;
    version_ = event.version;
    controller_client_id_ = event.controller_client_id;
    return true;
}

bool RemoteTerminalProjection::apply_delta(
    const TerminalDirtySnapshot& delta, std::string& error)
{
    if (delta.cols <= 0 || delta.rows <= 0)
    {
        error = "Remote terminal delta dimensions are invalid.";
        return false;
    }
    const bool dimensions_changed
        = delta.cols != snapshot_.cols || delta.rows != snapshot_.rows;
    if (dimensions_changed && !delta.full)
    {
        error = "Remote terminal resize delta is not complete.";
        return false;
    }
    if (delta.full)
    {
        const size_t expected_cells
            = static_cast<size_t>(delta.cols) * delta.rows;
        if (delta.cells.size() != expected_cells)
        {
            error = "Complete remote terminal delta has missing cells.";
            return false;
        }
        snapshot_.cols = delta.cols;
        snapshot_.rows = delta.rows;
        snapshot_.cells.assign(expected_cells, {});
    }

    for (const auto& dirty : delta.cells)
    {
        if (dirty.col < 0 || dirty.col >= delta.cols
            || dirty.row < 0 || dirty.row >= delta.rows)
        {
            error = "Remote terminal delta cell is out of range.";
            return false;
        }
        const size_t index = static_cast<size_t>(dirty.row) * delta.cols
            + static_cast<size_t>(dirty.col);
        if (index >= snapshot_.cells.size())
        {
            error = "Remote terminal delta cell index is invalid.";
            return false;
        }
        snapshot_.cells[index] = dirty.cell;
    }
    snapshot_.metadata = delta.metadata;
    return true;
}

RemoteTerminalClient::RemoteTerminalClient(
    RemoteTerminalClientOptions options)
    : options_(std::move(options))
{
}

bool RemoteTerminalClient::attach(std::string& error)
{
    const auto started_at = std::chrono::steady_clock::now();
    nlohmann::json result;
    if (!request(method("attach"), client_params(), result, error))
        return false;
    std::string parse_error;
    auto attach = remote_terminal_attach_from_json(result, parse_error);
    if (!attach)
    {
        last_error_code_ = "invalid_attach";
        error = std::move(parse_error);
        return false;
    }
    if (!options_.expected_server_epoch.empty()
        && attach->state.version.server_epoch
            != options_.expected_server_epoch)
    {
        last_error_code_ = "stale_epoch";
        error = "Attached server epoch does not match the negotiated server.";
        return false;
    }
    if (!projection_.attach(*attach, error))
    {
        last_error_code_ = "invalid_attach";
        return false;
    }
    last_attach_latency_
        = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started_at);
    return true;
}

bool RemoteTerminalClient::poll(bool& changed, std::string& error)
{
    changed = false;
    if (!projection_.attached())
    {
        last_error_code_ = "not_attached";
        error = "Attach before polling the remote terminal.";
        return false;
    }
    nlohmann::json params = client_params();
    params["server_epoch"] = projection_.version().server_epoch;
    params["terminal_id"] = projection_.version().terminal_id;
    params["generation"] = projection_.version().generation;
    params["after_sequence"] = projection_.version().sequence;
    nlohmann::json result;
    if (!request(method("poll"), std::move(params), result, error))
        return false;
    if (!result.is_object()
        || !result.contains("events") || !result["events"].is_array()
        || result["events"].size() > kRemoteTerminalMaxEventsPerPoll)
    {
        last_error_code_ = "invalid_poll";
        error = "Remote terminal poll response is invalid.";
        return false;
    }
    for (const auto& value : result["events"])
    {
        std::string parse_error;
        auto event = remote_terminal_event_from_json(value, parse_error);
        if (!event)
        {
            last_error_code_ = "invalid_event";
            error = std::move(parse_error);
            return false;
        }
        if (!projection_.apply(*event, error))
        {
            last_error_code_ = "invalid_event";
            return false;
        }
        changed = true;
    }
    return true;
}

bool RemoteTerminalClient::send_input(
    std::string_view text, std::string& error)
{
    nlohmann::json params = client_params();
    params["text"] = text;
    nlohmann::json result;
    return request(method("input"), std::move(params), result, error);
}

bool RemoteTerminalClient::resize(
    int cols, int rows, std::string& error)
{
    nlohmann::json params = client_params();
    params["cols"] = cols;
    params["rows"] = rows;
    nlohmann::json result;
    return request(method("resize"), std::move(params), result, error);
}

bool RemoteTerminalClient::take_control(std::string& error)
{
    nlohmann::json result;
    return request(
        method("take_control"), client_params(), result, error);
}

bool RemoteTerminalClient::disconnect(std::string& error)
{
    nlohmann::json result;
    return request(method("disconnect"), client_params(), result, error);
}

bool RemoteTerminalClient::restart(std::string& error)
{
    nlohmann::json result;
    return request(method("restart"), client_params(), result, error);
}

bool RemoteTerminalClient::read_scrollback(
    uint64_t offset_from_live, size_t max_rows,
    RemoteTerminalScrollbackPage& page, std::string& error)
{
    if (!projection_.attached())
    {
        last_error_code_ = "not_attached";
        error = "Attach before reading remote terminal scrollback.";
        return false;
    }
    nlohmann::json params = client_params();
    params["server_epoch"] = projection_.version().server_epoch;
    params["terminal_id"] = projection_.version().terminal_id;
    params["generation"] = projection_.version().generation;
    params["after_sequence"] = projection_.version().sequence;
    params["offset_from_live"] = offset_from_live;
    params["max_rows"] = max_rows;
    nlohmann::json result;
    if (!request(method("scrollback"), std::move(params), result, error))
        return false;
    auto parsed = remote_terminal_scrollback_page_from_json(result, error);
    if (!parsed)
    {
        last_error_code_ = "invalid_scrollback";
        return false;
    }
    if (parsed->version.server_epoch != projection_.version().server_epoch
        || parsed->version.terminal_id != projection_.version().terminal_id
        || parsed->version.generation != projection_.version().generation)
    {
        last_error_code_ = "stale_scrollback";
        error = "Remote terminal scrollback identity is stale.";
        return false;
    }
    page = std::move(*parsed);
    return true;
}

const RemoteTerminalClientOptions& RemoteTerminalClient::options() const
{
    return options_;
}

const RemoteTerminalProjection& RemoteTerminalClient::projection() const
{
    return projection_;
}

std::optional<std::string>
RemoteTerminalClient::take_clipboard_write()
{
    return projection_.take_clipboard_write();
}

std::chrono::microseconds
RemoteTerminalClient::last_attach_latency() const
{
    return last_attach_latency_;
}

const std::string& RemoteTerminalClient::last_error_code() const
{
    return last_error_code_;
}

bool RemoteTerminalClient::request(
    std::string_view method, nlohmann::json params,
    nlohmann::json& result, std::string& error)
{
    const auto response = ControlClient::request(
        namespaced_control_id(kServerControlId, options_.runtime_directory),
        options_.runtime_directory, method, std::move(params));
    if (!response.ok)
    {
        last_error_code_ = response.error_code;
        error = response.error_message;
        return false;
    }
    last_error_code_.clear();
    result = response.result;
    return true;
}

nlohmann::json RemoteTerminalClient::client_params() const
{
    nlohmann::json params{
        { "client_id", options_.client_id },
    };
    if (!options_.terminal_id.empty())
        params["terminal_id"] = options_.terminal_id;
    return params;
}

std::string RemoteTerminalClient::method(std::string_view operation) const
{
    return options_.method_prefix + "." + std::string(operation);
}

} // namespace draxul
