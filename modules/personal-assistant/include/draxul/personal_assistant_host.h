#pragma once

#include <draxul/grid_host_base.h>
#include <draxul/host_registry.h>
#include <draxul/personal_agent_store.h>

namespace draxul
{
// A lightweight entry point. Conversations themselves use the provider's terminal.
class PersonalAssistantHost : public GridHostBase
{
public:
    void shutdown() override { running_ = false; }
    bool is_running() const override { return running_; }
    std::string init_error() const override { return {}; }
    void pump() override;
    void on_key(const KeyEvent& event) override;
    bool dispatch_action(std::string_view action) override;
    void request_close() override { shutdown(); }
    std::string display_name() const override { return "Personal Assistant"; }
    std::string status_text() const override { return "Personal conversations"; }
private:
    bool initialize_host() override;
    void on_viewport_changed() override;
    void on_font_metrics_changed_impl() override { dirty_ = true; }
    std::string_view host_name() const override { return "Personal Assistant"; }
    void redraw();
    void write_line(int row, std::string_view text);
    std::shared_ptr<const PersonalAgentSnapshot> snapshot_;
    std::string selected_id_;
    bool running_ = false;
    bool dirty_ = true;
};
void register_personal_assistant_host_provider(HostProviderRegistry& registry);
}
