#pragma once
#include <draxul/grid_host_base.h>
#include <draxul/host_registry.h>
#include <draxul/nanovg_pass.h>
#include <draxul/personal_agent_store.h>
#include <draxul/personal_chat.h>
#include <draxul/markdown/markdown_render_pass.h>
#include <unordered_map>

namespace draxul
{
namespace personal_detail { class ChatImages; }
class PersonalAssistantHost : public GridHostBase
{
public:
    PersonalAssistantHost();
    ~PersonalAssistantHost() override;
    bool initialize(const HostContext& context,IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override { return running_; }
    std::string init_error() const override { return init_error_; }
    void pump() override;
    void draw(IFrameContext& frame) override;
    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;
    void on_text_editing(const TextEditingEvent& event) override;
    void on_mouse_button(const MouseButtonEvent& event) override;
    void on_mouse_wheel(const MouseWheelEvent& event) override;
    std::optional<MouseCursor> mouse_cursor_at(int px,int py) const override;
    bool dispatch_action(std::string_view action) override;
    void request_close() override { shutdown(); }
    std::string display_name() const override { return name_; }
    std::string status_text() const override;
private:
    bool initialize_host() override;
    void on_viewport_changed() override;
    void on_font_metrics_changed_impl() override { dirty_=true; }
    std::string_view host_name() const override { return "Personal Chat"; }
    void paint(NVGcontext* vg,int width,int height);
    std::string_view link_at(int px,int py) const;
    void insert(std::string_view text);
    void send();
    void changed();
    void write_line(int row,std::string_view text);
    std::shared_ptr<const PersonalAgentSnapshot> snapshot_;
    std::shared_ptr<const PersonalChatSnapshot> chat_;
    std::unique_ptr<INanoVGPass> pass_;
    std::unique_ptr<personal_detail::ChatImages> images_;
    RichTextService rich_text_;
    std::unique_ptr<markdown::MarkdownRenderPass> markdown_pass_;
    markdown::MarkdownTheme markdown_theme_;
    markdown::LayoutDocument markdown_frame_;
    RenderViewport markdown_viewport_{};
    struct CachedMarkdown { std::string source; float width=0,scale=0; markdown::LayoutDocument layout; };
    std::unordered_map<std::string,CachedMarkdown> markdown_cache_;
    float markdown_point_size_=0,display_ppi_=96;
    uint64_t markdown_revision_=0;
    std::string init_error_,nanovg_font_path_;
    std::string selected_id_,name_="Personal Agents",draft_,preedit_,error_;
    std::string submitted_draft_, submitted_id_;
    size_t cursor_=0;
    bool select_all_=false,running_=false,dirty_=true;
    float scroll_=0,max_scroll_=0;
    struct Hit { glm::vec4 rect; std::string action,text; };
    std::vector<Hit> hits_;
};
void register_personal_assistant_host_provider(HostProviderRegistry& registry);
}
