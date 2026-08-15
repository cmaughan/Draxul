#pragma once

namespace draxul
{
namespace scoreview
{

enum class MicPermission
{
    Pending, // consent dialog up / not yet answered — poll again
    Granted,
    Denied,
};

// Non-blocking microphone permission pre-flight. On macOS this queries TCC
// via AVFoundation and, when undetermined, kicks off the async consent
// request (once). SDL must never be the one to trigger the dialog: a
// TCC-blocked SDL_OpenAudioDeviceStream holds a device lock that deadlocks
// SDL_Quit's audio teardown. Non-Apple platforms report Granted.
MicPermission query_mic_permission();

} // namespace scoreview
} // namespace draxul
