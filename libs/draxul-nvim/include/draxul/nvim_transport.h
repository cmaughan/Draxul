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

    Result<void, Error> spawn(const std::string& nvim_path = "nvim",
        const std::vector<std::string>& extra_args = {},
        const std::string& working_dir = {});
    // Cancels write ownership before closing the pipes and reaping the child.
    void shutdown();

    // Writes the whole buffer, blocking while the child is not reading. Returns
    // false on a pipe error or after cancel_writes().
    bool write(const uint8_t* data, size_t len) const;
    // Makes every in-progress and later write() return false promptly, even
    // when the child never reads again. Waits only a bounded time for blocked
    // writers to leave the pipe. Idempotent; spawn() re-arms writes.
    void cancel_writes() const;
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
    // Invoked on the reader thread when a notification arrives or the read
    // pipe fails, and once on the writer thread when an unrequested write
    // failure ends the transport. Must not block or acquire a main-thread
    // drain mutex.
    std::function<void()> on_notification_available;

    // Invoked synchronously on the reader thread for Neovim rpcrequests while
    // the RPC write mutex is not held.
    std::function<MpackValue(const std::string& method, const std::vector<MpackValue>& params)> on_request;
};

// Outbound ownership: request(), notify(), and replies to Neovim requests
// encode on the calling thread and enqueue into one FIFO owned by a writer
// thread, so a child that stops reading never blocks the caller. The FIFO is
// bounded by kMaxOutboundBytes; a message that would exceed it is rejected
// whole, so the stream never carries a partial message.
class NvimRpc : public IRpcChannel
{
public:
    static constexpr size_t kMaxOutboundBytes = 256ULL * 1024 * 1024;

    NvimRpc();
    ~NvimRpc() override;

    // Stores callbacks before starting the reader and writer threads.
    bool initialize(NvimProcess& process, RpcCallbacks callbacks = {});
    // Stops accepting messages, gives already accepted output a short bounded
    // flush (so a final quit command reaches a healthy editor), then cancels
    // any write still blocked on a non-reading child. Never waits on Neovim.
    void close();
    void shutdown();

    RpcResult request(const std::string& method, const std::vector<MpackValue>& params) override;
    void notify(const std::string& method, const std::vector<MpackValue>& params) override;

    std::vector<RpcNotification> drain_notifications();
    // Thread-safe; acquires the internal notification mutex.
    size_t notification_queue_depth() const;
    // True after an unexpected pipe close rather than requested shutdown.
    bool connection_failed() const;

    // Once set, request() asserts that it is not called from this thread.
    void set_main_thread_id(std::thread::id id);

private:
    // Set before reader startup and immutable afterwards.
    RpcCallbacks callbacks_;

    void reader_thread_func();
    void writer_thread_func();
    bool enqueue_outbound(std::vector<char> encoded, const char* what);
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
