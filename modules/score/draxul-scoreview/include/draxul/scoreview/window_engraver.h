#pragma once

#include <draxul/scoreview/engraved_window.h>
#include <draxul/scoreview/layout_engine.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace draxul
{
namespace scoreview
{

// Engraves a rolling window on a background thread so the ~100ms Verovio
// serialize/parse round-trip no longer freezes the main (render/input) thread.
// It owns its OWN layout engine (a second Verovio toolkit) — the main thread's
// engine is never touched off-thread, and the two never lay out concurrently
// (the host drains this worker before any synchronous engine use).
//
// One engrave is in flight at a time: submit() posts a job when idle, poll()
// takes the finished result on a later frame, cancel() waits out an in-flight
// engrave and drops any pending result (the synchronous-rebuild barrier).
class WindowEngraver
{
public:
    struct Job
    {
        std::string window_xml;
        EngraveParams params;
        // Echoed back verbatim so the install knows where the window sits.
        int first_bar = 0;
        int count = 0;
        double stream_offset_q = 0.0;
    };

    struct Done
    {
        EngravedWindow window;
        int first_bar = 0;
        int count = 0;
        double stream_offset_q = 0.0;
        bool ok = false;
    };

    // Returns nullptr and fills `error` when the background toolkit's resources
    // cannot be loaded (the host then falls back to synchronous rebuilds).
    static std::unique_ptr<WindowEngraver> create(const std::string& resource_dir, std::string& error);
    ~WindowEngraver();

    WindowEngraver(const WindowEngraver&) = delete;
    WindowEngraver& operator=(const WindowEngraver&) = delete;

    // True while a job is queued/running or a finished result awaits poll().
    // The host only submits when this is false.
    bool busy() const;
    // Posts a job. Precondition: !busy().
    void submit(Job job);
    // Takes the finished result if one is ready; nullopt otherwise. On success
    // the worker returns to idle, ready for the next submit().
    std::optional<Done> poll();
    // Blocks until any in-flight engrave finishes, then drops a pending result
    // and returns to idle. Called before synchronous engine use so the two
    // toolkits never lay out at once.
    void cancel();

private:
    explicit WindowEngraver(std::unique_ptr<ILayoutEngine> engine);
    void worker_loop();

    std::unique_ptr<ILayoutEngine> engine_;

    enum class State : uint8_t
    {
        Idle, // no job, no pending result
        Working, // the worker is engraving
        Ready, // a result awaits poll()
    };

    mutable std::mutex mutex_;
    std::condition_variable job_cv_; // main -> worker: a job (or stop) posted
    std::condition_variable done_cv_; // worker -> main: engrave finished
    State state_ = State::Idle;
    bool stop_ = false;
    Job job_;
    Done result_;
    std::thread thread_;
};

} // namespace scoreview
} // namespace draxul
