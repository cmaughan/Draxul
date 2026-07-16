#include <draxul/scoreview/window_engraver.h>

#include <draxul/scoreview/engraved_window.h>
#include <draxul/scoreview/verovio_layout_engine.h>

#include <draxul/log.h>

#include <exception>
#include <utility>

namespace draxul
{
namespace scoreview
{

std::unique_ptr<WindowEngraver> WindowEngraver::create(
    const std::string& resource_dir, std::string& error)
{
    std::unique_ptr<ILayoutEngine> engine = VerovioLayoutEngine::create(resource_dir, error);
    if (!engine)
        return nullptr;
    return create(std::move(engine), error);
}

std::unique_ptr<WindowEngraver> WindowEngraver::create(
    std::unique_ptr<ILayoutEngine> engine, std::string& error)
{
    if (!engine)
    {
        error = "background engraver has no layout engine";
        return nullptr;
    }
    try
    {
        return std::unique_ptr<WindowEngraver>(new WindowEngraver(std::move(engine)));
    }
    catch (const std::exception& exception)
    {
        error = std::string("could not start background engraver: ") + exception.what();
        return nullptr;
    }
}

WindowEngraver::WindowEngraver(std::unique_ptr<ILayoutEngine> engine)
    : engine_(std::move(engine))
{
    thread_ = std::thread(&WindowEngraver::worker_loop, this);
}

WindowEngraver::~WindowEngraver()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        pending_job_.reset();
        result_ = Done{};
    }
    // Wake an idle worker. A running engine call owns no host state and is
    // joined here through the host's explicit teardown policy.
    job_cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

bool WindowEngraver::busy() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ != State::Idle;
}

WindowEngraver::RequestId WindowEngraver::submit(Job job)
{
    RequestId request_id = 0;
    bool wake_worker = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_)
        {
            DRAXUL_LOG_DEBUG(
                LogCategory::App, "score: window engrave submitted during shutdown, ignored");
            return 0;
        }
        request_id = next_request_id_++;
        latest_request_id_ = request_id;
        Queued queued{ request_id, std::move(job) };
        if (state_ == State::Working)
        {
            if (pending_job_)
                DRAXUL_LOG_DEBUG(LogCategory::App,
                    "score: coalescing window engrave %llu -> %llu",
                    static_cast<unsigned long long>(pending_job_->request_id),
                    static_cast<unsigned long long>(request_id));
            pending_job_ = std::move(queued);
        }
        else
        {
            if (state_ == State::Ready)
            {
                DRAXUL_LOG_DEBUG(LogCategory::App,
                    "score: discarding unpolled window engrave %llu for %llu",
                    static_cast<unsigned long long>(result_.request_id),
                    static_cast<unsigned long long>(request_id));
                result_ = Done{};
            }
            job_ = std::move(queued);
            state_ = State::Working;
            wake_worker = true;
        }
    }
    if (wake_worker)
        job_cv_.notify_one();
    return request_id;
}

std::optional<WindowEngraver::Done> WindowEngraver::poll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != State::Ready)
        return std::nullopt;
    Done done = std::move(result_);
    result_ = Done{};
    state_ = State::Idle;
    return done;
}

void WindowEngraver::cancel()
{
    std::lock_guard<std::mutex> lock(mutex_);
    const RequestId invalidation = next_request_id_++;
    latest_request_id_ = invalidation;
    pending_job_.reset();
    if (state_ == State::Ready)
    {
        result_ = Done{};
        state_ = State::Idle;
    }
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "score: invalidated window engraves before generation %llu",
        static_cast<unsigned long long>(invalidation));
}

void WindowEngraver::worker_loop()
{
    for (;;)
    {
        Queued queued;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            job_cv_.wait(lock, [this] { return state_ == State::Working || stop_; });
            if (stop_)
                return;
            queued = std::move(job_);
            job_ = Queued{};
        }

        // The heavy part runs WITHOUT the lock: load + layout + serialize/parse
        // on this thread's own engine, so the main thread keeps rendering.
        Done done;
        done.request_id = queued.request_id;
        Job& job = queued.job;
        done.first_bar = job.first_bar;
        done.count = job.count;
        done.stream_offset_q = job.stream_offset_q;
        std::string error;
        const EngraveResult result
            = engrave_window(*engine_, job.window_xml, job.params, done.window, error);
        done.ok = result == EngraveResult::Ok;
        if (!done.ok)
            DRAXUL_LOG_WARN(
                LogCategory::App, "score: async window engrave failed (%s)", error.c_str());

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_)
                return; // shutting down: drop the result
            if (pending_job_)
            {
                DRAXUL_LOG_DEBUG(LogCategory::App,
                    "score: dropping stale window engrave %llu; starting %llu",
                    static_cast<unsigned long long>(done.request_id),
                    static_cast<unsigned long long>(pending_job_->request_id));
                job_ = std::move(*pending_job_);
                pending_job_.reset();
                state_ = State::Working;
                continue;
            }
            if (done.request_id != latest_request_id_)
            {
                DRAXUL_LOG_DEBUG(LogCategory::App,
                    "score: dropping invalidated window engrave %llu",
                    static_cast<unsigned long long>(done.request_id));
                state_ = State::Idle;
                continue;
            }
            result_ = std::move(done);
            state_ = State::Ready;
        }
    }
}

} // namespace scoreview
} // namespace draxul
