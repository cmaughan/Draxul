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
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace draxul
{

namespace
{

constexpr auto kIoTimeout = std::chrono::seconds(5);
// Reserve the final public bucket for overflow so untrusted method names and
// native error values cannot grow the diagnostics snapshot without bound.
constexpr size_t kMaxDistinctMetricMethods = 63;
constexpr size_t kMaxDistinctMetricFailures = 63;

uint64_t elapsed_us(std::chrono::steady_clock::time_point started_at)
{
    return static_cast<uint64_t>(std::max<int64_t>(0,
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started_at)
            .count()));
}

std::string_view transport_stage_name(control_detail::TransportStage stage)
{
    using control_detail::TransportStage;
    switch (stage)
    {
    case TransportStage::RuntimeSecurity: return "runtime_security";
    case TransportStage::MetadataCreate: return "metadata_create";
    case TransportStage::MetadataWrite: return "metadata_write";
    case TransportStage::MetadataFlush: return "metadata_flush";
    case TransportStage::MetadataReplace: return "metadata_replace";
    case TransportStage::MetadataDirectoryFlush: return "metadata_directory_flush";
    case TransportStage::MetadataRead: return "metadata_read";
    case TransportStage::MetadataParse: return "metadata_parse";
    case TransportStage::EndpointPrepare: return "endpoint_prepare";
    case TransportStage::EndpointClaim: return "endpoint_claim";
    case TransportStage::EndpointConfigure: return "endpoint_configure";
    case TransportStage::ConnectWait: return "connect_wait";
    case TransportStage::Connect: return "connect";
    case TransportStage::ReadPrefix: return "read_prefix";
    case TransportStage::ReadPayload: return "read_payload";
    case TransportStage::WritePrefix: return "write_prefix";
    case TransportStage::WritePayload: return "write_payload";
    case TransportStage::Flush: return "flush";
    case TransportStage::Cancel: return "cancel";
    case TransportStage::ListenerCreate: return "listener_create";
    case TransportStage::ListenerWait: return "listener_wait";
    case TransportStage::Accept: return "accept";
    }
    return "unknown";
}

std::string_view transport_operation_name(control_detail::TransportStage stage)
{
    using control_detail::TransportStage;
    switch (stage)
    {
    case TransportStage::RuntimeSecurity: return "security";
    case TransportStage::MetadataCreate:
    case TransportStage::MetadataWrite:
    case TransportStage::MetadataFlush:
    case TransportStage::MetadataReplace:
    case TransportStage::MetadataDirectoryFlush:
    case TransportStage::MetadataRead:
    case TransportStage::MetadataParse: return "metadata";
    case TransportStage::EndpointPrepare:
    case TransportStage::EndpointClaim:
    case TransportStage::EndpointConfigure: return "endpoint";
    case TransportStage::ConnectWait:
    case TransportStage::Connect: return "connect";
    case TransportStage::ReadPrefix:
    case TransportStage::ReadPayload: return "read";
    case TransportStage::WritePrefix:
    case TransportStage::WritePayload:
    case TransportStage::Flush: return "write";
    case TransportStage::Cancel: return "cancel";
    case TransportStage::ListenerCreate:
    case TransportStage::ListenerWait:
    case TransportStage::Accept: return "listener";
    }
    return "unknown";
}

std::string_view native_domain_name(control_detail::NativeDomain domain)
{
    using control_detail::NativeDomain;
    switch (domain)
    {
    case NativeDomain::None: return "none";
    case NativeDomain::Win32: return "win32";
    case NativeDomain::Posix: return "posix";
    }
    return "none";
}

std::string_view failure_class_name(
    control_detail::FailureClass classification)
{
    using control_detail::FailureClass;
    switch (classification)
    {
    case FailureClass::EndpointUnavailable: return "endpoint_unavailable";
    case FailureClass::IoError: return "io_error";
    case FailureClass::DeadlineExceeded: return "deadline_exceeded";
    }
    return "io_error";
}

void add_timing(ControlTimingMetrics& timing, uint64_t duration_us)
{
    ++timing.samples;
    timing.total_us += duration_us;
    timing.max_us = std::max(timing.max_us, duration_us);
}

void add_transport_failure(
    std::vector<ControlTransportFailureMetrics>& failures,
    const control_detail::TransportError& error)
{
    const std::string operation(transport_operation_name(error.stage));
    const std::string stage(transport_stage_name(error.stage));
    const std::string domain(native_domain_name(error.domain));
    const std::string classification(
        failure_class_name(error.classification));
    const auto found = std::ranges::find_if(failures,
        [&](const ControlTransportFailureMetrics& existing) {
            return existing.operation == operation
                && existing.stage == stage
                && existing.native_domain == domain
                && existing.classification == classification
                && existing.native_code == error.native_code;
        });
    if (found != failures.end())
    {
        ++found->count;
        return;
    }
    if (failures.size() >= kMaxDistinctMetricFailures)
    {
        const auto overflow = std::ranges::find_if(failures,
            [](const ControlTransportFailureMetrics& existing) {
                return existing.stage == "other";
            });
        if (overflow != failures.end())
        {
            ++overflow->count;
            return;
        }
        failures.push_back(
            { "other", "other", "none", "io_error", 0, 1 });
        return;
    }
    failures.push_back({ operation, stage, domain, classification,
        error.native_code, 1 });
}

struct ClientMetricsState
{
    std::mutex mutex;
    ControlClientMetricsSnapshot snapshot;
};

ClientMetricsState& client_metrics_state()
{
    static ClientMetricsState state;
    return state;
}

void record_client_failure(const control_detail::TransportError& error)
{
    auto& state = client_metrics_state();
    std::lock_guard guard(state.mutex);
    add_transport_failure(state.snapshot.failures, error);
}

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
        std::chrono::steady_clock::time_point queued_at{};

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
    control_detail::ServerFrameResponse handle_frame(
        std::optional<std::string> bytes);
    void reset_metrics();
    void connection_opened();
    void connection_closed();
    void record_transport_failure(
        const control_detail::TransportError& error);
    void record_request(std::string_view method);
    void record_queue_time(std::string_view method, uint64_t duration_us);
    void record_dispatch_time(std::string_view method, uint64_t duration_us);
    void record_response(
        std::string_view method, bool failed, uint64_t duration_us);
    ControlServerMetricsSnapshot metrics_snapshot() const;

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
    mutable std::mutex metrics_mutex;
    ControlServerMetricsSnapshot metrics;
    std::unordered_map<std::string, ControlMethodMetrics> method_metrics;
};

void ControlServer::Impl::reset_metrics()
{
    std::lock_guard guard(metrics_mutex);
    metrics = {};
    method_metrics.clear();
}

void ControlServer::Impl::connection_opened()
{
    std::lock_guard guard(metrics_mutex);
    ++metrics.accepted_connections;
    ++metrics.active_connections;
    metrics.peak_connections = std::max(
        metrics.peak_connections, metrics.active_connections);
}

void ControlServer::Impl::connection_closed()
{
    std::lock_guard guard(metrics_mutex);
    if (metrics.active_connections > 0)
        --metrics.active_connections;
}

void ControlServer::Impl::record_transport_failure(
    const control_detail::TransportError& error)
{
    std::lock_guard guard(metrics_mutex);
    add_transport_failure(metrics.transport_failures, error);
}

void ControlServer::Impl::record_request(std::string_view method)
{
    std::lock_guard guard(metrics_mutex);
    ++metrics.requests;
    std::string key(method);
    if (!method_metrics.contains(key)
        && method_metrics.size() >= kMaxDistinctMetricMethods)
    {
        key = "other";
    }
    auto& entry = method_metrics[key];
    entry.method = key;
    ++entry.requests;
}

void ControlServer::Impl::record_queue_time(
    std::string_view method, uint64_t duration_us)
{
    std::lock_guard guard(metrics_mutex);
    const auto found = method_metrics.find(std::string(method));
    auto& entry = found != method_metrics.end()
        ? found->second
        : method_metrics["other"];
    if (entry.method.empty())
        entry.method = "other";
    add_timing(entry.queue_time, duration_us);
}

void ControlServer::Impl::record_dispatch_time(
    std::string_view method, uint64_t duration_us)
{
    std::lock_guard guard(metrics_mutex);
    const auto found = method_metrics.find(std::string(method));
    auto& entry = found != method_metrics.end()
        ? found->second
        : method_metrics["other"];
    if (entry.method.empty())
        entry.method = "other";
    add_timing(entry.dispatch_time, duration_us);
}

void ControlServer::Impl::record_response(
    std::string_view method, bool failed, uint64_t duration_us)
{
    std::lock_guard guard(metrics_mutex);
    const auto found = method_metrics.find(std::string(method));
    auto& entry = found != method_metrics.end()
        ? found->second
        : method_metrics["other"];
    if (entry.method.empty())
        entry.method = "other";
    if (failed)
    {
        ++metrics.failed_requests;
        ++entry.failures;
    }
    add_timing(entry.response_time, duration_us);
}

ControlServerMetricsSnapshot ControlServer::Impl::metrics_snapshot() const
{
    std::lock_guard guard(metrics_mutex);
    ControlServerMetricsSnapshot snapshot = metrics;
    snapshot.methods.clear();
    snapshot.methods.reserve(method_metrics.size());
    for (const auto& [method, entry] : method_metrics)
        snapshot.methods.push_back(entry);
    std::ranges::sort(snapshot.methods,
        {}, &ControlMethodMetrics::method);
    std::ranges::sort(snapshot.transport_failures,
        [](const auto& lhs, const auto& rhs) {
            return std::tie(lhs.operation, lhs.stage,
                       lhs.native_domain, lhs.classification,
                       lhs.native_code)
                < std::tie(rhs.operation, rhs.stage,
                    rhs.native_domain, rhs.classification,
                    rhs.native_code);
        });
    return snapshot;
}

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
    reset_metrics();
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
        record_transport_failure(secure_status.error);
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
        record_transport_failure(prepare_status.error);
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
        record_transport_failure(write_status.error);
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
    record_request(request.method);
    if (stopping)
    {
        return ControlMethodResult::error(
            "server_stopping", "Control server is stopping.");
    }
    auto pending = std::make_shared<Pending>();
    pending->request = std::move(request);
    pending->queued_at = std::chrono::steady_clock::now();
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
        record_queue_time(item->request.method,
            elapsed_us(item->queued_at));
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
        const auto dispatch_started_at
            = std::chrono::steady_clock::now();
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
        record_dispatch_time(item->request.method,
            elapsed_us(dispatch_started_at));
        complete_pending(item, std::move(result));
    }
}

control_detail::ServerFrameResponse ControlServer::Impl::handle_frame(
    std::optional<std::string> bytes)
{
    const auto started_at = std::chrono::steady_clock::now();
    ControlRequest request;
    ControlMethodResult result;
    if (!bytes)
    {
        std::lock_guard guard(metrics_mutex);
        ++metrics.invalid_frames;
        result = ControlMethodResult::error(
            "invalid_frame", "Invalid control frame.");
    }
    else
    {
        result = control_detail::parse_request(*bytes, token, request);
        if (result.ok)
            result = dispatch(request);
        else
        {
            std::lock_guard guard(metrics_mutex);
            ++metrics.invalid_frames;
        }
    }
    return {
        .bytes = control_detail::dump_wire_json(
            control_detail::response_json(request.id, result)),
        .method = request.method,
        .failed = !result.ok,
        .started_at = started_at,
    };
}

void ControlServer::Impl::run(std::stop_token stop_token)
{
    transport->run(stop_token,
        [this](std::optional<std::string> bytes) {
            return handle_frame(std::move(bytes));
        },
        [this](std::string result) {
            report_startup(std::move(result));
        },
        {
            .connection_opened = [this] { connection_opened(); },
            .connection_closed = [this] { connection_closed(); },
            .transport_failed = [this](const auto& error) {
                record_transport_failure(error);
            },
            .response_completed = [this](std::string_view method,
                                      bool failed, uint64_t duration_us) {
                record_response(method, failed, duration_us);
            },
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
    const uint32_t error = impl_->transport
        ? impl_->transport->take_listener_error()
        : 0;
    if (error != 0)
    {
        impl_->record_transport_failure({
            .stage = control_detail::TransportStage::ListenerCreate,
#ifdef _WIN32
            .domain = control_detail::NativeDomain::Win32,
#else
            .domain = control_detail::NativeDomain::Posix,
#endif
            .native_code = error,
            .classification = control_detail::FailureClass::IoError,
            .message = "Control listener recreation failed.",
        });
    }
    return error;
}

ControlServerMetricsSnapshot ControlServer::metrics_snapshot() const
{
    return impl_->metrics_snapshot();
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
    {
        auto& state = client_metrics_state();
        std::lock_guard guard(state.mutex);
        ++state.snapshot.requests;
        if (options.refresh_metadata)
            ++state.snapshot.metadata_refreshes;
    }
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
    const auto metadata_status = control_detail::read_cached_metadata(
        metadata_path, endpoint, token, metadata_error);
    if (!metadata_status.ok)
    {
        record_client_failure(metadata_status.error);
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

    {
        auto& state = client_metrics_state();
        std::lock_guard guard(state.mutex);
        ++state.snapshot.connection_attempts;
    }
    auto exchange = control_detail::client_exchange(
        endpoint, request_bytes, deadline);
    if (!exchange.ok)
    {
        record_client_failure(exchange.error);
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
    {
        auto& state = client_metrics_state();
        std::lock_guard guard(state.mutex);
        ++state.snapshot.successful_exchanges;
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

ControlClientMetricsSnapshot ControlClient::metrics_snapshot()
{
    auto& state = client_metrics_state();
    std::lock_guard guard(state.mutex);
    auto snapshot = state.snapshot;
    std::ranges::sort(snapshot.failures,
        [](const auto& lhs, const auto& rhs) {
            return std::tie(lhs.operation, lhs.stage,
                       lhs.native_domain, lhs.classification,
                       lhs.native_code)
                < std::tie(rhs.operation, rhs.stage,
                    rhs.native_domain, rhs.classification,
                    rhs.native_code);
        });
    return snapshot;
}

} // namespace draxul
