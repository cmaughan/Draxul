#include "remote_terminal_service.h"

#include <draxul/log.h>
#include <draxul/remote_terminal_protocol.h>
#include <draxul/server_protocol.h>

#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>

namespace draxul
{

namespace
{

constexpr size_t kPollPayloadBudget
    = kControlMaxMessageBytes - 64 * 1024;
constexpr auto kLoopLatencyWarningThreshold
    = std::chrono::milliseconds(100);
constexpr size_t kCompletedMutationLimit = 1024;

bool read_int64(const nlohmann::json& value, int64_t& result)
{
    if (value.is_number_unsigned())
    {
        const uint64_t encoded = value.get<uint64_t>();
        if (encoded
            > static_cast<uint64_t>(
                std::numeric_limits<int64_t>::max()))
        {
            return false;
        }
        result = static_cast<int64_t>(encoded);
        return true;
    }
    if (!value.is_number_integer())
        return false;
    result = value.get<int64_t>();
    return true;
}

size_t replace_safe_json_size(const nlohmann::json& value)
{
    return value
        .dump(-1, ' ', false,
            nlohmann::detail::error_handler_t::replace)
        .size();
}

bool strip_hyperlinks(RemoteTerminalEvent& event)
{
    bool changed = false;
    if (event.snapshot)
    {
        for (auto& cell : event.snapshot->cells)
        {
            if (!cell.hyperlink.empty())
            {
                cell.hyperlink.clear();
                changed = true;
            }
        }
    }
    if (event.delta)
    {
        for (auto& dirty : event.delta->cells)
        {
            if (!dirty.cell.hyperlink.empty())
            {
                dirty.cell.hyperlink.clear();
                changed = true;
            }
        }
    }
    return changed;
}

bool strip_attrs(RemoteTerminalEvent& event)
{
    bool changed = false;
    if (event.snapshot)
    {
        for (auto& cell : event.snapshot->cells)
        {
            if (cell.attr != HlAttr{})
            {
                cell.attr = {};
                changed = true;
            }
        }
    }
    if (event.delta)
    {
        for (auto& dirty : event.delta->cells)
        {
            if (dirty.cell.attr != HlAttr{})
            {
                dirty.cell.attr = {};
                changed = true;
            }
        }
    }
    return changed;
}

} // namespace

RemoteTerminalService::RemoteTerminalService(
    RemoteTerminalServiceOptions options, IRemoteTerminalRuntime& runtime)
    : options_(std::move(options))
    , runtime_(runtime)
    , preferred_controller_client_id_(
          options_.preferred_controller_client_id)
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
    return valid_server_client_id(client_id);
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

bool RemoteTerminalService::mutation_key(
    const nlohmann::json& params, std::string_view operation,
    std::string& key) const
{
    key.clear();
    if (!params.contains("request_id"))
        return true;
    if (!params["request_id"].is_number_unsigned())
        return false;
    const uint64_t request_id = params["request_id"].get<uint64_t>();
    if (request_id == 0)
        return false;
    std::string client_id;
    if (!read_client_id(params, client_id))
        return false;
    key = client_id + ":" + std::string(operation)
        + ":" + std::to_string(request_id);
    return true;
}

std::optional<ControlMethodResult>
RemoteTerminalService::cached_mutation(const std::string& key) const
{
    if (key.empty())
        return std::nullopt;
    const auto found = completed_mutations_.find(key);
    if (found == completed_mutations_.end())
        return std::nullopt;
    return found->second;
}

ControlMethodResult RemoteTerminalService::remember_mutation(
    std::string key, ControlMethodResult result)
{
    if (!key.empty() && result.ok)
    {
        if (!completed_mutations_.contains(key))
            completed_mutation_order_.push_back(key);
        completed_mutations_[key] = result;
        while (completed_mutation_order_.size()
            > kCompletedMutationLimit)
        {
            completed_mutations_.erase(
                completed_mutation_order_.front());
            completed_mutation_order_.pop_front();
        }
    }
    return result;
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
        subscriber.queued_bytes = 0;
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
        .process_running = runtime_.is_running(),
        .exit_code = runtime_.exit_code(),
        .controller_client_id = controller_client_id_,
        .snapshot = runtime_.snapshot(),
    };
    ++snapshot_frames_;
    return event;
}

RemoteTerminalEvent RemoteTerminalService::make_delta_event()
{
    ++sequence_;
    RemoteTerminalEvent event{
        .kind = RemoteTerminalEventKind::Delta,
        .version = version(),
        .process_id = runtime_.process_id(),
        .process_running = runtime_.is_running(),
        .exit_code = runtime_.exit_code(),
        .controller_client_id = controller_client_id_,
        .delta = runtime_.take_delta(),
    };
    ++delta_frames_;
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
        .process_id = runtime_.process_id(),
        .process_running = runtime_.is_running(),
        .exit_code = runtime_.exit_code(),
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
        .process_id = runtime_.process_id(),
        .process_running = runtime_.is_running(),
        .exit_code = runtime_.exit_code(),
        .controller_client_id = controller_client_id_,
        .clipboard = std::move(text),
    };
}

std::shared_ptr<const RemoteTerminalService::EncodedEvent>
RemoteTerminalService::encode_event(
    RemoteTerminalEvent event, bool degrade_to_poll_budget)
{
    auto encoded = remote_terminal_event_to_json(event);
    size_t bytes = replace_safe_json_size(encoded);
    bool degraded = false;
    if (degrade_to_poll_budget && bytes > kPollPayloadBudget)
    {
        degraded = strip_hyperlinks(event);
        if (degraded)
        {
            encoded = remote_terminal_event_to_json(event);
            bytes = replace_safe_json_size(encoded);
        }
    }
    if (degrade_to_poll_budget && bytes > kPollPayloadBudget)
    {
        degraded = strip_attrs(event) || degraded;
        encoded = remote_terminal_event_to_json(event);
        bytes = replace_safe_json_size(encoded);
    }
    if (degrade_to_poll_budget && bytes > kPollPayloadBudget)
        return {};
    if (degraded)
        ++degraded_frames_;
    return std::make_shared<const EncodedEvent>(EncodedEvent{
        .event = std::move(event),
        .encoded = std::move(encoded),
        .bytes = bytes,
    });
}

void RemoteTerminalService::require_resync(Subscriber& subscriber)
{
    subscriber.events.clear();
    subscriber.queued_bytes = 0;
    if (!subscriber.needs_resync)
        ++resyncs_;
    subscriber.needs_resync = true;
}

void RemoteTerminalService::broadcast(const RemoteTerminalEvent& event)
{
    if (subscribers_.empty())
        return;
    const auto encoded = encode_event(event, false);
    if (!encoded)
        return;
    for (auto& [client_id, subscriber] : subscribers_)
    {
        (void)client_id;
        if (subscriber.needs_resync)
            continue;
        if (encoded->bytes > kRemoteTerminalSubscriberQueueByteLimit
            || subscriber.events.size() >= kRemoteTerminalQueueLimit
            || subscriber.queued_bytes
                > kRemoteTerminalSubscriberQueueByteLimit - encoded->bytes)
        {
            ++oversized_queue_events_;
            require_resync(subscriber);
            continue;
        }
        subscriber.events.push_back(encoded);
        subscriber.queued_bytes += encoded->bytes;
        max_queue_depth_
            = std::max(max_queue_depth_, subscriber.events.size());
        max_queue_bytes_
            = std::max(max_queue_bytes_, subscriber.queued_bytes);
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
    published_process_running_ = true;
    published_exit_code_.reset();

    subscribers_[client_id] = {};
    if (controller_client_id_.empty()
        && (preferred_controller_client_id_.empty()
            || preferred_controller_client_id_ == client_id))
    {
        controller_client_id_ = client_id;
        preferred_controller_client_id_.clear();
        broadcast(make_controller_event());
        subscribers_[client_id].events.clear();
        subscribers_[client_id].queued_bytes = 0;
    }
    const auto state = encode_event(snapshot_event(), true);
    if (!state)
    {
        return ControlMethodResult::error(
            "frame_too_large",
            "The terminal snapshot exceeds the transport frame budget.");
    }
    snapshot_bytes_ += state->bytes;
    RemoteTerminalAttach result{
        .pane = {
            .pane_id = options_.pane_id,
            .terminal_id = options_.terminal_id,
            .name = options_.name,
            .execution_domain = "server_terminal",
            .process_id = runtime_.process_id(),
            .process_running = runtime_.is_running(),
            .exit_code = runtime_.exit_code(),
        },
        .state = state->event,
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
        const auto encoded = encode_event(snapshot_event(), true);
        if (!encoded)
        {
            return ControlMethodResult::error(
                "frame_too_large",
                "The terminal snapshot exceeds the transport frame budget.");
        }
        snapshot_bytes_ += encoded->bytes;
        events.push_back(encoded->encoded);
        delivery.events.clear();
        delivery.queued_bytes = 0;
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
            && delivery.events.front()->event.version.sequence
                <= requested.sequence)
        {
            delivery.queued_bytes -= std::min(
                delivery.queued_bytes,
                delivery.events.front()->bytes);
            delivery.events.pop_front();
        }
        size_t count = 0;
        size_t payload_bytes = 0;
        for (const auto& encoded : delivery.events)
        {
            if (count++ >= kRemoteTerminalMaxEventsPerPoll)
                break;
            if (encoded->bytes > kPollPayloadBudget - payload_bytes)
            {
                break;
            }
            events.push_back(encoded->encoded);
            payload_bytes += encoded->bytes;
            if (encoded->event.kind == RemoteTerminalEventKind::Snapshot)
                snapshot_bytes_ += encoded->bytes;
            else if (encoded->event.kind == RemoteTerminalEventKind::Delta)
                delta_bytes_ += encoded->bytes;
        }
    }
    return ControlMethodResult::success({ { "events", std::move(events) } });
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
    std::string mutation;
    if (!mutation_key(params, "input", mutation))
    {
        return ControlMethodResult::error(
            "invalid_request_id", "A valid request_id is required.");
    }
    if (auto cached = cached_mutation(mutation))
        return *cached;
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
    const RemoteTerminalInputResult input_result
        = runtime_.send_input(text);
    if (input_result == RemoteTerminalInputResult::Backpressure)
    {
        return ControlMethodResult::error(
            "backpressure",
            "The terminal input queue is full; retry after the shell drains input.");
    }
    if (input_result == RemoteTerminalInputResult::Failed)
    {
        return ControlMethodResult::error(
            "process_write_failed", "The terminal process rejected input.");
    }
    publish_runtime_updates(runtime_.pump());
    return remember_mutation(std::move(mutation),
        ControlMethodResult::success({
        { "accepted", true },
        { "sequence", sequence_ },
    }));
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
    std::string mutation;
    if (!mutation_key(params, "resize", mutation))
    {
        return ControlMethodResult::error(
            "invalid_request_id", "A valid request_id is required.");
    }
    if (auto cached = cached_mutation(mutation))
        return *cached;
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
    int64_t cols = 0;
    int64_t rows = 0;
    if (!read_int64(params["cols"], cols)
        || !read_int64(params["rows"], rows)
        || cols <= 0 || rows <= 0
        || cols > kRemoteTerminalMaxColumns
        || rows > kRemoteTerminalMaxRows
        || static_cast<uint64_t>(cols) * static_cast<uint64_t>(rows)
            > kRemoteTerminalMaxCells)
    {
        return ControlMethodResult::error(
            "invalid_resize", "Terminal dimensions are out of range.");
    }
    if (!runtime_.resize(
            static_cast<int>(cols), static_cast<int>(rows)))
    {
        return ControlMethodResult::error(
            "process_resize_failed", "The terminal process rejected the resize.");
    }
    const auto event = make_delta_event();
    broadcast(event);
    return remember_mutation(std::move(mutation),
        ControlMethodResult::success({
        { "accepted", true },
        { "sequence", event.version.sequence },
    }));
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
    std::string mutation;
    if (!mutation_key(params, "take_control", mutation))
    {
        return ControlMethodResult::error(
            "invalid_request_id", "A valid request_id is required.");
    }
    if (auto cached = cached_mutation(mutation))
        return *cached;
    if (!subscribers_.contains(client_id))
    {
        return ControlMethodResult::error(
            "not_attached", "Attach to the terminal before taking control.");
    }
    if (controller_client_id_ != client_id)
    {
        controller_client_id_ = client_id;
        preferred_controller_client_id_.clear();
        broadcast(make_controller_event());
    }
    return remember_mutation(std::move(mutation),
        ControlMethodResult::success({
        { "controller_client_id", controller_client_id_ },
        { "sequence", sequence_ },
    }));
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
    std::string mutation;
    if (!mutation_key(params, "disconnect", mutation))
    {
        return ControlMethodResult::error(
            "invalid_request_id", "A valid request_id is required.");
    }
    if (auto cached = cached_mutation(mutation))
        return *cached;
    disconnect_client(client_id);
    return remember_mutation(std::move(mutation),
        ControlMethodResult::success({ { "disconnected", true } }));
}

void RemoteTerminalService::disconnect_client(std::string_view client_id)
{
    const bool was_controller = controller_client_id_ == client_id;
    subscribers_.erase(std::string(client_id));
    if (was_controller)
    {
        controller_client_id_.clear();
        broadcast(make_controller_event());
    }
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
    std::string mutation;
    if (!mutation_key(params, "restart", mutation))
    {
        return ControlMethodResult::error(
            "invalid_request_id", "A valid request_id is required.");
    }
    if (auto cached = cached_mutation(mutation))
        return *cached;
    if (controller_client_id_ != client_id)
    {
        return ControlMethodResult::error(
            "not_controller", "This client is observing the terminal.");
    }
    std::string error;
    if (!restart_runtime(error))
        return ControlMethodResult::error("process_start_failed", std::move(error));
    return remember_mutation(std::move(mutation),
        ControlMethodResult::success({
        { "generation", generation_ },
        { "process_id", runtime_.process_id() },
    }));
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
    published_process_running_ = true;
    published_exit_code_.reset();
    ++generation_;
    sequence_ = 0;
    for (auto& [subscriber_id, subscriber] : subscribers_)
    {
        (void)subscriber_id;
        subscriber.events.clear();
        subscriber.queued_bytes = 0;
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
    const uint64_t requested_max_rows
        = params["max_rows"].get<uint64_t>();
    const size_t max_rows = static_cast<size_t>(std::min<uint64_t>(
        requested_max_rows,
        kRemoteTerminalMaxScrollbackPageRows));
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
        { "max_queue_bytes", max_queue_bytes_ },
        { "queue_limit", kRemoteTerminalQueueLimit },
        { "queue_byte_limit",
            kRemoteTerminalSubscriberQueueByteLimit },
        { "poll_payload_budget", kPollPayloadBudget },
        { "resyncs", resyncs_ },
        { "degraded_frames", degraded_frames_ },
        { "oversized_queue_events", oversized_queue_events_ },
        { "subscribers", subscribers_.size() },
        { "scrollback_requests", scrollback_requests_ },
        { "scrollback_rows_served", scrollback_rows_served_ },
        { "max_loop_interval_ms", max_loop_interval_ms_ },
        { "loop_latency_warnings", loop_latency_warnings_ },
    });
}

void RemoteTerminalService::pump()
{
    const auto now = std::chrono::steady_clock::now();
    if (last_pump_at_)
    {
        const auto interval
            = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - *last_pump_at_);
        max_loop_interval_ms_ = std::max<uint64_t>(
            max_loop_interval_ms_,
            static_cast<uint64_t>(std::max<int64_t>(
                0, interval.count())));
        if (interval > kLoopLatencyWarningThreshold)
        {
            ++loop_latency_warnings_;
            DRAXUL_LOG_WARN(LogCategory::App,
                "Server terminal loop interval was %lld ms (threshold %lld ms)",
                static_cast<long long>(interval.count()),
                static_cast<long long>(
                    kLoopLatencyWarningThreshold.count()));
        }
    }
    last_pump_at_ = now;
    if (started_)
    {
        publish_runtime_updates(runtime_.pump());
        const bool process_running = runtime_.is_running();
        const std::optional<int> exit_code = runtime_.exit_code();
        if (process_running != published_process_running_
            || exit_code != published_exit_code_)
        {
            published_process_running_ = process_running;
            published_exit_code_ = exit_code;
            broadcast(make_controller_event());
        }
    }
}

bool RemoteTerminalService::started() const
{
    return started_;
}

} // namespace draxul
