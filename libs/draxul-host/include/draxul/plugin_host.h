#pragma once

#include <draxul/host.h>
#include <draxul/plugin_api.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

namespace draxul
{

class IRenderPass;
class LoadedPlugin;
class PluginManager;

class PluginHost final : public IHost
{
public:
    explicit PluginHost(std::shared_ptr<PluginManager> manager,
        std::filesystem::path storage_root = {});
    ~PluginHost() override;

    bool initialize(const HostContext& context, IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override { return running_; }
    std::string init_error() const override { return error_; }
    std::string init_error_code() const override { return "plugin_load_failed"; }
    void set_viewport(const HostViewport& viewport) override;
    void set_presentation_visible(bool visible) override;
    bool requires_periodic_wake() const override { return false; }
    void pump() override;
    void draw(IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;
    void on_focus_gained() override;
    void on_focus_lost() override;
    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;
    void on_text_editing(const TextEditingEvent& event) override;
    void on_mouse_button(const MouseButtonEvent& event) override;
    void on_mouse_move(const MouseMoveEvent& event) override;
    void on_mouse_wheel(const MouseWheelEvent& event) override;
    std::optional<MouseCursor> mouse_cursor_at(int px, int py) const override;
    bool dispatch_action(std::string_view action) override;
    void request_close() override { running_ = false; }
    std::string display_name() const override;
    std::string status_text() const override;
    Color default_background() const override;
    HostRuntimeState runtime_state() const override;
    HostDebugState debug_state() const override;
    HostPrintHint print_hint() const override;
    void attach_imgui_host(IImGuiHost& host) override;
    void set_imgui_font(const std::string& path,
        float size_pixels) override;

    void accept_render_result(const DraxulPluginRenderResultV2& result);

private:
    struct PresentationSnapshot
    {
        std::string display_name;
        std::string status_text;
        Color background{ 0.04f, 0.05f, 0.08f, 1.0f };
        bool content_ready = true;
        uint32_t mouse_cursor = DRAXUL_PLUGIN_CURSOR_UNSPECIFIED;
        HostPrintHint print_hint;
    };

    static void request_redraw(void* context);
    static void request_tick(void* context);
    static void notify_presentation_changed(void* context);
    static void log_message(void* context, uint32_t level, const char* message, size_t length);
    static int32_t query_service(void* context, const char* service_id,
        size_t service_id_length, uint32_t requested_version,
        void* service_table, size_t service_table_size);
    static int32_t get_service_path(void* context, uint32_t path_kind,
        char* buffer, size_t* in_out_size);
    static int32_t get_recommended_font(void* context, char* path,
        size_t* in_out_size, float* size_pixels, float* display_scale,
        uint64_t* generation);
    static uint32_t read_storage_json(void* context, uint32_t scope,
        const char* key, size_t key_length, char* buffer,
        size_t* in_out_size);
    static uint32_t write_storage_json(void* context, uint32_t scope,
        const char* key, size_t key_length, const char* json,
        size_t json_length);
    static uint32_t remove_storage(void* context, uint32_t scope,
        const char* key, size_t key_length);
    DraxulPluginViewportV2 plugin_viewport() const;
    void send_input(DraxulPluginInputEventV2 event);
    void send_focus(bool focused);
    void run_tick(std::chrono::steady_clock::time_point now,
        bool& frame_needed);
    std::optional<PresentationSnapshot> presentation_snapshot() const;
    void initialize_service_paths();
    std::filesystem::path storage_path(uint32_t scope,
        std::string_view key) const;

    std::shared_ptr<PluginManager> manager_;
    std::shared_ptr<LoadedPlugin> plugin_;
    std::unique_ptr<IRenderPass> render_pass_;
    IGridRenderer* renderer_ = nullptr;
    std::atomic<IHostCallbacks*> callbacks_{ nullptr };
    void* instance_ = nullptr;
    HostViewport viewport_;
    DraxulPluginHostApiV2 host_api_{};
    DraxulPluginPresentationExtensionV2 presentation_{};
    std::atomic<bool> redraw_pending_{ false };
    std::atomic<bool> tick_pending_{ false };
    std::atomic<bool> presentation_pending_{ false };
    std::atomic<bool> shutting_down_{ false };
    std::optional<std::chrono::steady_clock::time_point> next_frame_;
    std::optional<std::chrono::steady_clock::time_point> next_tick_;
    std::chrono::steady_clock::time_point started_at_{};
    std::string plugin_id_;
    std::string pane_id_;
    std::string plugin_directory_;
    std::string config_json_;
    std::string error_;
    bool running_ = false;
    bool visible_ = true;
    bool focused_ = false;
    bool has_presentation_ = false;
    std::filesystem::path storage_root_override_;
    std::filesystem::path resource_path_;
    std::filesystem::path config_path_;
    std::filesystem::path data_path_;
    std::filesystem::path cache_path_;
    std::filesystem::path temporary_path_;
    std::thread::id main_thread_id_;
    std::string imgui_font_path_;
    float imgui_font_size_pixels_ = 13.0f;
    float display_ppi_ = 96.0f;
    uint64_t ui_style_generation_ = 1;
};

class HostProviderRegistry;
void register_plugin_host_provider(HostProviderRegistry& registry,
    std::shared_ptr<PluginManager> manager);

} // namespace draxul
