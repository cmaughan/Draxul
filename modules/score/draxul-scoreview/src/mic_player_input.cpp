#include <draxul/scoreview/mic_player_input.h>

#include "mic_permission.h"

#include <draxul/log.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <thread>

namespace draxul
{
namespace scoreview
{

namespace
{

class SdlMicrophoneOps final : public IMicrophoneOps
{
public:
    bool initialize(std::string& error) override
    {
        if (SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO))
            return true;
        error = std::string("SDL audio init failed: ") + SDL_GetError();
        return false;
    }

    Permission query_permission() override
    {
        switch (query_mic_permission())
        {
        case MicPermission::Granted:
            return Permission::Granted;
        case MicPermission::Denied:
            return Permission::Denied;
        case MicPermission::Pending:
        default:
            return Permission::Pending;
        }
    }

    void delay_permission_poll() override
    {
        SDL_Delay(100);
    }

    Stream open_stream(int sample_rate, std::string& error) override
    {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = 1;
        spec.freq = sample_rate;
        SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, nullptr, nullptr);
        if (stream == nullptr)
            error = std::string("microphone unavailable: ") + SDL_GetError();
        return stream;
    }

    bool resume_stream(Stream stream, std::string& error) override
    {
        if (SDL_ResumeAudioStreamDevice(static_cast<SDL_AudioStream*>(stream)))
            return true;
        error = std::string("microphone unavailable: ") + SDL_GetError();
        return false;
    }

    int available_bytes(Stream stream) override
    {
        return SDL_GetAudioStreamAvailable(static_cast<SDL_AudioStream*>(stream));
    }

    int read_bytes(Stream stream, void* destination, int byte_count) override
    {
        return SDL_GetAudioStreamData(
            static_cast<SDL_AudioStream*>(stream), destination, byte_count);
    }

    void clear(Stream stream) override
    {
        SDL_ClearAudioStream(static_cast<SDL_AudioStream*>(stream));
    }

    void destroy(Stream stream) override
    {
        SDL_DestroyAudioStream(static_cast<SDL_AudioStream*>(stream));
    }
};

} // namespace

MicPlayerInput::MicPlayerInput(const FlowController& flow, ListenerTuning tuning)
    : MicPlayerInput(flow, tuning, std::make_shared<SdlMicrophoneOps>())
{
}

MicPlayerInput::MicPlayerInput(const FlowController& flow, ListenerTuning tuning,
    std::shared_ptr<IMicrophoneOps> microphone_ops)
    : flow_(flow)
    , listener_(tuning)
    , shared_(std::make_shared<Shared>())
{
    shared_->sample_rate = tuning.sample_rate;
    shared_->ops = std::move(microphone_ops);
    std::string init_error;
    if (shared_->ops == nullptr)
        init_error = "microphone operations are unavailable";
    else if (shared_->ops->initialize(init_error))
        init_error.clear();
    if (!init_error.empty())
    {
        std::lock_guard lock(shared_->mutex);
        shared_->error = std::move(init_error);
        shared_->lifecycle = Shared::Lifecycle::Failed;
        return;
    }
    // The opener owns its own reference to Shared, so a pending consent
    // dialog can never dangle or hang shutdown.
    std::thread([s = shared_]() {
        const auto abandoned = [&s]() {
            std::lock_guard lock(s->mutex);
            return s->lifecycle == Shared::Lifecycle::Abandoned;
        };

        // Pre-flight the OS permission WITHOUT touching SDL's device layer:
        // a TCC-blocked SDL open holds a device lock that deadlocks
        // SDL_Quit's audio teardown. Only open once macOS says yes, so the
        // SDL call below is always fast.
        for (;;)
        {
            if (abandoned())
                return; // owner gone before anything was opened
            const IMicrophoneOps::Permission permission = s->ops->query_permission();
            if (permission == IMicrophoneOps::Permission::Granted)
                break;
            if (permission == IMicrophoneOps::Permission::Denied)
            {
                std::lock_guard lock(s->mutex);
                if (s->lifecycle == Shared::Lifecycle::Opening)
                {
                    s->error = "microphone permission denied (System Settings -> "
                               "Privacy & Security -> Microphone)";
                    s->lifecycle = Shared::Lifecycle::Failed;
                }
                return;
            }
            s->ops->delay_permission_poll();
        }
        if (abandoned())
            return;

        std::string open_error;
        IMicrophoneOps::Stream stream = s->ops->open_stream(s->sample_rate, open_error);
        if (stream == nullptr)
        {
            std::lock_guard lock(s->mutex);
            if (s->lifecycle == Shared::Lifecycle::Opening)
            {
                s->error = open_error.empty() ? "microphone unavailable" : std::move(open_error);
                s->lifecycle = Shared::Lifecycle::Failed;
            }
            return;
        }

        bool begin_resume = false;
        {
            std::lock_guard lock(s->mutex);
            if (s->lifecycle == Shared::Lifecycle::Opening)
            {
                // The worker keeps sole ownership of `stream` while resume
                // runs. Resuming is visible to destruction, but the stream is
                // not published and cannot be destroyed underneath the
                // potentially blocking device call.
                s->lifecycle = Shared::Lifecycle::Resuming;
                begin_resume = true;
            }
        }
        if (!begin_resume)
        {
            s->ops->destroy(stream);
            return;
        }

        std::string resume_error;
        const bool resumed = s->ops->resume_stream(stream, resume_error);

        bool publish = false;
        {
            std::lock_guard lock(s->mutex);
            if (s->lifecycle == Shared::Lifecycle::Resuming)
            {
                if (resumed)
                {
                    s->stream = stream;
                    s->lifecycle = Shared::Lifecycle::Ready;
                    publish = true;
                }
                else
                {
                    s->error = resume_error.empty() ? "microphone unavailable" : std::move(resume_error);
                    s->lifecycle = Shared::Lifecycle::Failed;
                }
            }
        }

        // A successful publication transfers ownership to MicPlayerInput.
        // Every other path (resume failure or abandonment at either handoff)
        // leaves ownership with this worker and destroys exactly once.
        if (!publish)
        {
            s->ops->destroy(stream);
        }
    }).detach();
}

MicPlayerInput::~MicPlayerInput()
{
    IMicrophoneOps::Stream stream = nullptr;
    std::shared_ptr<IMicrophoneOps> ops;
    {
        std::lock_guard lock(shared_->mutex);
        stream = shared_->stream;
        shared_->stream = nullptr;
        shared_->lifecycle = Shared::Lifecycle::Abandoned;
        ops = shared_->ops;
    }
    if (stream != nullptr)
        ops->destroy(stream);
    // Never join: an undecided permission callback may outlive the host. It
    // owns only Shared and will observe Abandoned before publishing anything.
}

MicPlayerInput::State MicPlayerInput::state() const
{
    std::lock_guard lock(shared_->mutex);
    switch (shared_->lifecycle)
    {
    case Shared::Lifecycle::Ready:
        return State::Ready;
    case Shared::Lifecycle::Failed:
        return State::Failed;
    case Shared::Lifecycle::Opening:
    case Shared::Lifecycle::Resuming:
    case Shared::Lifecycle::Abandoned:
    default:
        return State::Opening;
    }
}

std::string MicPlayerInput::error() const
{
    std::lock_guard lock(shared_->mutex);
    return shared_->lifecycle == Shared::Lifecycle::Failed ? shared_->error : std::string();
}

void MicPlayerInput::poll(double t_now_seconds, std::vector<PlayerNoteEvent>& out)
{
    // The armed gate's still-pending pitches focus the listener's scoring;
    // wrong notes still surface through its 88-key sweep.
    listener_.set_expected_pitches(flow_.armed_required_pitches());

    std::unique_lock lock(shared_->mutex);
    if (shared_->lifecycle != Shared::Lifecycle::Ready || shared_->stream == nullptr)
        return;
    if (!logged_ready_)
    {
        logged_ready_ = true;
        DRAXUL_LOG_INFO(LogCategory::App, "score: microphone capture open (%d Hz f32 mono)",
            shared_->sample_rate);
    }

    const int available = shared_->ops->available_bytes(shared_->stream);
    const size_t samples = available > 0 ? static_cast<size_t>(available) / sizeof(float) : 0;
    if (samples == 0)
        return;
    // A long transport pause leaves stale audio queued (poll only runs while
    // playing); judging minute-old notes helps nobody. Drop the backlog and
    // re-anchor the clock on the next fresh drain.
    if (samples > static_cast<size_t>(listener_.tuning().sample_rate) * 3)
    {
        shared_->ops->clear(shared_->stream);
        time_base_set_ = false;
        return;
    }
    drain_buffer_.resize(samples);
    const int got_bytes = shared_->ops->read_bytes(
        shared_->stream, drain_buffer_.data(), static_cast<int>(samples * sizeof(float)));
    lock.unlock();
    if (got_bytes <= 0)
        return;
    const size_t got = static_cast<size_t>(got_bytes) / sizeof(float);

    // The first drain anchors the listener's sample clock to the host
    // clock: these samples were captured over the interval ending ~now.
    // (Clock drift over a practice session is microseconds — revisit in E3
    // only if real sessions disagree.)
    if (!time_base_set_)
    {
        time_base_set_ = true;
        listener_.set_time_base(
            t_now_seconds - static_cast<double>(got) / listener_.tuning().sample_rate);
    }

    float peak = 0.0f;
    for (size_t i = 0; i < got; ++i)
        peak = std::max(peak, std::abs(drain_buffer_[i]));
    level_ = std::max(peak, level_ * 0.85f);

    listener_.push_samples(drain_buffer_.data(), got);
    for (const PlayerNoteEvent& event : listener_.take_events())
        out.push_back(event);
}

} // namespace scoreview
} // namespace draxul
