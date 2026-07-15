// MIDI input + soundfont voice (plans/scoreview.md): the MIDI message
// parsing and event plumbing are device-free (feed() injects raw bytes), and
// the soundfont synth renders offline from the fetched YDP grand.

#include <catch2/catch_all.hpp>

#include <draxul/scoreview/midi_player_input.h>
#include <draxul/scoreview/soundfont_synth.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace draxul::scoreview;

namespace
{

double block_energy(const float* samples, size_t count)
{
    double energy = 0.0;
    for (size_t i = 0; i < count; ++i)
        energy += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
    return energy;
}

std::string ydp_soundfont_path()
{
    for (const auto& entry : std::filesystem::directory_iterator(DRAXUL_SOUNDFONT_DIR))
    {
        if (entry.path().extension() == ".sf2")
            return entry.path().string();
    }
    return {};
}

} // namespace

TEST_CASE("midi input parses note messages into judge and voice streams", "[scoreview][midi]")
{
    MidiPlayerInput midi(MidiPlayerInput::kNoDevice);
    CHECK_FALSE(midi.ok()); // no device — feed() drives it

    const unsigned char on_c4[] = { 0x90, 60, 100 };
    const unsigned char on_e4_ch3[] = { 0x92, 64, 64 }; // channel is irrelevant
    const unsigned char off_c4[] = { 0x80, 60, 0 };
    const unsigned char on_vel0_e4[] = { 0x92, 64, 0 }; // note-on vel 0 == off
    const unsigned char pitch_bend[] = { 0xE0, 0x00, 0x40 }; // ignored
    const unsigned char too_short[] = { 0x90, 60 };
    midi.feed(on_c4, 3);
    midi.feed(on_e4_ch3, 3);
    midi.feed(off_c4, 3);
    midi.feed(on_vel0_e4, 3);
    midi.feed(pitch_bend, 3);
    midi.feed(too_short, 2);

    // Judging sees the two strikes only, timestamped at/before "now".
    std::vector<PlayerNoteEvent> events;
    midi.poll(10.0, events);
    REQUIRE(events.size() == 2);
    CHECK(events[0].midi_pitch == 60);
    CHECK(events[0].velocity == Catch::Approx(100.0f / 127.0f));
    CHECK(events[0].t_seconds <= 10.0);
    CHECK(events[0].t_seconds > 9.0); // arrival was microseconds ago, not sec
    CHECK(events[1].midi_pitch == 64);

    // The voice stream carries ons AND offs, in order.
    std::vector<MidiVoiceEvent> voiced;
    midi.take_voice_events(voiced);
    REQUIRE(voiced.size() == 4);
    CHECK(voiced[0].on);
    CHECK(voiced[1].on);
    CHECK_FALSE(voiced[2].on);
    CHECK(voiced[2].midi_pitch == 60);
    CHECK_FALSE(voiced[3].on); // the vel-0 note-on
    CHECK(voiced[3].midi_pitch == 64);

    // Drained: nothing repeats.
    events.clear();
    voiced.clear();
    midi.poll(11.0, events);
    midi.take_voice_events(voiced);
    CHECK(events.empty());
    CHECK(voiced.empty());
}

TEST_CASE("soundfont synth renders the YDP grand and damps on release", "[scoreview][midi]")
{
    const std::string path = ydp_soundfont_path();
    REQUIRE_FALSE(path.empty());

    SoundFontSynth piano;
    std::string error;
    REQUIRE(piano.load(path, 44100, error));
    CHECK(piano.loaded());

    // Silence before any note.
    std::vector<float> block(4410);
    piano.render(block.data(), block.size());
    CHECK(block_energy(block.data(), block.size()) == Catch::Approx(0.0));

    // A middle-C strike makes sound, placed at its scheduled sample.
    piano.schedule_note(piano.cursor() + 100, 60, 0.8f);
    piano.render(block.data(), block.size());
    const double struck = block_energy(block.data(), block.size());
    CHECK(struck > 1e-4);
    CHECK(block[50] == 0.0f); // strictly silent before the scheduled strike

    // Release, then let the damper work: the tail dies away.
    piano.schedule_off(piano.cursor(), 60);
    std::vector<float> tail(44100);
    piano.render(tail.data(), tail.size());
    const double early = block_energy(tail.data(), 4410);
    const double late = block_energy(tail.data() + tail.size() - 4410, 4410);
    CHECK(late < early * 0.5);

    // Load failure reports and keeps the previous font.
    SoundFontSynth bad;
    CHECK_FALSE(bad.load("/nonexistent/font.sf2", 44100, error));
    CHECK_FALSE(bad.loaded());
    CHECK_FALSE(error.empty());
}
