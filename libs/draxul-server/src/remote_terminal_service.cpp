#include "remote_terminal_service.h"

#include <draxul/remote_terminal_protocol.h>

#include <algorithm>
#include <nlohmann/json.hpp>

namespace draxul
{

namespace
{

constexpr size_t kPollPayloadBudget
    = kControlMaxMessageBytes - 64 * 1024;

} // namespace

RemoteTerminalService::RemoteTerminalService(
    RemoteTerminalServiceOptions options, IRemoteTerminalRuntime& runtime)
    : options_(std::move(options))
    , runtime_(runtime)
{
}

bool RemoteTerminalService::handles(std::string_view method) const
{
    return method.starts_with(options_.method_prefix + ".");
}

ControlMethodResult RemoteTerminalService::handle(
    std::string_view method, const nlohmann::json& params)
{
    const std::string suffix(method.substr(options_.method_prefix.size() + 1));
    if (suffix == "attach")
        return attach(params);
    if (suffix == "poll")
        return poll(params);
    if (suffix == "input")
        return input(params);
    if (suffix == "resize")
        return resize(params);
    if (suffix == "take_control")
        return take_control(params);
    if (suffix == "disconnect")
        return disconnect(params);
    if (suffix == "restart")
        return restart(params);
    if (suffix == "scrollback")
        return read_scrollback(params);
    if (suffix == "metrics")
        return metrics();
    return ControlMethodResult::error(
        "unknown_method", "Unknown remote terminal method.");
}

bool RemoteTerminalService::read_client_id(
    const nlohmann::json& params, std::string& client_id) const
{
    if (!params.is_object()
        || !params.contains("client_id")
        || !params["client_id"].is_string())
    {
        return false;
    }
    client_id = params["client_id"].get<std::string>();
    return !client_id.empty() && client_id.size() <= 128;
}

bool RemoteTerminalService::read_version(
    const nlohmann::json& params, RemoteTerminalVersion& requested) const
{
    if (!params.is_object()
        || !params.contains("server_epoch")
        || !params["server_epoch"].is_string()
        || !params.contains("terminal_id")
        || !params["terminal_id"].is_string()
        || !params.contains("generation")
        || !params["generation"].is_number_unsigned()
        || !params.contains("after_sequence")
        || !params["after_sequence"].is_number_unsigned())
    {
        return false;
    }
    requested.server_epoch = params["server_epoch"].get<std::string>();
    requested.terminal_id = params["terminal_id"].get<std::string>();
    requested.generation = params["generation"].get<uint64_t>();
    requested.sequence = params["after_sequence"].get<uint64_t>();
    return true;
}

bool RemoteTerminalService::ensure_runtime_started(std::string& error)
{
    if (runtime_.is_running())
        return true;
    if (!started_)
    {
        if (!runtime_.ensure_started(error))
            return false;
        started_ = true;
        return true;
    }
    if (options_.prepare_restart_generation)
    {
        options_.prepare_restart_generation(
            generation_ + 1);
    }
    if (!runtime_.restart(error))
        return false;
    ++generation_;
    sequence_ = 0;
    for (auto& [client_id, subscriber] : subscribers_)
    {
        (void)client_id;
        subscriber.events.clear();
        subscriber.needs_resync = true;
    }
    return true;
}

RemoteTerminalVersion RemoteTerminalService::version() const
{
    return {
        .server_epoch = options_.server_epoch,
        .terminal_id = options_.terminal_id,
        .generation = generation_,
        .sequence = sequence_,
    };
}

RemoteTerminalEvent RemoteTerminalService::snapshot_event()
{
    RemoteTerminalEvent event{
        .kind = RemoteTerminalEventKind::Snapshot,
        .version = version(),
        .process_id = runtime_.process_id(),
        .controller_client_id = controller_client_id_,
        .snapshot = runtime_.snapshot(),
    };
    ++snapshot_frames_;
    snapshot_bytes_ += remote_terminal_event_to_json(event).dump().size();
    return event;
}

RemoteTerminalEvent RemoteTerminalService::make_delta_event()
{
    ++sequence_;
    RemoteTerminalEvent event{
        .kind = RemoteTerminalEventKind::Delta,
        .version = version(),
        .controller_client_id = controller_client_id_,
        .delta = runtime_.take_delta(),
    };
    ++delta_frames_;
    delta_bytes_ += remote_terminal_event_to_json(event).dump().size();
    if (event.delta)
    {
        delta_cells_ += event.delta->cells.size();
        full_frame_cells_ += static_cast<uint64_t>(event.delta->cols)
            * static_cast<uint64_t>(event.delta->rows);
    }
    return event;
}

RemoteTerminalEvent RemoteTerminalService::make_controller_event()
{
    ++sequence_;
    return {
        .kind = RemoteTerminalEventKind::Controller,
        .version = version(),
        .controller_client_id = controller_client_id_,
    };
}

RemoteTerminalEvent RemoteTerminalService::make_clipboard_event(
    std::string text)
{
    ++sequence_;
    return {
        .kind = RemoteTerminalEventKind::Clipboard,
        .version = version(),
        .controller_client_id = controller_client_id_,
        .clipboard = std::move(text),
    };
}

void RemoteTerminalService::broadcast(const RemoteTerminalEvent& event)
{
    for (auto& [client_id, subscriber] : subscribers_)
    {
        (void)client_id;
        if (subscriber.needs_resync)
            continue;
        if (subscriber.events.size() >= kRemoteTerminalQueueLimit)
        {
            subscriber.events.clear();
            subscriber.needs_resync = true;
            ++resyncs_;
            continue;
        }
        subscriber.events.push_back(event);
        max_queue_depth_
            = std::max(max_queue_depth_, subscriber.events.size());
    }
}

void RemoteTerminalService::publish_runtime_updates(bool terminal_changed)
{
    if (terminal_changed)
        broadcast(make_delta_event());
    if (auto clipboard = runtime_.take_clipboard_write())
        broadcast(make_clipboard_event(std::move(*clipboard)));
}

ControlMethodResult RemoteTerminalService::attach(
    const nlohmann::json& params)
{
    std::string client_id;
    if (!read_client_id(params, client_id))
    {
        return ControlMethodResult::error(
            "invalid_client", "A valid client_id is required.");
    }
    if (params.contains("terminal_id")
        && (!params["terminal_id"].is_string()
            || params["terminal_id"].get<std::string>()
                != options_.terminal_id))
    {
        return ControlMethodResult::error(
            "terminal_not_found",
            "The requested server terminal does not exist.");
    }
    std::string error;
    if (!ensure_runtime_started(error))
        return ControlMethodResult::error("process_start_failed", std::move(error));

    subscribers_[client_id] = {};
    if (controller_client_id_.empty())
    {
        controller_client_id_ = client_id;
        broadcast(make_controller_event());
        subscribers_[client_id].events.clear();
    }
    RemoteTerminalAttach result{
        .pane = {
            .pane_id = options_.pane_id,
            .terminal_id = options_.terminal_id,
            .name = options_.name,
            .execution_domain = "server_terminal",
            .process_id = runtime_.process_id(),
        },
        .state = snapshot_event(),
    };
    return ControlMethodResult::success(remote_terminal_attach_to_json(result));
}

ControlMethodResult RemoteTerminalService::poll(
    const nlohmann::json& params)
{
    std::string client_id;
    RemoteTerminalVersion requested;
    if (!read_client_id(params, client_id) || !read_version(params, requested))
    {
        return ControlMethodResult::error(
            "invalid_poll", "A valid client and terminal version are required.");
    }
    auto found = subscribers_.find(client_id);
    if (found == subscribers_.end())
    {
        return ControlMethodResult::error(
            "not_attached", "Attach to the terminal before polling.");
    }
    if (requested.server_epoch != options_.server_epoch)
    {
        return ControlMethodResult::error(
            "stale_epoch", "The terminal server epoch has changed.");
    }
    if (requested.terminal_id != options_.terminal_id)
    {
        return ControlMethodResult::error(
            "stale_terminal", "The terminal identity has changed.");
    }

    auto& delivery = found->second;
    nlohmann::json events = nlohmann::json::array();
    if (requested.generation != generation_ || delivery.needs_resync)
    {
        events.push_back(remote_terminal_event_to_json(snapshot_event()));
        delivery.events.clear();
        delivery.needs_resync = false;
    }
    else
    {
        if (requested.sequence > sequence_)
        {
            return ControlMethodResult::error(
                "stale_sequence", "The client sequence is ahead of the server.");
        }
        while (!delivery.events.empty()
            && delivery.events.front().version.sequence <= requested.sequence)
        {
            delivery.events.pop_front();
        }
        size_t count = 0;
        size_t payload_bytes = 0;
        for (const auto& event : delivery.events)
        {
            if (count++ >= kRemoteTerminalMaxEventsPerPoll)
                break;
            auto encoded = remote_terminal_event_to_json(event);
            const size_t encoded_bytes = encoded.dump().size();
            if (!events.empty()
                && payload_bytes + encoded_bytes > kPollPayloadBudget)
            {
                break;
            }
            events.push_back(std::move(encoded));
            payload_bytes += encoded_bytes;
        }
    }
    return ControlMethodResult::success({
        { "events", std::move(events) },
        { "server_sequence", sequence_ },
    });
}

ControlMethodResult RemoteTerminalService::input(
    const nlohmann::json& params)
{
    std::string client_id;
    if (!read_client_id(params, client_id)
        || !params.contains("text") || !params["text"].is_string())
    {
        return ControlMethodResult::error(
            "invalid_input", "A valid client_id and text are required.");
    }
    if (!subscribers_.contains(client_id))
    {
        return ControlMethodResult::error(
            "not_attached", "Attach to the terminal before sending input.");
    }
    if (client_id != controller_client_id_)
    {
        return ControlMethodResult::error(
            "not_controller", "This client is observing the terminal.");
    }
    const std::string text = params["text"].get<std::string>();
    if (text.empty() || text.size() > 64 * 1024)
    {
        return ControlMethodResult::error(
            "invalid_input", "Terminal input must be between 1 and 65536 bytes.");
    }
    if (!runtime_.send_input(text))
    {
        return ControlMethodResult::error(
            "process_write_failed", "The terminal process rejected input.");
    }
    publish_runtime_updates(runtime_.pump());
    return ControlMethodResult::success({
        { "accepted", true },
        { "sequence", sequence_ },
    });
}

ControlMethodResult RemoteTerminalService::resize(
    const nlohmann::json& params)
{
    std::string client_id;
    if (!read_client_id(params, client_id)
        || !params.contains("cols") || !params["cols"].is_number_integer()
        || !params.contains("rows") || !params["rows"].is_number_integer())
    {
        return ControlMethodResult::error(
            "invalid_resize", "A valid client_id, cols, and rows are required.");
    }
    if (!subscribers_.contains(client_id))
    {
        return ControlMethodResult::error(
            "not_attached", "Attach to the terminal before resizing.");
    }
    if (client_id != controller_client_id_)
    {
        return ControlMethodResult::error(
            "not_controller", "This client is observing the terminal.");
    }
    const int cols = params["cols"].get<int>();
    const int rows = params["rows"].get<int>();
    if (cols <= 0 || rows <= 0
        || cols > TerminalStateLimits::kMaxColumns
        || rows > TerminalStateLimits::kMaxRows
        || static_cast<size_t>(cols) * static_cast<size_t>(rows)
            > TerminalStateLimits::kMaxCells)
    {
        return ControlMethodResult::error(
            "invalid_resize", "Terminal dimensions are out of range.");
    }
    if (!runtime_.resize(cols, rows))
    {
        return ControlMethodResult::error(
            "process_resize_failed", "The terminal process rejected the resize.");
    }
    const auto event = make_delta_event();
    broadcast(event);
    return ControlMethodResult::success({
        { "accepted", true },
        { "sequence", event.version.sequence },
    });
}

ControlMethodResult RemoteTerminalService::take_control(
    const nlohmann::json& params)
{
    std::string client_id;
    if (!read_client_id(params, client_id))
    {
        return ControlMethodResult::error(
            "invalid_client", "A valid client_id is required.");
    }
    if (!subscribers_.contains(client_id))
    {
        return ControlMethodResult::error(
            "not_attached", "Attach to the terminal before taking control.");
    }
    if (controller_client_id_ != client_id)
    {
        controller_client_id_ = client_id;
        broadcast(make_controller_event());
    }
    return ControlMethodResult::success({
        { "controller_client_id", controller_client_id_ },
        { "sequence", sequence_ },
    });
}

ControlMethodResult RemoteTerminalService::disconnect(
    const nlohmann::json& params)
{
    std::string client_id;
    if (!read_client_id(params, client_id))
    {
        return ControlMethodResult::error(
            "invalid_client", "A valid client_id is required.");
    }
    const bool was_controller = controller_client_id_ == client_id;
    subscribers_.erase(client_id);
    if (was_controller)
    {
        controller_client_id_.clear();
        broadcast(make_controller_event());
    }
    return ControlMethodResult::success({ { "disconnected", true } });
}

ControlMethodResult RemoteTerminalService::restart(
    const nlohmann::json& params)
{
    std::string client_id;
    if (!read_client_id(params, client_id)
        || !subscribers_.contains(client_id))
    {
        return ControlMethodResult::error(
            "not_attached", "Attach to the terminal before restarting it.");
    }
    if (controller_client_id_ != client_id)
    {
        return ControlMethodResult::error(
            "not_controller", "This client is observing the terminal.");
    }
    std::string error;
    if (!restart_runtime(error))
        return ControlMethodResult::error("process_start_failed", std::move(error));
    return ControlMethodResult::success({
        { "generation", generation_ },
        { "process_id", runtime_.process_id() },
    });
}

bool RemoteTerminalService::restart_runtime(std::string& error)
{
    if (options_.prepare_restart_generation)
    {
        options_.prepare_restart_generation(
            generation_ + 1);
    }
    if (!runtime_.restart(error))
        return false;
    started_ = true;
    ++generation_;
    sequence_ = 0;
    for (auto& [subscriber_id, subscriber] : subscribers_)
    {
        (void)subscriber_id;
        subscriber.events.clear();
        subscriber.needs_resync = true;
    }
    return true;
}

ControlMethodResult RemoteTerminalService::read_scrollback(
    const nlohmann::json& params)
{
    std::string client_id;
    RemoteTerminalVersion requested;
    if (!read_client_id(params, client_id)
        || !read_version(params, requested)
        || !params.contains("offset_from_live")
        || !params["offset_from_live"].is_number_unsigned()
        || !params.contains("max_rows")
        || !params["max_rows"].is_number_unsigned())
    {
        return ControlMethodResult::error(
            "invalid_scrollback",
            "A valid client, terminal version, offset, and row count are required.");
    }
    if (!subscribers_.contains(client_id))
    {
        return ControlMethodResult::error(
            "not_attached", "Attach to the terminal before reading scrollback.");
    }
    if (requested.server_epoch != options_.server_epoch)
    {
        return ControlMethodResult::error(
            "stale_epoch", "The terminal server epoch has changed.");
    }
    if (requested.terminal_id != options_.terminal_id)
    {
        return ControlMethodResult::error(
            "stale_terminal", "The terminal identity has changed.");
    }
    if (requested.generation != generation_)
    {
        return ControlMethodResult::error(
            "stale_generation", "The terminal generation has changed.");
    }

    const uint64_t requested_offset
        = params["offset_from_live"].get<uint64_t>();
    const uint64_t total_rows = runtime_.scrollback_rows();
    const uint64_t offset = std::min(requested_offset, total_rows);
    const size_t max_rows = std::min(
        params["max_rows"].get<size_t>(),
        kRemoteTerminalMaxScrollbackPageRows);
    RemoteTerminalScrollbackPage page{
        .version = version(),
        .total_rows = total_rows,
        .offset_from_live = offset,
        .cols = runtime_.snapshot().cols,
        .snapshot = runtime_.scrollback_page(offset, max_rows),
    };
    ++scrollback_requests_;
    if (page.snapshot)
        scrollback_rows_served_ += page.snapshot->rows;
    return ControlMethodResult::success(
        remote_terminal_scrollback_page_to_json(page));
}

ControlMethodResult RemoteTerminalService::metrics() const
{
    return ControlMethodResult::success({
        { "sanitized", true },
        { "snapshot_frames", snapshot_frames_ },
        { "snapshot_bytes", snapshot_bytes_ },
        { "delta_frames", delta_frames_ },
        { "delta_bytes", delta_bytes_ },
        { "delta_cells", delta_cells_ },
        { "full_frame_cells", full_frame_cells_ },
        { "max_queue_depth", max_queue_depth_ },
        { "queue_limit", kRemoteTerminalQueueLimit },
        { "resyncs", resyncs_ },
        { "subscribers", subscribers_.size() },
        { "scrollback_requests", scrollback_requests_ },
        { "scrollback_rows_served", scrollback_rows_served_ },
    });
}

void RemoteTerminalService::pump()
{
    if (started_)
        publish_runtime_updates(runtime_.pump());
}

bool RemoteTerminalService::started() const
{
    return started_;
}

} // namespace draxul
