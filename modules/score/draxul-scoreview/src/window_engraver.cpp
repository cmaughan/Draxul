#include <draxul/scoreview/window_engraver.h>

#include <draxul/scoreview/engraved_window.h>
#include <draxul/scoreview/verovio_layout_engine.h>

#include <draxul/log.h>

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
    return std::unique_ptr<WindowEngraver>(new WindowEngraver(std::move(engine)));
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
    }
    // Wake the worker whether it is waiting for a job or mid-engrave finishing.
    job_cv_.notify_all();
    done_cv_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

bool WindowEngraver::busy() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ != State::Idle;
}

void WindowEngraver::submit(Job job)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::Idle)
        {
            // Caller is expected to gate on busy(); ignore rather than clobber
            // an in-flight engrave.
            DRAXUL_LOG_WARN(LogCategory::App, "score: window engrave submitted while busy, ignored");
            return;
        }
        job_ = std::move(job);
        state_ = State::Working;
    }
    job_cv_.notify_one();
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
    std::unique_lock<std::mutex> lock(mutex_);
    done_cv_.wait(lock, [this] { return state_ != State::Working; });
    if (state_ == State::Ready)
    {
        result_ = Done{};
        state_ = State::Idle;
    }
}

void WindowEngraver::worker_loop()
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            job_cv_.wait(lock, [this] { return state_ == State::Working || stop_; });
            if (stop_)
                return;
            job = std::move(job_);
            job_ = Job{};
        }

        // The heavy part runs WITHOUT the lock: load + layout + serialize/parse
        // on this thread's own engine, so the main thread keeps rendering.
        Done done;
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
            result_ = std::move(done);
            state_ = State::Ready;
        }
        done_cv_.notify_all();
    }
}

} // namespace scoreview
} // namespace draxul
