#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <draxul/nvim_protocol.h>

namespace draxul
{

class NvimProcess
{
public:
    NvimProcess();
    ~NvimProcess();

    // Result remains contextually convertible to bool for legacy callers.
    Result<void, Error> spawn(const std::string& nvim_path = "nvim",
        const std::vector<std::string>& extra_args = {},
        const std::string& working_dir = {});
    void shutdown();

    bool write(const uint8_t* data, size_t len) const;
    int read(uint8_t* buffer, size_t max_len) const;

    bool is_running() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// These callbacks must be supplied to initialize(), before the reader thread
// starts. std::thread construction publishes the stored functions to the new
// thread, and they are never reassigned afterwards.
struct RpcCallbacks
{
    // Invoked on the reader thread when a notification arrives or the pipe
    // fails. Must not block or acquire a main-thread drain mutex.
    std::function<void()> on_notification_available;

    // Invoked synchronously on the reader thread for Neovim rpcrequests while
    // the RPC write mutex is not held.
    std::function<MpackValue(const std::string& method, const std::vector<MpackValue>& params)> on_request;
};

class NvimRpc : public IRpcChannel
{
public:
    NvimRpc();
    ~NvimRpc() override;

    // Stores callbacks before starting the reader thread.
    bool initialize(NvimProcess& process, RpcCallbacks callbacks = {});
    void close();
    void shutdown();

    RpcResult request(const std::string& method, const std::vector<MpackValue>& params) override;
    void notify(const std::string& method, const std::vector<MpackValue>& params) override;

    std::vector<RpcNotification> drain_notifications();
    // Thread-safe; acquires the internal notification mutex.
    size_t notification_queue_depth() const;
    // True after an unexpected pipe close rather than requested shutdown.
    bool connection_failed() const;

    // Compatibility factories retained for existing callers. Protocol-only
    // code should use MpackValue::make_* so it does not require transport.
    static MpackValue make_int(int64_t v);
    static MpackValue make_uint(uint64_t v);
    static MpackValue make_str(const std::string& v);
    static MpackValue make_bool(bool v);
    static MpackValue make_array(std::vector<MpackValue> v);
    static MpackValue make_map(std::vector<std::pair<MpackValue, MpackValue>> v);
    static MpackValue make_nil();

    // Once set, request() asserts that it is not called from this thread.
    void set_main_thread_id(std::thread::id id);

private:
    // Set before reader startup and immutable afterwards.
    RpcCallbacks callbacks_;

    void reader_thread_func();
    void reply_to_request(uint32_t msgid, const MpackValue& error, const MpackValue& result);
    void dispatch_rpc_message(const MpackValue& msg);
    void dispatch_rpc_response(const std::vector<MpackValue>& msg_array);
    void dispatch_rpc_request(const std::vector<MpackValue>& msg_array);
    void dispatch_rpc_notification(const std::vector<MpackValue>& msg_array);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::thread::id main_thread_id_{};
};

} // namespace draxul
