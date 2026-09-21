#pragma once

#include <condition_variable>
#include <draxul/nvim_protocol.h>
#include <mutex>
#include <string>
#include <thread>

#include <draxul/ui_request_worker_state.h>

namespace draxul
{

class UiRequestWorker
{
public:
    UiRequestWorker() = default;
    // Safety net: if the owner forgets to call stop() (partial NvimHost
    // initialization, exception mid-init, etc.) the joinable std::thread
    // member would otherwise trigger std::terminate on destruction.
    ~UiRequestWorker();

    UiRequestWorker(const UiRequestWorker&) = delete;
    UiRequestWorker& operator=(const UiRequestWorker&) = delete;
    UiRequestWorker(UiRequestWorker&&) = delete;
    UiRequestWorker& operator=(UiRequestWorker&&) = delete;

    void start(IRpcChannel* rpc);
    void stop();
    void request_resize(int cols, int rows, std::string reason);

private:
    void thread_main();

    IRpcChannel* rpc_ = nullptr;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    UiRequestWorkerState state_;
};

} // namespace draxul
