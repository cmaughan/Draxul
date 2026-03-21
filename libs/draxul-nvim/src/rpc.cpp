#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <draxul/log.h>
#include <draxul/mpack_codec.h>
#include <draxul/nvim_rpc.h>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace draxul
{

struct NvimRpc::Impl
{
    NvimProcess* process_ = nullptr;
    std::thread reader_thread_;
    std::atomic<bool> running_{ false };

    std::mutex write_mutex_;
    std::mutex notif_mutex_;
    std::vector<RpcNotification> notifications_;

    std::mutex response_mutex_;
    std::condition_variable response_cv_;
    std::unordered_map<uint32_t, RpcResponse> responses_;

    std::atomic<uint32_t> next_msgid_{ 1 };
    std::atomic<bool> read_failed_{ false };

    std::vector<uint8_t> read_buf_;
};

namespace
{
constexpr auto kRequestTimeout = std::chrono::seconds(5);
static constexpr size_t kMaxNotificationQueueDepth = 4096;
static constexpr size_t kNotificationQueueWarnDepth = 512;
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
NvimRpc::~NvimRpc() = default;

bool NvimRpc::initialize(NvimProcess& process)
{
    impl_->process_ = &process;
    impl_->read_buf_.resize(256 * 1024);
    impl_->read_failed_ = false;
    impl_->running_ = true;

    impl_->reader_thread_ = std::thread(&NvimRpc::reader_thread_func, this);
    return true;
}

void NvimRpc::close()
{
    bool was_running = impl_->running_.exchange(false);
    if (was_running)
    {
        DRAXUL_LOG_INFO(LogCategory::Rpc, "RPC transport closed");
    }
    impl_->response_cv_.notify_all();
}

void NvimRpc::shutdown()
{
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

RpcResult NvimRpc::request(const std::string& method, const std::vector<MpackValue>& params)
{
    // Belt-and-suspenders thread guard: assert fires in Debug builds; the runtime
    // check below also fires in Release builds where assert is compiled out.
    assert((main_thread_id_ == std::thread::id{} || std::this_thread::get_id() != main_thread_id_)
        && "NvimRpc::request() must not be called from the main thread");
    if (main_thread_id_ != std::thread::id{} && std::this_thread::get_id() == main_thread_id_)
    {
        DRAXUL_LOG_ERROR(LogCategory::Rpc,
            "NvimRpc::request() called from main thread — would block render loop");
        return {};
    }
    RpcResult rpc_result;
    if (!impl_->process_ || !impl_->running_)
    {
        return rpc_result;
    }

    uint32_t msgid = impl_->next_msgid_++;

    std::vector<char> encoded;
    if (!encode_rpc_request(msgid, method, params, encoded))
    {
        DRAXUL_LOG_ERROR(LogCategory::Rpc, "Failed to encode request %s", method.c_str());
        return rpc_result;
    }

    {
        std::lock_guard<std::mutex> write_lock(impl_->write_mutex_);
        if (!impl_->running_)
            return rpc_result;

        if (!impl_->process_->write(reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size()))
        {
            DRAXUL_LOG_ERROR(LogCategory::Rpc, "Write failed for request %s", method.c_str());
            impl_->read_failed_ = true;
            impl_->response_cv_.notify_all();
            return rpc_result;
        }
    }

    std::unique_lock<std::mutex> lock(impl_->response_mutex_);
    bool ready = impl_->response_cv_.wait_for(lock, kRequestTimeout, [this, &msgid]() {
        return impl_->responses_.count(msgid) > 0 || !impl_->running_ || impl_->read_failed_ || (impl_->process_ && !impl_->process_->is_running());
    });

    if (!ready || impl_->responses_.count(msgid) == 0)
    {
        DRAXUL_LOG_WARN(LogCategory::Rpc, "Request timed out or aborted: %s", method.c_str());
        impl_->responses_.erase(msgid);
        return rpc_result;
    }

    auto resp = std::move(impl_->responses_[msgid]);
    impl_->responses_.erase(msgid);
    rpc_result.result = std::move(resp.result);
    rpc_result.error = std::move(resp.error);
    rpc_result.transport_ok = true;
    return rpc_result;
}

void NvimRpc::notify(const std::string& method, const std::vector<MpackValue>& params)
{
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
    std::lock_guard<std::mutex> lock(impl_->notif_mutex_);
    std::vector<RpcNotification> result;
    result.swap(impl_->notifications_);
    return result;
}

size_t NvimRpc::notification_queue_depth() const
{
    std::lock_guard<std::mutex> lock(impl_->notif_mutex_);
    return impl_->notifications_.size();
}

void NvimRpc::dispatch_rpc_response(const std::vector<MpackValue>& msg_array)
{
    RpcResponse resp;
    resp.msgid = (uint32_t)msg_array[1].as_int();
    resp.error = msg_array[2];
    resp.result = msg_array[3];

    std::lock_guard<std::mutex> lock(impl_->response_mutex_);
    impl_->responses_[resp.msgid] = std::move(resp);
    impl_->response_cv_.notify_all();
}

void NvimRpc::dispatch_rpc_request(const std::vector<MpackValue>& msg_array)
{
    auto req_msgid = (uint32_t)msg_array[1].as_int();
    std::string method = msg_array[2].as_str();
    std::vector<MpackValue> params;
    if (msg_array[3].type() == MpackValue::Array)
        params = msg_array[3].as_array();

    MpackValue result = NvimRpc::make_nil();
    MpackValue error = NvimRpc::make_nil();
    if (on_request)
        result = on_request(method, params);
    else
        error = NvimRpc::make_str("no handler for: " + method);

    reply_to_request(req_msgid, error, result);
}

void NvimRpc::dispatch_rpc_notification(const std::vector<MpackValue>& msg_array)
{
    RpcNotification notif;
    notif.method = msg_array[1].as_str();
    if (msg_array[2].type() == MpackValue::Array)
        notif.params = msg_array[2].as_array();

    {
        std::lock_guard<std::mutex> lock(impl_->notif_mutex_);
        if (impl_->notifications_.size() >= kMaxNotificationQueueDepth)
        {
            // Drop the oldest notification to stay bounded.
            impl_->notifications_.erase(impl_->notifications_.begin());
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

    if (on_notification_available)
        on_notification_available();
}

void NvimRpc::dispatch_rpc_message(const MpackValue& msg)
{
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
    std::vector<uint8_t> accum;
    accum.reserve(1024 * 1024);
    size_t read_pos = 0;

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
            if (on_notification_available)
                on_notification_available();
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

        while (read_pos < accum.size())
        {
            MpackValue msg;
            size_t consumed = 0;
            std::span<const uint8_t> remaining(accum.data() + read_pos, accum.size() - read_pos);
            if (!decode_mpack_value(remaining, msg, &consumed) || consumed == 0)
                break;

            read_pos += consumed;

            // Reset when the buffer is fully drained to avoid unbounded growth.
            if (read_pos == accum.size())
            {
                accum.clear();
                read_pos = 0;
            }

            dispatch_rpc_message(msg);
        }
    }
}

void NvimRpc::reply_to_request(uint32_t msgid, const MpackValue& error, const MpackValue& result)
{
    std::vector<char> encoded;
    if (!encode_rpc_response(msgid, error, result, encoded))
    {
        DRAXUL_LOG_ERROR(LogCategory::Rpc, "Failed to encode response for msgid %u", msgid);
        return;
    }
    std::lock_guard<std::mutex> lock(impl_->write_mutex_);
    if (impl_->process_)
        impl_->process_->write(reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size());
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
