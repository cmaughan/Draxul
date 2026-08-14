// Ear milestone E0-E1 (plans/scoreview-ear.md): the offline harness and the
// classical NoteListener — onsets, known-target inharmonic scoring, wrong
// notes, octave phantoms, detuned pianos, calibration, sustain overlap, and
// latency bounds. All fixtures are synthetic (deterministic, tunable lies
// that reproduce real piano acoustics); real recordings join later.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/note_listener.h>

#include "support/synthetic_piano.h"
#include "support/temp_dir.h"
#include "support/wav_reader.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace draxul::scoreview;
using draxul::tests::synth_add_noise;
using draxul::tests::synth_add_note;
using draxul::tests::SyntheticPianoConfig;

namespace
{

struct TruthNote
{
    double t = 0.0;
    int midi = 0;
};

struct HarnessResult
{
    std::vector<PlayerNoteEvent> events;
    double max_latency_seconds = 0.0; // event availability delay vs event time
};

// Streams the buffer through the listener in ~10ms chunks (as the host
// would), collecting events and measuring how long after an event's
// timestamp it became available.
HarnessResult run_listener(NoteListener& listener, const std::vector<float>& buffer,
    const std::vector<int>& expected_pitches)
{
    listener.set_expected_pitches(expected_pitches);
    HarnessResult result;
    const size_t chunk = 441; // 10 ms at 44.1k
    const int rate = listener.tuning().sample_rate;
    for (size_t offset = 0; offset < buffer.size(); offset += chunk)
    {
        const size_t count = std::min(chunk, buffer.size() - offset);
        listener.push_samples(buffer.data() + offset, count);
        const double stream_t = static_cast<double>(offset + count) / rate;
        for (const PlayerNoteEvent& event : listener.take_events())
        {
            result.max_latency_seconds = std::max(result.max_latency_seconds, stream_t - event.t_seconds);
            result.events.push_back(event);
        }
    }
    return result;
}

bool has_pitch_near(const std::vector<PlayerNoteEvent>& events, int midi, double t,
    double window_s = 0.08)
{
    for (const PlayerNoteEvent& event : events)
    {
        if (event.midi_pitch == midi && std::abs(event.t_seconds - t) <= window_s)
            return true;
    }
    return false;
}

int count_pitch(const std::vector<PlayerNoteEvent>& events, int midi)
{
    int n = 0;
    for (const PlayerNoteEvent& event : events)
        n += event.midi_pitch == midi ? 1 : 0;
    return n;
}

// Catch prints these only on failure — the diagnostic trail for tuning.
#define DUMP_EVENTS(result)                                   \
    for (const PlayerNoteEvent& dump_event : (result).events) \
    UNSCOPED_INFO("event pitch=" << dump_event.midi_pitch << " t=" << dump_event.t_seconds)

} // namespace

TEST_CASE("listener detects a single note with bounded latency", "[scoreview][listener]")
{
    SyntheticPianoConfig piano;
    std::vector<float> buffer(static_cast<size_t>(44100 * 0.2), 0.0f);
    synth_add_note(buffer, piano, 0.3, 60, 1.2, 0.7);
    buffer.resize(44100 * 2, 0.0f);

    NoteListener listener;
    const HarnessResult result = run_listener(listener, buffer, { 60 });
    DUMP_EVENTS(result);
    REQUIRE(result.events.size() == 1);
    CHECK(result.events[0].midi_pitch == 60);
    CHECK(result.events[0].t_seconds == Catch::Approx(0.3).margin(0.05));
    CHECK(result.max_latency_seconds < 0.12);
}

TEST_CASE("listener tracks a scale in order without extras", "[scoreview][listener]")
{
    SyntheticPianoConfig piano;
    std::vector<float> buffer;
    const int scale[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
    for (int i = 0; i < 8; ++i)
        synth_add_note(buffer, piano, 0.3 + i * 0.4, scale[i], 1.0, 0.7);
    buffer.resize(buffer.size() + 44100, 0.0f);

    NoteListener listener;
    // All scale tones expected (the gate narrows this in practice; the
    // listener must cope with a broad expected set too).
    const HarnessResult result = run_listener(
        listener, buffer, std::vector<int>(std::begin(scale), std::end(scale)));

    DUMP_EVENTS(result);
    REQUIRE(result.events.size() == 8);
    for (int i = 0; i < 8; ++i)
    {
        CHECK(result.events[static_cast<size_t>(i)].midi_pitch == scale[i]);
        CHECK(result.events[static_cast<size_t>(i)].t_seconds == Catch::Approx(0.3 + i * 0.4).margin(0.05));
    }
}

TEST_CASE("listener resolves chords including wide spacing", "[scoreview][listener]")
{
    SyntheticPianoConfig piano;
    std::vector<float> buffer;
    synth_add_note(buffer, piano, 0.3, 48, 1.5, 0.6); // C3
    synth_add_note(buffer, piano, 0.3, 64, 1.5, 0.55); // E4
    synth_add_note(buffer, piano, 0.3, 67, 1.5, 0.55); // G4
    buffer.resize(buffer.size() + 44100, 0.0f);

    NoteListener listener;
    const HarnessResult result = run_listener(listener, buffer, { 48, 64, 67 });
    CHECK(has_pitch_near(result.events, 48, 0.3));
    CHECK(has_pitch_near(result.events, 64, 0.3));
    CHECK(has_pitch_near(result.events, 67, 0.3));
}

TEST_CASE("listener reports a wrong note instead of the expected one", "[scoreview][listener]")
{
    SyntheticPianoConfig piano;
    std::vector<float> buffer;
    synth_add_note(buffer, piano, 0.3, 62, 1.0, 0.7); // played D4...
    buffer.resize(buffer.size() + 44100, 0.0f);

    NoteListener listener;
    const HarnessResult result = run_listener(listener, buffer, { 60 }); // ...expected C4
    DUMP_EVENTS(result);
    CHECK(count_pitch(result.events, 62) == 1);
    CHECK(count_pitch(result.events, 60) == 0);
}

TEST_CASE("listener does not hallucinate the octave above", "[scoreview][listener]")
{
    SyntheticPianoConfig piano;
    std::vector<float> buffer;
    synth_add_note(buffer, piano, 0.3, 60, 1.5, 0.7); // C4 alone
    buffer.resize(buffer.size() + 44100, 0.0f);

    NoteListener listener;
    // Both C4 and C5 expected (an octave passage): only C4 may confirm.
    const HarnessResult expected_octave = run_listener(listener, buffer, { 60, 72 });
    DUMP_EVENTS(expected_octave);
    CHECK(count_pitch(expected_octave.events, 60) == 1);
    CHECK(count_pitch(expected_octave.events, 72) == 0);

    // C4 expected alone: C5 must not be reported as a wrong note either.
    NoteListener listener2;
    const HarnessResult swept = run_listener(listener2, buffer, { 60 });
    CHECK(count_pitch(swept.events, 60) == 1);
    CHECK(count_pitch(swept.events, 72) == 0);
}

TEST_CASE("listener survives a detuned piano and calibrates to it", "[scoreview][listener]")
{
    SyntheticPianoConfig piano;
    piano.global_detune_cents = -35.0; // a neglected upright
    std::vector<float> buffer;
    const int scale[] = { 60, 62, 64, 65, 67, 69, 71, 72, 60, 64, 67, 72 };
    for (int i = 0; i < 12; ++i)
        synth_add_note(buffer, piano, 0.3 + i * 0.4, scale[i], 1.0, 0.7);
    buffer.resize(buffer.size() + 44100, 0.0f);

    NoteListener listener;
    const HarnessResult result = run_listener(
        listener, buffer, std::vector<int>(std::begin(scale), std::end(scale)));

    // Recognized from the very first note (tolerance windows)...
    REQUIRE(result.events.size() >= 11);
    CHECK(result.events[0].midi_pitch == 60);
    // ...and the calibration converges toward the true offset.
    CHECK(listener.global_offset_cents() < -12.0);
    CHECK(listener.note_offset_cents(60) == Catch::Approx(-35.0).margin(10.0));
}

TEST_CASE("listener recognizes inharmonic bass notes", "[scoreview][listener]")
{
    SyntheticPianoConfig piano;
    piano.b_bass = 6e-4; // stiffer than the listener's default model
    std::vector<float> buffer;
    synth_add_note(buffer, piano, 0.3, 33, 2.0, 0.8); // A1
    synth_add_note(buffer, piano, 1.1, 45, 2.0, 0.8); // A2
    buffer.resize(buffer.size() + 44100, 0.0f);

    NoteListener listener;
    const HarnessResult result = run_listener(listener, buffer, { 33, 45 });
    CHECK(has_pitch_near(result.events, 33, 0.3));
    CHECK(has_pitch_near(result.events, 45, 1.1));
}

TEST_CASE("listener ignores sustained ringing under a new onset", "[scoreview][listener]")
{
    SyntheticPianoConfig piano;
    std::vector<float> buffer;
    synth_add_note(buffer, piano, 0.3, 60, 3.0, 0.8); // C4 rings on...
    synth_add_note(buffer, piano, 1.2, 64, 1.5, 0.6); // ...E4 played later
    buffer.resize(buffer.size() + 44100, 0.0f);

    NoteListener listener;
    const HarnessResult result = run_listener(listener, buffer, { 60, 64 });
    DUMP_EVENTS(result);
    CHECK(has_pitch_near(result.events, 60, 0.3));
    CHECK(has_pitch_near(result.events, 64, 1.2));
    // The second onset must not re-report the still-ringing C4.
    CHECK(count_pitch(result.events, 60) == 1);
}

TEST_CASE("listener stays silent on noise and silence", "[scoreview][listener]")
{
    std::vector<float> buffer(44100 * 3, 0.0f);
    synth_add_noise(buffer, 0.004);

    NoteListener listener;
    const HarnessResult result = run_listener(listener, buffer, { 60 });
    CHECK(result.events.empty());
}

TEST_CASE("wav reader loads PCM16 stereo and float32 mono", "[scoreview][listener]")
{
    draxul::tests::TempDir dir("wav-reader");

    // Hand-crafted PCM16 stereo: frames (16384,-16384) and (0, 32767) at
    // 22050 Hz. Mono mix: 0.0 and ~0.5.
    const auto put16 = [](std::vector<uint8_t>& b, uint32_t v) {
        b.push_back(v & 0xFF);
        b.push_back((v >> 8) & 0xFF);
    };
    const auto put32 = [](std::vector<uint8_t>& b, uint32_t v) {
        b.push_back(v & 0xFF);
        b.push_back((v >> 8) & 0xFF);
        b.push_back((v >> 16) & 0xFF);
        b.push_back((v >> 24) & 0xFF);
    };
    const auto tag = [](std::vector<uint8_t>& b, const char* t) {
        b.insert(b.end(), t, t + 4);
    };
    std::vector<uint8_t> pcm;
    tag(pcm, "RIFF");
    put32(pcm, 36 + 8);
    tag(pcm, "WAVE");
    tag(pcm, "fmt ");
    put32(pcm, 16);
    put16(pcm, 1); // PCM
    put16(pcm, 2); // stereo
    put32(pcm, 22050);
    put32(pcm, 22050 * 4);
    put16(pcm, 4);
    put16(pcm, 16);
    tag(pcm, "data");
    put32(pcm, 8);
    put16(pcm, 16384);
    put16(pcm, static_cast<uint16_t>(-16384));
    put16(pcm, 0);
    put16(pcm, 32767);

    const std::string pcm_path = (dir.path / "pcm16.wav").string();
    FILE* f = std::fopen(pcm_path.c_str(), "wb");
    REQUIRE(f != nullptr);
    std::fwrite(pcm.data(), 1, pcm.size(), f);
    std::fclose(f);

    draxul::tests::WavData wav;
    REQUIRE(draxul::tests::read_wav(pcm_path, wav));
    CHECK(wav.sample_rate == 22050);
    REQUIRE(wav.samples.size() == 2);
    CHECK(wav.samples[0] == Catch::Approx(0.0).margin(1e-6));
    CHECK(wav.samples[1] == Catch::Approx(0.5).margin(1e-3));

    // Float32 mono with an extra chunk before data (readers must skip it).
    std::vector<uint8_t> f32;
    tag(f32, "RIFF");
    put32(f32, 4 + 24 + 12 + 16);
    tag(f32, "WAVE");
    tag(f32, "fmt ");
    put32(f32, 16);
    put16(f32, 3); // IEEE float
    put16(f32, 1);
    put32(f32, 44100);
    put32(f32, 44100 * 4);
    put16(f32, 4);
    put16(f32, 32);
    tag(f32, "LIST");
    put32(f32, 4);
    tag(f32, "INFO");
    tag(f32, "data");
    put32(f32, 8);
    const float values[2] = { -0.25f, 0.75f };
    uint32_t raw = 0;
    std::memcpy(&raw, &values[0], 4);
    put32(f32, raw);
    std::memcpy(&raw, &values[1], 4);
    put32(f32, raw);

    const std::string f32_path = (dir.path / "float32.wav").string();
    f = std::fopen(f32_path.c_str(), "wb");
    REQUIRE(f != nullptr);
    std::fwrite(f32.data(), 1, f32.size(), f);
    std::fclose(f);

    draxul::tests::WavData wav2;
    REQUIRE(draxul::tests::read_wav(f32_path, wav2));
    CHECK(wav2.sample_rate == 44100);
    REQUIRE(wav2.samples.size() == 2);
    CHECK(wav2.samples[0] == Catch::Approx(-0.25).margin(1e-6));
    CHECK(wav2.samples[1] == Catch::Approx(0.75).margin(1e-6));
}

TEST_CASE("listener handles fast repeated notes", "[scoreview][listener]")
{
    SyntheticPianoConfig piano;
    std::vector<float> buffer;
    for (int i = 0; i < 4; ++i)
        synth_add_note(buffer, piano, 0.3 + i * 0.18, 67, 0.5, 0.7); // ~5.5 notes/s
    buffer.resize(buffer.size() + 44100, 0.0f);

    NoteListener listener;
    const HarnessResult result = run_listener(listener, buffer, { 67 });
    DUMP_EVENTS(result);
    CHECK(count_pitch(result.events, 67) == 4);
}
