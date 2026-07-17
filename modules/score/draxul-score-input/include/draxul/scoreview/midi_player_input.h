#pragma once

#include <draxul/scoreview/player_input.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class RtMidiIn; // RtMidi stays private to the .cpp

namespace draxul
{
namespace scoreview
{

// A note-on/off with velocity for the play-thru instrument voice. Judging
// consumes only the note-ons (through IPlayerInput::poll); the voice wants
// the releases too so held notes damp like a real piano.
struct MidiVoiceEvent
{
    int midi_pitch = -1;
    float velocity = 0.0f; // 0..1
    bool on = false;
};

// A hardware MIDI keyboard as the runner's input (plans/scoreview.md): the
// judging currency is identical to the microphone listener's — MIDI pitch +
// steady-clock seconds — but lossless and latency-free, so the composer can
// be tuned against ground-truth input before the acoustic path is trusted.
//
// RtMidi delivers messages on a backend thread; they queue under a mutex
// (stamped with the arrival steady-clock time) and drain on the main thread
// in poll(), mapped into the caller's now_seconds domain.
class MidiPlayerInput final : public IPlayerInput
{
public:
    // Names of the available MIDI input ports, index-aligned with the port
    // number open() expects. Empty when MIDI is unavailable.
    static std::vector<std::string> list_ports();

    // Pass kNoDevice to build a device-free instance (unit tests drive
    // feed() directly; ok() stays false).
    static constexpr int kNoDevice = -1;
    explicit MidiPlayerInput(int port);
    ~MidiPlayerInput() override;

    bool ok() const
    {
        return ok_;
    }
    const std::string& port_name() const
    {
        return port_name_;
    }
    const std::string& error() const
    {
        return error_;
    }

    // Judging stream: note-ons since the last poll, timestamped in the
    // caller's clock domain (arrival-time accurate, not poll-time).
    void poll(double t_now_seconds, std::vector<PlayerNoteEvent>& out) override;

    // Voice stream: every on/off since the last call (velocity preserved).
    // Drain AFTER poll() — poll is what migrates the pending queue.
    void take_voice_events(std::vector<MidiVoiceEvent>& out);

    // Parses one raw MIDI message — the RtMidi callback body, public and
    // device-free so tests can inject bytes. Understands note-on (velocity 0
    // = off, per convention) and note-off on any channel; everything else is
    // ignored for now.
    void feed(const unsigned char* data, size_t size);

    // Backlog bound: the backend queue never exceeds this many events. On
    // overload the OLDEST events drop first (fresh input reflects the
    // keyboard's current state; a stale backlog does not) and poll() logs
    // the drop count once, off the realtime thread.
    static constexpr size_t kMaxPendingEvents = 4096;

private:
    struct Pending
    {
        MidiVoiceEvent event;
        std::chrono::steady_clock::time_point at;
    };

    std::unique_ptr<RtMidiIn> in_;
    bool ok_ = false;
    std::string port_name_;
    std::string error_;

    std::mutex mutex_;
    std::vector<Pending> pending_; // filled by feed() (backend thread), bounded
    size_t dropped_events_ = 0; // overload drops since the last poll()
    std::vector<MidiVoiceEvent> voice_out_; // drained on the main thread
};

} // namespace scoreview
} // namespace draxul
