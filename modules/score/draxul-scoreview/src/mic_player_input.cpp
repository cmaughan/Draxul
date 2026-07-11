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

MicPlayerInput::MicPlayerInput(const FlowController& flow, ListenerTuning tuning)
    : flow_(flow)
    , listener_(tuning)
    , shared_(std::make_shared<Shared>())
{
    shared_->sample_rate = tuning.sample_rate;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        shared_->error = std::string("SDL audio init failed: ") + SDL_GetError();
        shared_->state = static_cast<int>(State::Failed);
        return;
    }
    // The opener owns its own reference to Shared, so a pending consent
    // dialog can never dangle or hang shutdown.
    std::thread([s = shared_]() {
        // Pre-flight the OS permission WITHOUT touching SDL's device layer:
        // a TCC-blocked SDL open holds a device lock that deadlocks
        // SDL_Quit's audio teardown. Only open once macOS says yes, so the
        // SDL call below is always fast.
        for (;;)
        {
            if (s->abandoned.load())
                return; // owner gone before anything was opened
            const MicPermission permission = query_mic_permission();
            if (permission == MicPermission::Granted)
                break;
            if (permission == MicPermission::Denied)
            {
                s->error = "microphone permission denied (System Settings -> "
                           "Privacy & Security -> Microphone)";
                s->state = static_cast<int>(State::Failed);
                return;
            }
            SDL_Delay(100); // dialog up; poll until answered
        }
        if (s->abandoned.load())
            return;
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = 1;
        spec.freq = s->sample_rate;
        SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, nullptr, nullptr);
        if (stream == nullptr)
        {
            s->error = std::string("microphone unavailable: ") + SDL_GetError();
            s->state = static_cast<int>(State::Failed);
            return;
        }
        if (s->abandoned.exchange(true))
        {
            // The owner was destroyed while we opened: ours to close.
            SDL_DestroyAudioStream(stream);
            return;
        }
        SDL_ResumeAudioStreamDevice(stream); // device streams open paused
        s->stream = stream;
        s->state = static_cast<int>(State::Ready);
    }).detach();
}

MicPlayerInput::~MicPlayerInput()
{
    if (shared_->abandoned.exchange(true))
    {
        // The opener finished first — the stream (if any) is ours to close.
        if (shared_->stream != nullptr)
            SDL_DestroyAudioStream(shared_->stream);
    }
    // Otherwise the opener is still blocked; it will observe `abandoned`
    // and clean up itself. Never join — shutdown must not wait on a dialog.
}

MicPlayerInput::State MicPlayerInput::state() const
{
    return static_cast<State>(shared_->state.load());
}

std::string MicPlayerInput::error() const
{
    return state() == State::Failed ? shared_->error : std::string();
}

void MicPlayerInput::poll(double t_now_seconds, std::vector<PlayerNoteEvent>& out)
{
    if (state() != State::Ready)
        return;
    if (!logged_ready_)
    {
        logged_ready_ = true;
        DRAXUL_LOG_INFO(LogCategory::App, "score: microphone capture open (%d Hz f32 mono)",
            shared_->sample_rate);
    }

    // The armed gate's still-pending pitches focus the listener's scoring;
    // wrong notes still surface through its 88-key sweep.
    listener_.set_expected_pitches(flow_.armed_required_pitches());

    const int available = SDL_GetAudioStreamAvailable(shared_->stream);
    const size_t samples = available > 0 ? static_cast<size_t>(available) / sizeof(float) : 0;
    if (samples == 0)
        return;
    // A long transport pause leaves stale audio queued (poll only runs while
    // playing); judging minute-old notes helps nobody. Drop the backlog and
    // re-anchor the clock on the next fresh drain.
    if (samples > static_cast<size_t>(listener_.tuning().sample_rate) * 3)
    {
        SDL_ClearAudioStream(shared_->stream);
        time_base_set_ = false;
        return;
    }
    drain_buffer_.resize(samples);
    const int got_bytes = SDL_GetAudioStreamData(
        shared_->stream, drain_buffer_.data(), static_cast<int>(samples * sizeof(float)));
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
