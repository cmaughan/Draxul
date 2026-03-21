#include <draxul/ui_request_worker.h>

#include <draxul/log.h>

namespace draxul
{

void UiRequestWorker::start(IRpcChannel* rpc)
{
    stop();
    rpc_ = rpc;
    state_.start();
    thread_ = std::thread(&UiRequestWorker::thread_main, this);
}

void UiRequestWorker::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.stop();
    }
    cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
    rpc_ = nullptr;
}

void UiRequestWorker::request_resize(int cols, int rows, std::string reason)
{
    bool accepted = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        accepted = state_.request_resize(cols, rows, std::move(reason));
    }
    if (accepted)
        cv_.notify_one();
}

void UiRequestWorker::thread_main()
{
    while (true)
    {
        PendingResizeRequest request;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return !state_.running() || state_.has_pending_request(); });
            if (!state_.running())
                break;

            auto next = state_.take_pending_request();
            if (!next)
                continue;
            request = std::move(*next);
        }

        if (!rpc_)
            continue;

        auto resize = rpc_->request("nvim_ui_try_resize", { NvimRpc::make_int(request.cols), NvimRpc::make_int(request.rows) });
        if (!resize.ok())
        {
            DRAXUL_LOG_WARN(LogCategory::App, "nvim_ui_try_resize failed during %s", request.reason.c_str());
        }
    }
}

} // namespace draxul
