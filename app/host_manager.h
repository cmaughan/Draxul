#pragma once

#include <draxul/host.h>
#include <functional>
#include <memory>
#include <string>

namespace draxul
{

class IWindow;
class IGridRenderer;
class IImGuiHost;
class TextService;
struct AppOptions;
struct AppConfig;

// Owns the IHost instance and manages its lifecycle: creation, initialisation,
// and shutdown. App holds a HostManager and calls create() during initialisation.
// All other subsystems access the host through host().
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

    explicit HostManager(Deps deps);

    // Creates and initialises the host. Returns false on failure; error() contains the
    // reason. Caller passes in the host callbacks so App can keep the lambda captures.
    bool create(HostCallbacks callbacks);

    // Shuts down and releases the host.
    void shutdown();

    // Returns null before create() succeeds or after shutdown().
    IHost* host() const
    {
        return host_.get();
    }

    // Transfers ownership of a new host into the manager.
    void set_host(std::unique_ptr<IHost> h)
    {
        host_ = std::move(h);
    }

    const std::string& error() const
    {
        return error_;
    }

private:
    Deps deps_;
    std::unique_ptr<IHost> host_;
    std::string error_;
};

} // namespace draxul
