#pragma once

#include <memory>
#include <string>
#include <string_view>

namespace draxul::scoreview
{

enum class ScoreDeviceKind
{
    AudioOutput,
    Microphone,
    MidiInput,
};

class IScoreDeviceLease
{
public:
    virtual ~IScoreDeviceLease() = default;
};

struct ScoreDeviceLeaseResult
{
    std::unique_ptr<IScoreDeviceLease> lease;
    std::string error;
};

class IScoreDeviceLeaseProvider
{
public:
    virtual ~IScoreDeviceLeaseProvider() = default;
    virtual ScoreDeviceLeaseResult acquire(ScoreDeviceKind kind,
        std::string_view device_name, const void* owner) = 0;
};

// The shared registry used by every ScoreView instance in one UI process.
// A separate factory is exposed for deterministic contention tests.
std::shared_ptr<IScoreDeviceLeaseProvider> process_score_device_leases();
std::shared_ptr<IScoreDeviceLeaseProvider> create_score_device_lease_provider();

} // namespace draxul::scoreview
