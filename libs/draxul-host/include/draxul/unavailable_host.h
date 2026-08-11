#pragma once

#include <draxul/grid_host_base.h>

#include <string>
#include <string_view>

namespace draxul
{

// Inert projection for a client-local topology pane whose host provider is
// absent from this build. It deliberately remains a real grid host so the
// surrounding shared topology can continue to render and reconcile.
class UnavailableHost final : public GridHostBase
{
public:
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override;
    void pump() override;
    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    std::string status_text() const override;

protected:
    bool initialize_host() override;
    void on_viewport_changed() override;
    void on_font_metrics_changed_impl() override;
    std::string_view host_name() const override;

private:
    void render_message();

    bool running_ = false;
    std::string message_;
};

} // namespace draxul
