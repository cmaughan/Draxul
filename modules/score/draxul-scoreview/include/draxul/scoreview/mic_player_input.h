#pragma once

#include <draxul/scoreview/flow_controller.h>
#include <draxul/scoreview/note_listener.h>
#include <draxul/scoreview/player_input.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace draxul
{
namespace scoreview
{

// Device and permission boundary for the microphone input. The production
// implementation delegates to AVFoundation/SDL; tests provide deterministic
// implementations so every opener/destructor handoff can be exercised without
// touching a real device.
class IMicrophoneOps
{
public:
    enum class Permission
    {
        Pending,
        Granted,
        Denied,
    };

    using Stream = void*;

    virtual ~IMicrophoneOps() = default;

    virtual bool initialize(std::string& error) = 0;
    virtual Permission query_permission() = 0;
    virtual void delay_permission_poll() = 0;
    virtual Stream open_stream(int sample_rate, std::string& error) = 0;
    virtual bool resume_stream(Stream stream, std::string& error) = 0;
    virtual int available_bytes(Stream stream) = 0;
    virtual int read_bytes(Stream stream, void* destination, int byte_count) = 0;
    virtual void clear(Stream stream) = 0;
    virtual void destroy(Stream stream) = 0;
};

// The acoustic front-end (plans/scoreview-ear.md E2): SDL records the
// default microphone (f32 mono at the listener's rate — SDL converts
// whatever the hardware provides), poll() drains the buffered stream on the
// main thread, arms the NoteListener with the flow's currently armed gate
// pitches, and forwards note events in the seam's currency.
//
// Permission and device open run ASYNCHRONOUSLY: on macOS the worker polls
// the non-blocking TCC consent state before it touches SDL's device layer.
// Construction returns immediately in the Opening state; the host keeps
// pumping, and falls back to the keyboard if state() reaches Failed.
class MicPlayerInput final : public IPlayerInput
{
public:
    enum class State : int
    {
        Opening = 0, // worker waiting on device/permission
        Ready = 1,
        Failed = 2,
    };

    explicit MicPlayerInput(const FlowController& flow, ListenerTuning tuning = {});
    MicPlayerInput(const FlowController& flow, ListenerTuning tuning,
        std::shared_ptr<IMicrophoneOps> microphone_ops);
    ~MicPlayerInput() override;

    MicPlayerInput(const MicPlayerInput&) = delete;
    MicPlayerInput& operator=(const MicPlayerInput&) = delete;

    State state() const;
    std::string error() const; // valid once state() == Failed

    void poll(double t_now_seconds, std::vector<PlayerNoteEvent>& out) override;

    // Recent input peak, 0..1 (fast attack, gentle release) — the status
    // pill's level meter.
    float level() const
    {
        return level_;
    }
    const NoteListener& listener() const
    {
        return listener_;
    }

private:
    // Outlives the object if the opener thread is still polling consent. The
    // mutex makes state, error, and stream ownership one atomic publication.
    struct Shared
    {
        enum class Lifecycle
        {
            Opening,
            Resuming,
            Ready,
            Failed,
            Abandoned,
        };

        std::mutex mutex;
        Lifecycle lifecycle = Lifecycle::Opening;
        IMicrophoneOps::Stream stream = nullptr;
        std::string error;
        int sample_rate = 0;
        std::shared_ptr<IMicrophoneOps> ops;
    };

    const FlowController& flow_;
    NoteListener listener_;
    std::shared_ptr<Shared> shared_;
    std::vector<float> drain_buffer_;
    float level_ = 0.0f;
    bool time_base_set_ = false;
    bool logged_ready_ = false;
};

} // namespace scoreview
} // namespace draxul
