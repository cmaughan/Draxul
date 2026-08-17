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

std::string RemoteTerminalService::subscriber_key(
    std::string_view client_id, uint64_t subscription_id)
{
    if (subscription_id == 0)
        return std::string(client_id);
    return std::string(client_id) + '\n'
        + std::to_string(subscription_id);
}

RemoteTerminalService::Subscriber*
RemoteTerminalService::find_subscriber(
    std::string_view client_id, uint64_t subscription_id)
{
    const auto found = subscribers_.find(
        subscriber_key(client_id, subscription_id));
    return found == subscribers_.end() ? nullptr : &found->second;
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
    if (suffix == "suspend")
        return suspend(params);
    if (suffix == "resume")
        return resume(params);
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

RemoteTerminalAttach RemoteTerminalService::make_attach(
    const std::shared_ptr<const EncodedEvent>& state) const
{
    return {
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
    const bool any_active = std::ranges::any_of(
        subscribers_, [](const auto& entry) {
            return !entry.second.suspended;
        });
    if (!any_active)
    {
        if (event.kind == RemoteTerminalEventKind::Delta)
            ++avoided_delta_encodes_;
        if (event.kind == RemoteTerminalEventKind::Clipboard)
            ++suppressed_clipboard_events_;
        return;
    }
    if (event.kind == RemoteTerminalEventKind::Clipboard)
    {
        const bool controller_active = std::ranges::any_of(
            subscribers_, [this](const auto& entry) {
                return entry.second.client_id
                        == controller_client_id_
                    && !entry.second.suspended;
            });
        if (!controller_active)
        {
            ++suppressed_clipboard_events_;
            return;
        }
    }
    const auto encoded = encode_event(event, false);
    if (!encoded)
        return;
    for (auto& [client_id, subscriber] : subscribers_)
    {
        (void)client_id;
        if (subscriber.suspended || subscriber.needs_resync)
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

    subscribers_[client_id] = { .client_id = client_id };
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
    RemoteTerminalAttach result = make_attach(state);
    return ControlMethodResult::success(remote_terminal_attach_to_json(result));
}

ControlMethodResult RemoteTerminalService::suspend(
    const nlohmann::json& params)
{
    std::string client_id;
    if (!read_client_id(params, client_id))
    {
        return ControlMethodResult::error(
            "invalid_client", "A valid client_id is required.");
    }
    std::string mutation;
    if (!mutation_key(params, "suspend", mutation))
    {
        return ControlMethodResult::error(
            "invalid_request_id", "A valid request_id is required.");
    }
    if (auto cached = cached_mutation(mutation))
        return *cached;
    const auto found = subscribers_.find(client_id);
    if (found == subscribers_.end())
    {
        return ControlMethodResult::error(
            "not_attached", "Attach to the terminal before suspending it.");
    }
    auto& subscriber = found->second;
    const bool changed = !subscriber.suspended;
    subscriber.events.clear();
    subscriber.queued_bytes = 0;
    subscriber.needs_resync = true;
    subscriber.suspended = true;
    if (changed)
        ++suspension_count_;
    return remember_mutation(std::move(mutation),
        ControlMethodResult::success({
            { "suspended", true },
            { "controller_retained",
                controller_client_id_ == client_id },
        }));
}

ControlMethodResult RemoteTerminalService::resume(
    const nlohmann::json& params)
{
    std::string client_id;
    if (!read_client_id(params, client_id))
    {
        return ControlMethodResult::error(
            "invalid_client", "A valid client_id is required.");
    }
    std::string error;
    if (!ensure_runtime_started(error))
    {
        return ControlMethodResult::error(
            "process_start_failed", std::move(error));
    }
    auto [found, inserted]
        = subscribers_.try_emplace(client_id);
    auto& subscriber = found->second;
    subscriber.client_id = client_id;
    const bool changed = inserted || subscriber.suspended;
    subscriber.events.clear();
    subscriber.queued_bytes = 0;
    subscriber.needs_resync = false;
    subscriber.suspended = false;
    if (changed)
        ++resume_count_;
    if (controller_client_id_.empty()
        && (preferred_controller_client_id_.empty()
            || preferred_controller_client_id_ == client_id))
    {
        controller_client_id_ = client_id;
        preferred_controller_client_id_.clear();
        broadcast(make_controller_event());
        subscriber.events.clear();
        subscriber.queued_bytes = 0;
    }
    const auto state = encode_event(snapshot_event(), true);
    if (!state)
    {
        return ControlMethodResult::error(
            "frame_too_large",
            "The terminal snapshot exceeds the transport frame budget.");
    }
    snapshot_bytes_ += state->bytes;
    RemoteTerminalAttach result = make_attach(state);
    return ControlMethodResult::success(
        remote_terminal_attach_to_json(result));
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
    Subscriber* delivery = find_subscriber(client_id, 0);
    if (!delivery)
    {
        return ControlMethodResult::error(
            "not_attached", "Attach to the terminal before polling.");
    }
    auto slice = poll_delivery(*delivery, requested,
        kPollPayloadBudget, kPollPayloadBudget,
        kRemoteTerminalMaxEventsPerPoll, false);
    if (!slice.ok())
    {
        return ControlMethodResult::error(
            std::move(slice.error_code),
            std::move(slice.error_message));
    }
    nlohmann::json events = nlohmann::json::array();
    for (const auto& event : slice.events)
        events.push_back(remote_terminal_event_to_json(event));
    return ControlMethodResult::success({ { "events", std::move(events) } });
}

RemoteTerminalPollSlice RemoteTerminalService::poll_delivery(
    Subscriber& delivery, const RemoteTerminalVersion& requested,
    size_t soft_byte_budget, size_t hard_byte_budget,
    size_t event_limit, bool resync_stale_sequence)
{
    RemoteTerminalPollSlice result;
    if (requested.server_epoch != options_.server_epoch)
    {
        result.error_code = "stale_epoch";
        result.error_message = "The terminal server epoch has changed.";
        return result;
    }
    if (requested.terminal_id != options_.terminal_id)
    {
        result.error_code = "stale_terminal";
        result.error_message = "The terminal identity has changed.";
        return result;
    }
    if (delivery.suspended)
    {
        result.error_code = "suspended";
        result.error_message = "Resume the terminal before polling it.";
        return result;
    }
    const bool requires_resync
        = requested.generation != generation_ || delivery.needs_resync;
    const bool stale_sequence = requested.sequence > sequence_;
    if (stale_sequence && !resync_stale_sequence && !requires_resync)
    {
        result.error_code = "stale_sequence";
        result.error_message = "The client sequence is ahead of the server.";
        return result;
    }
    if (requires_resync || stale_sequence)
    {
        const auto encoded = encode_event(snapshot_event(), true);
        if (!encoded)
        {
            result.error_code = "frame_too_large";
            result.error_message
                = "The terminal snapshot exceeds the transport frame budget.";
            return result;
        }
        if (encoded->bytes > hard_byte_budget)
        {
            result.more = true;
            return result;
        }
        result.events.push_back(encoded->event);
        result.payload_bytes = encoded->bytes;
        result.resync = true;
        result.oversized = encoded->bytes > soft_byte_budget;
        snapshot_bytes_ += encoded->bytes;
        delivery.events.clear();
        delivery.queued_bytes = 0;
        delivery.needs_resync = false;
        return result;
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
    const size_t bounded_events = std::min(
        event_limit, kRemoteTerminalMaxEventsPerPoll);
    for (const auto& encoded : delivery.events)
    {
        if (result.events.size() >= bounded_events
            || encoded->bytes > hard_byte_budget - result.payload_bytes)
        {
            result.more = true;
            break;
        }
        if (encoded->bytes > soft_byte_budget - result.payload_bytes)
        {
            if (!result.events.empty())
            {
                result.more = true;
                break;
            }
            result.oversized = true;
        }
        result.events.push_back(encoded->event);
        result.payload_bytes += encoded->bytes;
        if (encoded->event.kind == RemoteTerminalEventKind::Snapshot)
            snapshot_bytes_ += encoded->bytes;
        else if (encoded->event.kind == RemoteTerminalEventKind::Delta)
            delta_bytes_ += encoded->bytes;
        if (result.oversized)
        {
            result.more = delivery.events.size() > 1;
            break;
        }
    }
    return result;
}

RemoteTerminalPollSlice RemoteTerminalService::poll_subscription(
    std::string_view client_id,
    const SessionTerminalSubscription& subscription,
    size_t soft_byte_budget, size_t hard_byte_budget,
    size_t event_limit)
{
    RemoteTerminalPollSlice result;
    if (subscription.terminal_id != options_.terminal_id)
    {
        result.error_code = "stale_terminal";
        result.error_message = "The terminal identity has changed.";
        return result;
    }
    const std::string key = subscriber_key(
        client_id, subscription.subscription_id);
    auto found = subscribers_.find(key);
    if (!subscription.visible)
    {
        if (found == subscribers_.end())
        {
            found = subscribers_.emplace(key,
                Subscriber{ .client_id = std::string(client_id) })
                        .first;
        }
        auto& subscriber = found->second;
        subscriber.events.clear();
        subscriber.queued_bytes = 0;
        subscriber.needs_resync = true;
        if (!subscriber.suspended)
            ++suspension_count_;
        subscriber.suspended = true;
        result.suspended = true;
        return result;
    }

    const bool was_suspended = found != subscribers_.end()
        && found->second.suspended;
    const bool requires_attach = found == subscribers_.end()
        || found->second.suspended || !subscription.cursor;
    if (requires_attach)
    {
        std::string start_error;
        if (!ensure_runtime_started(start_error))
        {
            result.error_code = "process_start_failed";
            result.error_message = std::move(start_error);
            return result;
        }
        published_process_running_ = true;
        published_exit_code_.reset();
        Subscriber subscriber{ .client_id = std::string(client_id) };
        subscribers_[key] = std::move(subscriber);
        if (controller_client_id_.empty()
            && (preferred_controller_client_id_.empty()
                || preferred_controller_client_id_ == client_id))
        {
            controller_client_id_ = std::string(client_id);
            preferred_controller_client_id_.clear();
            broadcast(make_controller_event());
            subscribers_[key].events.clear();
            subscribers_[key].queued_bytes = 0;
        }
        const auto state = encode_event(snapshot_event(), true);
        if (!state)
        {
            result.error_code = "frame_too_large";
            result.error_message
                = "The terminal snapshot exceeds the transport frame budget.";
            return result;
        }
        if (state->bytes > hard_byte_budget)
        {
            subscribers_[key].needs_resync = true;
            result.more = true;
            return result;
        }
        result.attach = make_attach(state);
        result.payload_bytes = state->bytes;
        result.resync = subscription.cursor.has_value();
        result.oversized = state->bytes > soft_byte_budget;
        snapshot_bytes_ += state->bytes;
        if (was_suspended)
            ++resume_count_;
        return result;
    }

    const RemoteTerminalVersion requested{
        .server_epoch = options_.server_epoch,
        .terminal_id = options_.terminal_id,
        .generation = subscription.cursor->generation,
        .sequence = subscription.cursor->after_sequence,
    };
    return poll_delivery(found->second, requested,
        soft_byte_budget, hard_byte_budget, event_limit, true);
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
    const bool attached = std::ranges::any_of(
        subscribers_, [&client_id](const auto& entry) {
            return entry.second.client_id == client_id;
        });
    if (!attached)
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
    const bool attached = std::ranges::any_of(
        subscribers_, [&client_id](const auto& entry) {
            return entry.second.client_id == client_id;
        });
    if (!attached)
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
    const bool attached = std::ranges::any_of(
        subscribers_, [&client_id](const auto& entry) {
            return entry.second.client_id == client_id;
        });
    if (!attached)
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
    std::erase_if(subscribers_, [&client_id](const auto& entry) {
        return entry.second.client_id == client_id;
    });
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
        || !std::ranges::any_of(
            subscribers_, [&client_id](const auto& entry) {
                return entry.second.client_id == client_id;
            }))
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
    const bool attached = std::ranges::any_of(
        subscribers_, [&client_id](const auto& entry) {
            return entry.second.client_id == client_id;
        });
    if (!attached)
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
    const size_t active_subscribers
        = static_cast<size_t>(std::ranges::count_if(
            subscribers_, [](const auto& entry) {
                return !entry.second.suspended;
            }));
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
        { "active_subscribers", active_subscribers },
        { "suspended_subscribers",
            subscribers_.size() - active_subscribers },
        { "suspensions", suspension_count_ },
        { "resumes", resume_count_ },
        { "avoided_delta_encodes", avoided_delta_encodes_ },
        { "suppressed_clipboard_events",
            suppressed_clipboard_events_ },
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
        if (interval
            > options_.loop_latency_warning_threshold)
        {
            ++loop_latency_warnings_;
            DRAXUL_LOG_WARN(LogCategory::App,
                "Server terminal loop interval was %lld ms (threshold %lld ms)",
                static_cast<long long>(interval.count()),
                static_cast<long long>(
                    options_.loop_latency_warning_threshold.count()));
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
