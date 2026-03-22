#pragma once

#include <draxul/host.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace draxul
{

class IWindow;
class IGridRenderer;
class IImGuiHost;
class TextService;
struct AppOptions;
struct AppConfig;

// Owns the IHost instance(s) and manages their lifecycle: creation, initialisation,
// and shutdown. App holds a HostManager and calls create() during initialisation.
// Supports multiple host slots for multi-pane split layouts.
class HostManager
{
public:
    struct Deps
    {
        const AppOptions* options = nullptr;
        const AppConfig* config = nullptr;
        IWindow* window = nullptr;
        IGridRenderer* grid_renderer = nullptr;
        IImGuiHost* imgui_host = nullptr;
        TextService* text_service = nullptr;
        const float* display_ppi = nullptr;

        // Provides the initial viewport at creation time
        std::function<HostViewport()> get_viewport;
    };

    struct HostSlot
    {
        std::unique_ptr<IHost> host;
        int pane_id = 0;
    };

    explicit HostManager(Deps deps);

    // Creates and initialises the primary host (slot 0). Returns false on failure;
    // error() contains the reason. Caller passes in the host callbacks so App can
    // keep the lambda captures.
    bool create(HostCallbacks callbacks);

    // Creates and initialises an additional host slot with a specific pane_id and viewport.
    // Returns false on failure; error() contains the reason.
    bool add_slot(HostCallbacks callbacks, int pane_id, HostViewport viewport);

    // Shuts down and releases all hosts.
    void shutdown();

    // Returns null before create() succeeds or after shutdown().
    IHost* host() const
    {
        return focused_host();
    }

    IHost* focused_host() const
    {
        if (focused_slot_ < 0 || focused_slot_ >= static_cast<int>(slots_.size()))
            return nullptr;
        return slots_[static_cast<size_t>(focused_slot_)].host.get();
    }

    IHost* host_for_pane(int pane_id) const
    {
        for (const auto& slot : slots_)
        {
            if (slot.pane_id == pane_id && slot.host)
                return slot.host.get();
        }
        return nullptr;
    }

    void set_focused_slot(int index)
    {
        focused_slot_ = index;
    }

    int focused_slot_index() const
    {
        return focused_slot_;
    }

    int pane_id_for_slot(int index) const
    {
        if (index < 0 || index >= static_cast<int>(slots_.size()))
            return 0;
        return slots_[static_cast<size_t>(index)].pane_id;
    }

    int slot_count() const
    {
        return static_cast<int>(slots_.size());
    }

    IHost* host_at(int index) const
    {
        if (index < 0 || index >= static_cast<int>(slots_.size()))
            return nullptr;
        return slots_[static_cast<size_t>(index)].host.get();
    }

    // Transfers ownership of a new host into the manager (replaces focused slot).
    void set_host(std::unique_ptr<IHost> h)
    {
        if (slots_.empty())
            slots_.push_back({ std::move(h), 0 });
        else
            slots_[static_cast<size_t>(focused_slot_)].host = std::move(h);
    }

    const std::string& error() const
    {
        return error_;
    }

private:
    bool create_slot(HostCallbacks callbacks, int pane_id, HostViewport viewport,
        HostLaunchOptions launch, bool is_primary);

    Deps deps_;
    std::vector<HostSlot> slots_;
    int focused_slot_ = 0;
    std::string error_;
};

} // namespace draxul
