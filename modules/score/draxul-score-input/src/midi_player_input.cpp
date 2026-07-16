#include <draxul/scoreview/midi_player_input.h>

#include <draxul/log.h>

#include <RtMidi.h>

namespace draxul
{
namespace scoreview
{

namespace
{
// The RtMidi callback trampoline: `user` is the MidiPlayerInput.
void rtmidi_trampoline(double /*dt*/, std::vector<unsigned char>* message, void* user)
{
    if (message != nullptr && user != nullptr)
        static_cast<MidiPlayerInput*>(user)->feed(message->data(), message->size());
}
} // namespace

std::vector<std::string> MidiPlayerInput::list_ports()
{
    std::vector<std::string> names;
    try
    {
        RtMidiIn probe;
        const unsigned count = probe.getPortCount();
        names.reserve(count);
        for (unsigned port = 0; port < count; ++port)
            names.push_back(probe.getPortName(port));
    }
    catch (const std::exception& error)
    {
        // Includes RtMidiError — e.g. a transient CoreMIDI client-create
        // failure (-304) right after a relaunch. No devices this call; the
        // next combo open retries.
        DRAXUL_LOG_WARN(LogCategory::App, "midi: port enumeration failed: %s", error.what());
    }
    catch (...)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "midi: port enumeration failed (unknown error)");
    }
    return names;
}

MidiPlayerInput::MidiPlayerInput(int port)
{
    if (port == kNoDevice)
        return; // test instance: feed() only
    try
    {
        in_ = std::make_unique<RtMidiIn>();
        const unsigned count = in_->getPortCount();
        if (port < 0 || static_cast<unsigned>(port) >= count)
        {
            error_ = "MIDI port out of range";
            in_.reset();
            return;
        }
        port_name_ = in_->getPortName(static_cast<unsigned>(port));
        in_->openPort(static_cast<unsigned>(port));
        in_->ignoreTypes(/*sysex=*/true, /*time=*/true, /*sense=*/true);
        in_->setCallback(&rtmidi_trampoline, this);
        ok_ = true;
        DRAXUL_LOG_INFO(LogCategory::App, "midi: listening on '%s'", port_name_.c_str());
    }
    catch (const std::exception& error)
    {
        error_ = error.what();
        in_.reset();
    }
    catch (...)
    {
        error_ = "unknown MIDI error";
        in_.reset();
    }
}

MidiPlayerInput::~MidiPlayerInput()
{
    // Cancel the callback BEFORE members die: the backend thread must not
    // enter feed() during destruction. RtMidi calls can throw (its error()
    // raises RtMidiError) and a throwing destructor is std::terminate —
    // swallow everything.
    try
    {
        if (in_)
        {
            in_->cancelCallback();
            in_->closePort();
        }
    }
    catch (...)
    {
    }
}

void MidiPlayerInput::feed(const unsigned char* data, size_t size)
{
    if (data == nullptr || size < 3)
        return;
    const unsigned char status = data[0] & 0xF0u;
    if (status != 0x90u && status != 0x80u)
        return; // channel/CC/pitch-bend etc — not judging currency (yet)
    const int pitch = data[1] & 0x7F;
    const int raw_velocity = data[2] & 0x7F;
    Pending pending;
    pending.event.midi_pitch = pitch;
    pending.event.velocity = static_cast<float>(raw_velocity) / 127.0f;
    // Note-on with velocity 0 is a note-off by long MIDI convention.
    pending.event.on = status == 0x90u && raw_velocity > 0;
    pending.at = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(pending);
}

void MidiPlayerInput::poll(double t_now_seconds, std::vector<PlayerNoteEvent>& out)
{
    std::vector<Pending> taken;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        taken.swap(pending_);
    }
    if (taken.empty())
        return;
    const auto now = std::chrono::steady_clock::now();
    for (const Pending& pending : taken)
    {
        if (pending.event.on)
        {
            PlayerNoteEvent event;
            event.midi_pitch = pending.event.midi_pitch;
            // Arrival-time accurate: map the callback's steady-clock stamp
            // into the caller's seconds domain.
            event.t_seconds
                = t_now_seconds - std::chrono::duration<double>(now - pending.at).count();
            event.velocity = pending.event.velocity;
            out.push_back(event);
        }
        voice_out_.push_back(pending.event);
    }
}

void MidiPlayerInput::take_voice_events(std::vector<MidiVoiceEvent>& out)
{
    out.insert(out.end(), voice_out_.begin(), voice_out_.end());
    voice_out_.clear();
}

} // namespace scoreview
} // namespace draxul
