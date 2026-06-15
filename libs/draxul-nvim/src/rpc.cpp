#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <draxul/log.h>
#include <draxul/mpack_codec.h>
#include <draxul/nvim_rpc.h>
#include <draxul/perf_timing.h>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace draxul
{

struct NvimRpc::Impl
{
    NvimProcess* process_ = nullptr;
    std::thread reader_thread_;
    std::atomic<bool> running_{ false };

    std::mutex write_mutex_;
    std::mutex notif_mutex_;
    std::deque<RpcNotification> notifications_;

    std::mutex response_mutex_;
    std::condition_variable response_cv_;
    std::unordered_map<uint32_t, RpcResponse> responses_;
    std::unordered_set<uint32_t> timed_out_msgids_;

    std::atomic<uint32_t> next_msgid_{ 1 };
    std::atomic<bool> read_failed_{ false };

    // WI 05: diagnostic counter for structurally-valid msgpack packets whose
    // typed fields do not match what the RPC dispatcher expects (e.g. method
    // name at a position that holds an int). Incremented from the reader
    // thread's catch handler; read-only elsewhere.
    std::atomic<uint64_t> malformed_packet_count_{ 0 };

    std::vector<uint8_t> read_buf_;
};

namespace
{
// 5 s is a conservative default: long enough for most nvim operations (plugin init,
// large-workspace indexing), short enough to surface genuine hangs promptly.
static constexpr auto kRpcRequestTimeout = std::chrono::seconds(5);
static constexpr size_t kMaxNotificationQueueDepth = 4096;
static constexpr size_t kNotificationQueueWarnDepth = 512;

// WI 05: how many malformed RPC packets (type-mismatches during dispatch)
// we tolerate before declaring the transport unusable and tearing down the
// reader thread. Each one also logs an error with a hex dump of the packet.
static constexpr uint64_t kMaxMalformedPacketsPerSession = 10;

// Format the first bytes of a decoded packet as a hex dump for logging.
// We do not have the raw on-wire bytes at this point (the mpack decode has
// already consumed them) so we re-encode the MpackValue. This is only done
// on the error path, so the re-encode cost is negligible.
std::string hex_dump_first_bytes(const MpackValue& msg, size_t max_bytes = 64)
{
    std::vector<char> encoded;
    if (!encode_mpack_value(msg, encoded))
        return "<re-encode failed>";
    const size_t n = std::min(encoded.size(), max_bytes);
    std::string result;
    result.reserve(n * 3 + 8);
    static const char* kHex = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i)
    {
        auto byte = static_cast<uint8_t>(encoded[i]);
        if (i > 0)
            result.push_back(' ');
        result.push_back(kHex[(byte >> 4) & 0x0F]);
        result.push_back(kHex[byte & 0x0F]);
    }
    if (encoded.size() > max_bytes)
        result.append(" ...");
    return result;
}
} // namespace

void NvimRpc::set_main_thread_id(std::thread::id id)
{
    assert(main_thread_id_ == std::thread::id{} && "set_main_thread_id() must only be called once");
    main_thread_id_ = id;
}

NvimRpc::NvimRpc()
    : impl_(std::make_unique<Impl>())
{
}

NvimRpc::~NvimRpc()
{
    // Safety net: if the owner forgets to call shutdown() (e.g. NvimHost
    // initialization throws or exits early after initialize() started the
    // reader thread), the joinable std::thread member inside impl_ would
    // otherwise trigger std::terminate on destruction. Always drain here.
    if (impl_ && impl_->reader_thread_.joinable())
    {
        shutdown();
    }
}

bool NvimRpc::initialize(NvimProcess& process, RpcCallbacks callbacks)
{
    PERF_MEASURE();
    impl_->process_ = &process;
    impl_->read_buf_.resize(256 * 1024);
    impl_->read_failed_ = false;
    // Install callbacks BEFORE spawning the reader thread so the thread
    // construction synchronizes-with every use of callbacks_ inside
    // reader_thread_func(). After this point callbacks_ is read-only from
    // every thread, which is safe for concurrent const access.
    callbacks_ = std::move(callbacks);
    impl_->running_ = true;

    impl_->reader_thread_ = std::thread(&NvimRpc::reader_thread_func, this);
    return true;
}

void NvimRpc::close()
{
    PERF_MEASURE();
    bool was_running = impl_->running_.exchange(false);
    if (was_running)
    {
        DRAXUL_LOG_INFO(LogCategory::Rpc, "RPC transport closed");
    }
    impl_->response_cv_.notify_all();
}

void NvimRpc::shutdown()
{
    PERF_MEASURE();
    close();
    if (impl_->reader_thread_.joinable())
    {
        impl_->reader_thread_.join();
    }
}

bool NvimRpc::connection_failed() const
{
    return impl_->read_failed_;
}

namespace
{
// Stringify the mpack `error` field that nvim returns alongside an rpcrequest
// failure. Neovim conventionally encodes errors as `[code, message]` arrays;
// some helpers send a bare string. Anything else falls back to the type name.
std::string stringify_rpc_error(const MpackValue& error)
{
    if (error.is_nil())
        return "unknown rpc error";
    if (error.type() == MpackValue::String)
        return error.as_str();
    if (error.type() == MpackValue::Array)
    {
        const auto& arr = error.as_array();
        if (arr.size() >= 2 && arr[1].type() == MpackValue::String)
            return arr[1].as_str();
        if (!arr.empty() && arr[0].type() == MpackValue::String)
            return arr[0].as_str();
    }
    return "rpc error (non-string payload)";
}
} // namespace

RpcResult NvimRpc::request(const std::string& method, const std::vector<MpackValue>& params)
{
    PERF_MEASURE();
    // Belt-and-suspenders thread guard: assert fires in Debug builds; the runtime
    // check below also fires in Release builds where assert is compiled out.
    assert((main_thread_id_ == std::thread::id{} || std::this_thread::get_id() != main_thread_id_)
        && "NvimRpc::request() must not be called from the main thread");
    if (main_thread_id_ != std::thread::id{} && std::this_thread::get_id() == main_thread_id_)
    {
        DRAXUL_LOG_ERROR(LogCategory::Rpc,
            "NvimRpc::request() called from main thread — would block render loop");
        return RpcResult::err(Error::io("rpc request from main thread"));
    }
    if (!impl_->process_ || !impl_->running_)
    {
        return RpcResult::err(Error::io("rpc transport not running"));
    }

    uint32_t msgid = impl_->next_msgid_++;

    std::vector<char> encoded;
    if (!encode_rpc_request(msgid, method, params, encoded))
    {
        DRAXUL_LOG_ERROR(LogCategory::Rpc, "Failed to encode request %s", method.c_str());
        return RpcResult::err(Error::io("failed to encode rpc request: " + method));
    }

    {
        std::lock_guard<std::mutex> write_lock(impl_->write_mutex_);
        if (!impl_->running_)
            return RpcResult::err(Error::io("rpc transport closed"));

        if (!impl_->process_->write(reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size()))
        {
            DRAXUL_LOG_ERROR(LogCategory::Rpc, "Write failed for request %s", method.c_str());
            impl_->read_failed_ = true;
            impl_->response_cv_.notify_all();
            return RpcResult::err(Error::io("rpc write failed: " + method));
        }
    }

    std::unique_lock<std::mutex> lock(impl_->response_mutex_);
    bool ready = impl_->response_cv_.wait_for(lock, kRpcRequestTimeout, [this, &msgid]() {
        return impl_->responses_.contains(msgid) || !impl_->running_ || impl_->read_failed_ || (impl_->process_ && !impl_->process_->is_running());
    });

    if (!ready || !impl_->responses_.contains(msgid))
    {
        DRAXUL_LOG_WARN(LogCategory::Rpc, "Request timed out or aborted: %s", method.c_str());
        impl_->responses_.erase(msgid);
        impl_->timed_out_msgids_.insert(msgid);
        // Bound the set to prevent unbounded growth over long sessions.
        // Evict the oldest (smallest) msgid when the cap is exceeded,
        // since msgids are monotonically increasing.
        static constexpr size_t kMaxTimedOutIds = 128;
        while (impl_->timed_out_msgids_.size() > kMaxTimedOutIds)
        {
            auto oldest = std::min_element(impl_->timed_out_msgids_.begin(),
                impl_->timed_out_msgids_.end());
            impl_->timed_out_msgids_.erase(oldest);
        }
        return RpcResult::err(Error::io("rpc request timed out or aborted: " + method));
    }

    auto resp = std::move(impl_->responses_[msgid]);
    impl_->responses_.erase(msgid);
    if (!resp.error.is_nil())
    {
        return RpcResult::err(Error::rpc(stringify_rpc_error(resp.error)));
    }
    return RpcResult::ok(std::move(resp.result));
}

void NvimRpc::notify(const std::string& method, const std::vector<MpackValue>& params)
{
    PERF_MEASURE();
    if (!impl_->process_ || !impl_->running_)
    {
        return;
    }

    std::vector<char> encoded;
    if (!encode_rpc_notification(method, params, encoded))
    {
        DRAXUL_LOG_ERROR(LogCategory::Rpc, "Failed to encode notification %s", method.c_str());
        return;
    }

    std::lock_guard<std::mutex> write_lock(impl_->write_mutex_);
    if (!impl_->running_)
        return;

    if (!impl_->process_->write(reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size()))
    {
        DRAXUL_LOG_ERROR(LogCategory::Rpc, "Write failed for notification %s", method.c_str());
        impl_->read_failed_ = true;
        impl_->response_cv_.notify_all();
    }
}

std::vector<RpcNotification> NvimRpc::drain_notifications()
{
    PERF_MEASURE();
    std::lock_guard<std::mutex> lock(impl_->notif_mutex_);
    std::vector<RpcNotification> result(
        std::make_move_iterator(impl_->notifications_.begin()),
        std::make_move_iterator(impl_->notifications_.end()));
    impl_->notifications_.clear();
    return result;
}

size_t NvimRpc::notification_queue_depth() const
{
    std::lock_guard<std::mutex> lock(impl_->notif_mutex_);
    return impl_->notifications_.size();
}

void NvimRpc::dispatch_rpc_response(const std::vector<MpackValue>& msg_array)
{
    PERF_MEASURE();
    int64_t raw_id = msg_array[1].as_int();
    if (raw_id < 0 || raw_id > static_cast<int64_t>(UINT32_MAX))
    {
        DRAXUL_LOG_WARN(LogCategory::Rpc,
            "dispatch_rpc_response: out-of-range msgid %lld; discarding",
            static_cast<long long>(raw_id));
        return;
    }
    uint32_t msgid = static_cast<uint32_t>(raw_id);

    std::lock_guard<std::mutex> lock(impl_->response_mutex_);

    // Discard late responses for requests that already timed out.
    // Do NOT erase the msgid from the set — a duplicate response for
    // the same timed-out request could arrive, and the entry must
    // remain to absorb it.  The set is bounded by eviction in request().
    if (impl_->timed_out_msgids_.count(msgid))
    {
        DRAXUL_LOG_DEBUG(LogCategory::Rpc,
            "Discarding late response for timed-out msgid %u", msgid);
        return;
    }

    RpcResponse resp;
    resp.msgid = msgid;
    resp.error = msg_array[2];
    resp.result = msg_array[3];

    impl_->responses_[resp.msgid] = std::move(resp);
    impl_->response_cv_.notify_all();
}

void NvimRpc::dispatch_rpc_request(const std::vector<MpackValue>& msg_array)
{
    PERF_MEASURE();
    int64_t raw_req_id = msg_array[1].as_int();
    if (raw_req_id < 0 || raw_req_id > static_cast<int64_t>(UINT32_MAX))
    {
        DRAXUL_LOG_WARN(LogCategory::Rpc,
            "dispatch_rpc_request: out-of-range msgid %lld; discarding",
            static_cast<long long>(raw_req_id));
        return;
    }
    auto req_msgid = static_cast<uint32_t>(raw_req_id);
    std::string method = msg_array[2].as_str();
    std::vector<MpackValue> params;
    if (msg_array[3].type() == MpackValue::Array)
        params = msg_array[3].as_array();

    MpackValue result = NvimRpc::make_nil();
    MpackValue error = NvimRpc::make_nil();
    if (callbacks_.on_request)
        result = callbacks_.on_request(method, params);
    else
        error = NvimRpc::make_str("no handler for: " + method);

    reply_to_request(req_msgid, error, result);
}

void NvimRpc::dispatch_rpc_notification(const std::vector<MpackValue>& msg_array)
{
    PERF_MEASURE();
    RpcNotification notif;
    notif.method = msg_array[1].as_str();
    if (msg_array[2].type() == MpackValue::Array)
        notif.params = msg_array[2].as_array();

    {
        std::lock_guard<std::mutex> lock(impl_->notif_mutex_);
        if (impl_->notifications_.size() >= kMaxNotificationQueueDepth)
        {
            // Drop the oldest notification to stay bounded.
            impl_->notifications_.pop_front();
            DRAXUL_LOG_WARN(LogCategory::Rpc,
                "RPC notification queue at capacity (%zu); dropping oldest",
                kMaxNotificationQueueDepth);
        }
        else if (impl_->notifications_.size() == kNotificationQueueWarnDepth)
        {
            DRAXUL_LOG_WARN(LogCategory::Rpc,
                "RPC notification queue depth reached %zu",
                kNotificationQueueWarnDepth);
        }
        impl_->notifications_.push_back(std::move(notif));
    }

    if (callbacks_.on_notification_available)
        callbacks_.on_notification_available();
}

void NvimRpc::dispatch_rpc_message(const MpackValue& msg)
{
    PERF_MEASURE();
    if (msg.type() != MpackValue::Array || msg.as_array().size() < 3)
        return;

    const auto& msg_array = msg.as_array();
    auto type = (int)msg_array[0].as_int();

    if (type == 1 && msg_array.size() >= 4)
        dispatch_rpc_response(msg_array);
    else if (type == 0 && msg_array.size() >= 4)
        dispatch_rpc_request(msg_array);
    else if (type == 2 && msg_array.size() >= 3)
        dispatch_rpc_notification(msg_array);
}

void NvimRpc::reader_thread_func()
{
    PERF_MEASURE();
    std::vector<uint8_t> accum;
    accum.reserve(1024 * 1024);
    size_t read_pos = 0;
    // Counts consecutive single-byte discards from a hard decode failure. Resets on
    // every successful decode. If we discard too many in a row the stream is hopelessly
    // corrupt and we abort the reader rather than silently consume garbage forever.
    int consecutive_invalid_discards = 0;
    constexpr int kMaxConsecutiveInvalidDiscards = 16;

    while (impl_->running_)
    {
        int n = impl_->process_->read(impl_->read_buf_.data(), impl_->read_buf_.size());
        if (n <= 0)
        {
            if (!impl_->running_)
            {
                DRAXUL_LOG_INFO(LogCategory::Rpc, "Reader thread exiting (transport closed)");
                break;
            }
            if (n == 0)
                DRAXUL_LOG_INFO(LogCategory::Rpc, "nvim pipe closed (EOF)");
            else
                DRAXUL_LOG_ERROR(LogCategory::Rpc, "nvim pipe read error (n=%d)", n);
            impl_->read_failed_ = true;
            impl_->running_ = false;
            impl_->response_cv_.notify_all();
            if (callbacks_.on_notification_available)
                callbacks_.on_notification_available();
            break;
        }

        // Compact the buffer if read_pos has advanced past the compaction threshold
        // but there is still unprocessed data remaining. This bounds memory use while
        // avoiding an O(n) shift after every decoded message.
        if (read_pos > 0 && read_pos < accum.size() && read_pos > 65536)
        {
            accum.erase(accum.begin(), accum.begin() + (std::ptrdiff_t)read_pos);
            read_pos = 0;
        }

        accum.insert(accum.end(), impl_->read_buf_.begin(), impl_->read_buf_.begin() + n);

        // WI 08: Guard against unbounded accumulation. A corrupt stream that
        // sends valid-looking msgpack headers claiming a large payload but never
        // delivers the body bytes would grow accum indefinitely (read_pos stays 0
        // so the compaction guard above never fires). Cap it hard.
        constexpr size_t kMaxAccumBytes = 256ULL * 1024 * 1024; // 256 MB
        if (accum.size() > kMaxAccumBytes)
        {
            DRAXUL_LOG_ERROR(LogCategory::Rpc,
                "RPC accumulation buffer exceeded %zu bytes; stream is corrupt, aborting reader",
                kMaxAccumBytes);
            impl_->read_failed_ = true;
            impl_->running_ = false;
            impl_->response_cv_.notify_all();
            if (callbacks_.on_notification_available)
                callbacks_.on_notification_available();
            return;
        }

        while (read_pos < accum.size())
        {
            MpackValue msg;
            size_t consumed = 0;
            bool hard_error = false;
            std::span<const uint8_t> remaining(accum.data() + read_pos, accum.size() - read_pos);
            if (!decode_mpack_value(remaining, msg, &consumed, &hard_error) || consumed == 0)
            {
                if (!hard_error)
                {
                    // Likely a truncated value: wait for more bytes from the next read.
                    break;
                }
                // Hard structural failure on the byte at read_pos (e.g. reserved 0xC1).
                // Skip it so the reader can recover instead of retrying the same bad
                // prefix forever while accum grows unboundedly.
                DRAXUL_LOG_ERROR(LogCategory::Rpc,
                    "Invalid msgpack prefix byte 0x%02X at offset %zu; discarding 1 byte",
                    (unsigned)remaining[0], read_pos);
                read_pos += 1;
                if (++consecutive_invalid_discards >= kMaxConsecutiveInvalidDiscards)
                {
                    DRAXUL_LOG_ERROR(LogCategory::Rpc,
                        "Aborting reader after %d consecutive invalid bytes; stream is corrupt",
                        consecutive_invalid_discards);
                    impl_->read_failed_ = true;
                    impl_->running_ = false;
                    impl_->response_cv_.notify_all();
                    if (callbacks_.on_notification_available)
                        callbacks_.on_notification_available();
                    return;
                }
                continue;
            }

            consecutive_invalid_discards = 0;
            read_pos += consumed;

            // Reset when the buffer is fully drained to avoid unbounded growth.
            if (read_pos == accum.size())
            {
                accum.clear();
                read_pos = 0;
            }

            // WI 05: dispatch_rpc_message() calls typed accessors (as_int,
            // as_str) on fixed positions. A structurally-valid msgpack packet
            // with the wrong element types throws std::bad_variant_access. We
            // must NOT let that propagate through the reader thread into
            // std::terminate: log, count, and continue so the next packet can
            // be decoded. Only if we see a flood of malformed packets in one
            // session do we give up and tear down the transport.
            try
            {
                dispatch_rpc_message(msg);
            }
            catch (const std::exception& e)
            {
                const uint64_t count = ++impl_->malformed_packet_count_;
                DRAXUL_LOG_ERROR(LogCategory::Rpc,
                    "Malformed RPC packet (#%llu): %s; hex=%s",
                    static_cast<unsigned long long>(count),
                    e.what(),
                    hex_dump_first_bytes(msg).c_str());
                if (count >= kMaxMalformedPacketsPerSession)
                {
                    DRAXUL_LOG_ERROR(LogCategory::Rpc,
                        "Aborting reader after %llu malformed RPC packets; "
                        "transport is unusable",
                        static_cast<unsigned long long>(count));
                    impl_->read_failed_ = true;
                    impl_->running_ = false;
                    impl_->response_cv_.notify_all();
                    if (callbacks_.on_notification_available)
                        callbacks_.on_notification_available();
                    return;
                }
                // Continue the inner loop so we keep draining accum and
                // processing subsequent (valid) packets on the same transport.
            }
        }
    }
}

void NvimRpc::reply_to_request(uint32_t msgid, const MpackValue& error, const MpackValue& result)
{
    PERF_MEASURE();
    std::vector<char> encoded;
    if (!encode_rpc_response(msgid, error, result, encoded))
    {
        DRAXUL_LOG_ERROR(LogCategory::Rpc, "Failed to encode response for msgid %u", msgid);
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->write_mutex_);
    if (impl_->process_
        && !impl_->process_->write(reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size()))
    {
        DRAXUL_LOG_ERROR(LogCategory::Rpc, "Write failed for reply to msgid %u", msgid);
        impl_->read_failed_ = true;
        impl_->response_cv_.notify_all();
        if (callbacks_.on_notification_available)
            callbacks_.on_notification_available();
    }
}

MpackValue NvimRpc::make_int(int64_t v)
{
    MpackValue val;
    val.storage = v;
    return val;
}
MpackValue NvimRpc::make_uint(uint64_t v)
{
    MpackValue val;
    val.storage = v;
    return val;
}
MpackValue NvimRpc::make_str(const std::string& v)
{
    MpackValue val;
    val.storage = v;
    return val;
}
MpackValue NvimRpc::make_bool(bool v)
{
    MpackValue val;
    val.storage = v;
    return val;
}
MpackValue NvimRpc::make_array(std::vector<MpackValue> v)
{
    MpackValue val;
    val.storage = std::move(v);
    return val;
}
MpackValue NvimRpc::make_map(std::vector<std::pair<MpackValue, MpackValue>> v)
{
    MpackValue val;
    val.storage = std::move(v);
    return val;
}
MpackValue NvimRpc::make_nil()
{
    return MpackValue{};
}

} // namespace draxul
