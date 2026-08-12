#include <draxul/scoreview/score_device_lease.h>

#include <mutex>
#include <unordered_map>
#include <utility>

namespace draxul::scoreview
{
namespace
{

struct RegistryState
{
    std::mutex mutex;
    std::unordered_map<std::string, const void*> owners;
};

const char* kind_name(ScoreDeviceKind kind)
{
    switch (kind)
    {
    case ScoreDeviceKind::AudioOutput:
        return "audio output";
    case ScoreDeviceKind::Microphone:
        return "microphone";
    case ScoreDeviceKind::MidiInput:
        return "MIDI input";
    }
    return "device";
}

std::string device_key(ScoreDeviceKind kind, std::string_view name)
{
    return std::to_string(static_cast<int>(kind)) + ":" + std::string(name);
}

class Lease final : public IScoreDeviceLease
{
public:
    Lease(std::shared_ptr<RegistryState> state, std::string key,
        const void* owner)
        : state_(std::move(state)), key_(std::move(key)), owner_(owner)
    {
    }

    ~Lease() override
    {
        std::scoped_lock lock(state_->mutex);
        const auto found = state_->owners.find(key_);
        if (found != state_->owners.end() && found->second == owner_)
            state_->owners.erase(found);
    }

private:
    std::shared_ptr<RegistryState> state_;
    std::string key_;
    const void* owner_ = nullptr;
};

class Provider final : public IScoreDeviceLeaseProvider
{
public:
    Provider() : state_(std::make_shared<RegistryState>()) {}

    ScoreDeviceLeaseResult acquire(ScoreDeviceKind kind,
        std::string_view device_name, const void* owner) override
    {
        if (!owner || device_name.empty())
            return { {}, "invalid ScoreView device lease request" };
        const std::string key = device_key(kind, device_name);
        {
            std::scoped_lock lock(state_->mutex);
            if (state_->owners.contains(key))
            {
                return { {}, std::string(kind_name(kind)) + " '"
                    + std::string(device_name)
                    + "' is already in use by another ScoreView pane" };
            }
            state_->owners.emplace(key, owner);
        }
        return { std::make_unique<Lease>(state_, key, owner), {} };
    }

private:
    std::shared_ptr<RegistryState> state_;
};

} // namespace

std::shared_ptr<IScoreDeviceLeaseProvider> create_score_device_lease_provider()
{
    return std::make_shared<Provider>();
}

std::shared_ptr<IScoreDeviceLeaseProvider> process_score_device_leases()
{
    static const std::shared_ptr<IScoreDeviceLeaseProvider> provider
        = create_score_device_lease_provider();
    return provider;
}

} // namespace draxul::scoreview
