#include "score_audio_controller.h"

#include <draxul/log.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <system_error>

namespace draxul
{
namespace scoreview
{

ScoreAudioController::~ScoreAudioController()
{
    shutdown();
}

void ScoreAudioController::stage_soundfonts(const std::filesystem::path& dir)
{
    std::error_code list_error;
    for (const auto& entry : std::filesystem::directory_iterator(dir, list_error))
    {
        if (entry.path().extension() == ".sf2")
            soundfont_paths_.push_back(entry.path());
    }
    std::sort(soundfont_paths_.begin(), soundfont_paths_.end());
}

void ScoreAudioController::set_tick_level(TickLevel level)
{
    tick_level_ = level;
    if (tick_level_ == TickLevel::Off && tick_stream_ != nullptr)
    {
        SDL_ClearAudioStream(tick_stream_);
        metronome_.clear();
    }
}

void ScoreAudioController::cycle_tick_level()
{
    tick_level_ = tick_level_ == TickLevel::Off
        ? TickLevel::Beats
        : (tick_level_ == TickLevel::Beats ? TickLevel::Eighths : TickLevel::Off);
    if (tick_level_ == TickLevel::Off && tick_stream_ != nullptr)
    {
        SDL_ClearAudioStream(tick_stream_);
        metronome_.clear();
    }
    DRAXUL_LOG_INFO(LogCategory::App, "score: metronome %s",
        tick_level_ == TickLevel::Off ? "off"
                                      : (tick_level_ == TickLevel::Beats ? "beats" : "eighths"));
}

void ScoreAudioController::set_audition(bool on)
{
    audition_ = on;
    if (audition_ && voice_ == Voice::Piano && !ensure_piano_voice())
        use_synth();
    if (!audition_)
    {
        tones_.clear();
        piano_.clear();
    }
}

void ScoreAudioController::toggle_audition()
{
    set_audition(!audition_);
    DRAXUL_LOG_INFO(LogCategory::App, "score: audition %s", audition_ ? "on" : "off");
}

void ScoreAudioController::use_synth()
{
    voice_ = Voice::Synth;
    piano_.clear(); // release held piano voices
}

bool ScoreAudioController::prefer_piano(int index)
{
    if (soundfont_paths_.empty())
        return false;
    piano_selected_index_
        = std::clamp(index, 0, static_cast<int>(soundfont_paths_.size()) - 1);
    voice_ = Voice::Piano;
    tones_.clear();
    return true;
}

bool ScoreAudioController::use_piano(int index)
{
    piano_selected_index_ = index;
    if (!ensure_piano_voice())
        return false;
    voice_ = Voice::Piano;
    tones_.clear();
    return true;
}

bool ScoreAudioController::ensure_piano_voice()
{
    if (soundfont_paths_.empty())
    {
        DRAXUL_LOG_WARN(LogCategory::App, "score: no .sf2 soundfonts staged beside the app");
        return false;
    }
    const int want = std::clamp(
        piano_selected_index_, 0, static_cast<int>(soundfont_paths_.size()) - 1);
    if (piano_loaded_index_ == want && piano_.loaded())
        return true;
    std::string error;
    if (!piano_.load(soundfont_paths_[static_cast<size_t>(want)].string(),
            metronome_.tuning().sample_rate, error))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "score: %s — staying on the synth voice", error.c_str());
        return false;
    }
    piano_loaded_index_ = want;
    return true;
}

bool ScoreAudioController::ensure_output_stream()
{
    if (tick_stream_ != nullptr)
        return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "score: metronome audio init failed: %s", SDL_GetError());
        return false;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq = metronome_.tuning().sample_rate;
    tick_stream_
        = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (tick_stream_ == nullptr)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "score: metronome output unavailable: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(tick_stream_); // device streams open paused
    DRAXUL_LOG_INFO(LogCategory::App, "score: metronome output open (%d Hz)",
        metronome_.tuning().sample_rate);
    return true;
}

void ScoreAudioController::shutdown()
{
    if (tick_stream_ != nullptr)
    {
        SDL_DestroyAudioStream(tick_stream_);
        tick_stream_ = nullptr;
    }
}

void ScoreAudioController::pump(double p0_q, double p1_q, double dt, double quarters_per_bar,
    const FlowController& flow)
{
    if (!ensure_output_stream())
    {
        tick_level_ = TickLevel::Off;
        return;
    }
    const int rate = metronome_.tuning().sample_rate;

    // Schedule ticks for every grid crossing in (p0, p1], placed at their
    // fractional offset inside this pump's time slice so beat spacing
    // tracks the transport rather than the frame rate.
    if (p1_q > p0_q && dt > 0.0 && tick_level_ != TickLevel::Off)
    {
        const double step = tick_level_ == TickLevel::Eighths ? 0.5 : 1.0;
        double grid = (std::floor(p0_q / step + 1e-9) + 1.0) * step;
        for (; grid <= p1_q + 1e-9; grid += step)
        {
            const double fraction = std::clamp((grid - p0_q) / (p1_q - p0_q), 0.0, 1.0);
            const int64_t at = metronome_.cursor()
                + static_cast<int64_t>(fraction * dt * static_cast<double>(rate));
            TickKind kind = TickKind::Subdivision;
            if (std::abs(grid - std::round(grid)) < 1e-6)
            {
                const double in_bar = std::fmod(std::round(grid), quarters_per_bar);
                kind = std::abs(in_bar) < 1e-6 ? TickKind::Accent : TickKind::Beat;
            }
            metronome_.schedule_tick(at, kind);
        }
    }

    // Audition: sound every note whose onset the playhead crossed this
    // pump (all sounding pitches, ties included — the score as heard).
    if (audition_ && p1_q > p0_q && dt > 0.0 && flow.gates_ready())
    {
        const auto& onsets = flow.onsets();
        const auto& gates = flow.gates();
        for (size_t i = 0; i < onsets.size() && i < gates.size(); ++i)
        {
            const double q = onsets[i].qstamp;
            if (q <= p0_q)
                continue;
            if (q > p1_q)
                break;
            const double fraction = std::clamp((q - p0_q) / (p1_q - p0_q), 0.0, 1.0);
            const bool piano = voice_ == Voice::Piano && piano_.loaded();
            const int64_t at = (piano ? piano_.cursor() : tones_.cursor())
                + static_cast<int64_t>(fraction * dt * static_cast<double>(rate));
            for (const FlowController::GateNote& note : gates[i].notes)
            {
                if (note.pitch < 0)
                    continue;
                if (piano)
                    piano_.schedule_note(at, note.pitch, 0.55f);
                else
                    tones_.schedule_note(at, note.pitch, 0.16f);
            }
        }
    }

    // Keep ~70 ms queued so the device never starves between pumps.
    constexpr int kTargetSamples = 3072;
    const int queued_bytes = SDL_GetAudioStreamQueued(tick_stream_);
    const int queued = queued_bytes > 0 ? queued_bytes / static_cast<int>(sizeof(float)) : 0;
    if (queued < kTargetSamples)
    {
        const size_t need = static_cast<size_t>(kTargetSamples - queued);
        tick_buffer_.resize(need);
        metronome_.render(tick_buffer_.data(), need);
        tone_buffer_.resize(need);
        tones_.render(tone_buffer_.data(), need);
        for (size_t at = 0; at < need; ++at)
            tick_buffer_[at] += tone_buffer_[at];
        // Both voices render every block (a silent synth is near-free), so
        // switching instruments never clicks or drops scheduled tails.
        if (piano_.loaded())
        {
            piano_buffer_.resize(need);
            piano_.render(piano_buffer_.data(), need);
            for (size_t at = 0; at < need; ++at)
                tick_buffer_[at] += piano_buffer_[at];
        }
        SDL_PutAudioStreamData(
            tick_stream_, tick_buffer_.data(), static_cast<int>(need * sizeof(float)));
    }
}

} // namespace scoreview
} // namespace draxul
