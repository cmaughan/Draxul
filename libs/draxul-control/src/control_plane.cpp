#include <draxul/control_plane.h>

#include "control_codec.h"
#include "control_deadline.h"
#include "control_metadata.h"
#include "control_transport.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace draxul
{

namespace
{

constexpr auto kIoTimeout = std::chrono::seconds(5);

bool retries_with_fresh_metadata(control_detail::TransportStage stage)
{
    return stage == control_detail::TransportStage::ConnectWait
        || stage == control_detail::TransportStage::Connect;
}

bool invalidates_metadata_cache(control_detail::TransportStage stage)
{
    return stage != control_detail::TransportStage::EndpointPrepare
        && stage != control_detail::TransportStage::EndpointConfigure;
}

ControlClientResult public_transport_error(
    const control_detail::TransportError& error)
{
    using control_detail::FailureClass;
    switch (error.classification)
    {
    case FailureClass::DeadlineExceeded:
        return { false, nullptr, "deadline_exceeded",
            "The Draxul control request exceeded its deadline." };
    case FailureClass::EndpointUnavailable:
        return { false, nullptr, "endpoint_unavailable",
            error.message.empty()
                ? "The Draxul Session control endpoint is unavailable."
                : error.message };
    case FailureClass::IoError:
        break;
    }
    return { false, nullptr, "io_error", "Control request failed." };
}

} // namespace

ControlMethodResult ControlMethodResult::success(nlohmann::json result)
{
    return { true, std::move(result), {}, {} };
}

ControlMethodResult ControlMethodResult::error(
    std::string code, std::string message)
{
    return { false, nullptr, std::move(code), std::move(message) };
}

std::filesystem::path control_runtime_directory(
    const std::filesystem::path& config_directory)
{
    return config_directory / "runtime";
}

std::filesystem::path control_metadata_path(
    const std::filesystem::path& runtime_directory,
    std::string_view session_id)
{
    return runtime_directory
        / (control_detail::session_key(session_id) + ".control.json");
}

class ControlServer::Impl
{
public:
    struct Pending
    {
        ControlRequest request;
        std::promise<ControlMethodResult> response;
        std::atomic<bool> completed = false;
        std::atomic<bool> cancelled = false;

        bool complete(ControlMethodResult result)
        {
            bool expected = false;
            if (!completed.compare_exchange_strong(expected, true))
                return false;
            response.set_value(std::move(result));
            return true;
        }
    };

    bool start(std::string new_session_id,
        std::filesystem::path new_runtime_directory,
        std::function<void()> wake,
        std::string* error,
        nlohmann::json metadata_extra);
    void stop();
    void run(std::stop_token stop_token);
    void process_pending(const Handler& handler);
    ControlMethodResult dispatch(ControlRequest request);
    void complete_pending(const std::shared_ptr<Pending>& pending,
        ControlMethodResult result);
    void report_startup(std::string result);
    std::string handle_frame(std::optional<std::string> bytes);

    std::string session_id;
    std::filesystem::path runtime_directory;
    std::filesystem::path metadata;
    std::string endpoint;
    std::string token;
    std::function<void()> wake_main_thread;
    std::unique_ptr<control_detail::ServerTransport> transport;
    std::jthread thread;
    std::atomic<bool> active = false;
    std::atomic<bool> stopping = false;
    std::mutex queue_mutex;
    std::deque<std::shared_ptr<Pending>> queue;
    std::unordered_set<std::shared_ptr<Pending>> pending_requests;
    std::mutex startup_mutex;
    std::condition_variable startup_changed;
    std::optional<std::string> startup_result;
    bool owns_endpoint = false;
    std::atomic<bool> endpoint_in_use = false;
};

void ControlServer::Impl::report_startup(std::string result)
{
    {
        std::lock_guard<std::mutex> guard(startup_mutex);
        if (startup_result)
            return;
        startup_result = std::move(result);
    }
    startup_changed.notify_all();
}

bool ControlServer::Impl::start(std::string new_session_id,
    std::filesystem::path new_runtime_directory,
    std::function<void()> wake,
    std::string* error,
    nlohmann::json metadata_extra)
{
    if (active)
    {
        if (error)
            *error = "Control server is already running.";
        return false;
    }
    session_id = std::move(new_session_id);
    runtime_directory = std::move(new_runtime_directory);
    wake_main_thread = std::move(wake);
    metadata = control_metadata_path(runtime_directory, session_id);
    token = control_detail::random_token();

    std::error_code dir_error;
    std::filesystem::create_directories(runtime_directory, dir_error);
    if (dir_error)
    {
        if (error)
            *error = "Unable to create control runtime directory.";
        return false;
    }
    auto secure_status
        = control_detail::secure_runtime_directory(runtime_directory);
    if (!secure_status.ok)
    {
        if (error)
            *error = std::move(secure_status.error.message);
        return false;
    }

    endpoint_in_use = false;
    owns_endpoint = false;
    stopping = false;
    transport = control_detail::make_server_transport();
    auto prepare_status = transport->prepare(session_id, runtime_directory);
    if (!prepare_status.ok)
    {
        endpoint_in_use = transport->endpoint_in_use();
        if (error)
            *error = std::move(prepare_status.error.message);
        transport->cleanup();
        return false;
    }
    endpoint = transport->endpoint();

    active = true;
    {
        std::lock_guard<std::mutex> guard(startup_mutex);
        startup_result.reset();
    }
    thread = std::jthread(
        [this](std::stop_token stop_token) { run(stop_token); });

    std::string startup_error;
    {
        std::unique_lock<std::mutex> lock(startup_mutex);
        startup_changed.wait(
            lock, [this] { return startup_result.has_value(); });
        startup_error = *startup_result;
    }
    if (!startup_error.empty())
    {
        endpoint_in_use = transport->endpoint_in_use();
        if (error)
            *error = startup_error;
        stop();
        return false;
    }
    owns_endpoint = true;

    if (!metadata_extra.is_object())
        metadata_extra = nlohmann::json::object();
    metadata_extra["version"] = kControlProtocolVersion;
    metadata_extra["session_id"] = session_id;
    metadata_extra["endpoint"] = endpoint;
    metadata_extra["token"] = token;
    const std::string metadata_bytes = metadata_extra.dump();
    auto write_status = control_detail::write_current_user_metadata(
        metadata, metadata_bytes);
    if (!write_status.ok)
    {
        if (error)
            *error = std::move(write_status.error.message);
        stop();
        return false;
    }
    return true;
}

void ControlServer::Impl::stop()
{
    stopping = true;
    std::vector<std::shared_ptr<Pending>> pending;
    {
        std::lock_guard lock(queue_mutex);
        queue.clear();
        pending.reserve(pending_requests.size());
        for (const auto& request : pending_requests)
            pending.push_back(request);
        pending_requests.clear();
    }
    for (const auto& request : pending)
    {
        request->complete(ControlMethodResult::error(
            "server_stopping", "Control server is stopping."));
    }

    if (thread.joinable())
    {
        thread.request_stop();
        thread.join();
    }
    active = false;
    if (owns_endpoint)
    {
        std::error_code ignored;
        std::filesystem::remove(metadata, ignored);
        owns_endpoint = false;
    }
    if (transport)
        transport->cleanup();
}

ControlMethodResult ControlServer::Impl::dispatch(ControlRequest request)
{
    if (stopping)
    {
        return ControlMethodResult::error(
            "server_stopping", "Control server is stopping.");
    }
    auto pending = std::make_shared<Pending>();
    pending->request = std::move(request);
    if (std::chrono::steady_clock::now()
        >= pending->request.expires_at)
    {
        pending->cancelled = true;
        return ControlMethodResult::error(
            "deadline_exceeded",
            "The control request expired before dispatch.");
    }
    auto response = pending->response.get_future();
    {
        std::lock_guard lock(queue_mutex);
        if (stopping)
        {
            return ControlMethodResult::error(
                "server_stopping", "Control server is stopping.");
        }
        queue.push_back(pending);
        pending_requests.insert(pending);
    }
    if (wake_main_thread)
        wake_main_thread();
    auto wait_budget
        = std::chrono::duration_cast<std::chrono::milliseconds>(kIoTimeout);
    bool waiting_to_request_deadline = false;
    if (pending->request.expires_at
        != std::chrono::steady_clock::time_point::max())
    {
        const auto request_budget
            = std::chrono::duration_cast<std::chrono::milliseconds>(
                pending->request.expires_at
                - std::chrono::steady_clock::now());
        waiting_to_request_deadline = request_budget <= wait_budget;
        wait_budget = std::min(wait_budget, request_budget);
    }
    if (wait_budget <= std::chrono::milliseconds::zero()
        || response.wait_for(wait_budget) != std::future_status::ready)
    {
        pending->cancelled = true;
        auto timeout = ControlMethodResult::error(
            waiting_to_request_deadline
                ? "deadline_exceeded"
                : "main_thread_timeout",
            waiting_to_request_deadline
                ? "The control request exceeded its deadline."
                : "Draxul did not process the request in time.");
        complete_pending(pending, timeout);
        return timeout;
    }
    return response.get();
}

void ControlServer::Impl::complete_pending(
    const std::shared_ptr<Pending>& pending,
    ControlMethodResult result)
{
    pending->complete(std::move(result));
    std::lock_guard lock(queue_mutex);
    pending_requests.erase(pending);
}

void ControlServer::Impl::process_pending(const Handler& handler)
{
    std::deque<std::shared_ptr<Pending>> pending;
    {
        std::lock_guard lock(queue_mutex);
        pending.swap(queue);
    }
    for (auto& item : pending)
    {
        if (item->cancelled
            || std::chrono::steady_clock::now()
                >= item->request.expires_at)
        {
            item->cancelled = true;
            complete_pending(item,
                ControlMethodResult::error("deadline_exceeded",
                    "The control request exceeded its deadline."));
            continue;
        }
        if (stopping || item->completed)
        {
            complete_pending(item,
                ControlMethodResult::error("server_stopping",
                    "Control server is stopping."));
            continue;
        }
        ControlMethodResult result;
        try
        {
            result = handler(item->request);
        }
        catch (const std::exception&)
        {
            result = ControlMethodResult::error(
                "internal_error",
                "The control request failed internally.");
        }
        complete_pending(item, std::move(result));
    }
}

std::string ControlServer::Impl::handle_frame(
    std::optional<std::string> bytes)
{
    ControlRequest request;
    ControlMethodResult result;
    if (!bytes)
    {
        result = ControlMethodResult::error(
            "invalid_frame", "Invalid control frame.");
    }
    else
    {
        result = control_detail::parse_request(*bytes, token, request);
        if (result.ok)
            result = dispatch(request);
    }
    return control_detail::dump_wire_json(
        control_detail::response_json(request.id, result));
}

void ControlServer::Impl::run(std::stop_token stop_token)
{
    transport->run(stop_token,
        [this](std::optional<std::string> bytes) {
            return handle_frame(std::move(bytes));
        },
        [this](std::string result) {
            report_startup(std::move(result));
        });
    report_startup({});
    active = false;
}

ControlServer::ControlServer()
    : impl_(std::make_unique<Impl>())
{
}

ControlServer::~ControlServer()
{
    stop();
}

bool ControlServer::start(std::string session_id,
    std::filesystem::path runtime_directory,
    std::function<void()> wake_main_thread,
    std::string* error,
    nlohmann::json metadata_extra)
{
    return impl_->start(std::move(session_id),
        std::move(runtime_directory), std::move(wake_main_thread), error,
        std::move(metadata_extra));
}

std::string namespaced_control_id(std::string_view base_id,
    const std::filesystem::path& runtime_directory)
{
    std::ostringstream id;
    id << base_id << '-' << std::hex
       << control_detail::fnv1a(
              control_detail::normalized_runtime_key(runtime_directory));
    return id.str();
}

void ControlServer::stop()
{
    impl_->stop();
}

bool ControlServer::running() const
{
    return impl_->active;
}

bool ControlServer::endpoint_in_use() const
{
    return impl_->endpoint_in_use;
}

void ControlServer::abandon_endpoint()
{
    impl_->owns_endpoint = false;
    if (impl_->transport)
        impl_->transport->abandon_endpoint();
}

uint32_t ControlServer::take_listener_error()
{
    return impl_->transport
        ? impl_->transport->take_listener_error()
        : 0;
}

void ControlServer::process_pending(const Handler& handler)
{
    impl_->process_pending(handler);
}

const std::string& ControlServer::endpoint() const
{
    return impl_->endpoint;
}

const std::filesystem::path& ControlServer::metadata_path() const
{
    return impl_->metadata;
}

ControlClientResult ControlClient::request(std::string_view session_id,
    const std::filesystem::path& runtime_directory,
    std::string_view method,
    nlohmann::json params,
    ControlRequestOptions options)
{
    const auto started_at = std::chrono::steady_clock::now();
    const auto timeout = std::max(
        std::chrono::milliseconds(1), options.timeout);
    const auto deadline = started_at + timeout;
    const auto metadata_path
        = control_metadata_path(runtime_directory, session_id);
    if (options.refresh_metadata)
        control_detail::invalidate_cached_metadata(metadata_path);
    std::string endpoint;
    std::string token;
    std::string metadata_error;
    if (!control_detail::read_cached_metadata(
            metadata_path, endpoint, token, metadata_error))
    {
        return { false, nullptr, "endpoint_unavailable",
            std::move(metadata_error) };
    }

    const auto wire_timeout = control_detail::remaining_time(deadline);
    if (wire_timeout.count() == 0)
    {
        return { false, nullptr, "deadline_exceeded",
            "The Draxul control request exceeded its deadline." };
    }
    const std::string id
        = control_detail::random_token().substr(0, 16);
    const std::string request_bytes = control_detail::encode_request(
        id, token, method, params, wire_timeout);

    auto exchange = control_detail::client_exchange(
        endpoint, request_bytes, deadline);
    if (!exchange.ok)
    {
        if (invalidates_metadata_cache(exchange.error.stage))
            control_detail::invalidate_cached_metadata(metadata_path);
        if (retries_with_fresh_metadata(exchange.error.stage)
            && !options.refresh_metadata)
        {
            return request(session_id, runtime_directory, method,
                std::move(params),
                {
                    .timeout = control_detail::remaining_time(deadline),
                    .refresh_metadata = true,
                });
        }
        return public_transport_error(exchange.error);
    }

    auto result = control_detail::parse_response(
        exchange.response_bytes, id);
    if (result.error_code == "authentication_failed"
        && !options.refresh_metadata)
    {
        control_detail::invalidate_cached_metadata(metadata_path);
        return request(session_id, runtime_directory, method,
            std::move(params),
            {
                .timeout = control_detail::remaining_time(deadline),
                .refresh_metadata = true,
            });
    }
    return result;
}

} // namespace draxul
