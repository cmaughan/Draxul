#include <draxul/unavailable_host.h>

#include <algorithm>

namespace draxul
{

void UnavailableHost::shutdown()
{
    running_ = false;
}

bool UnavailableHost::is_running() const
{
    return running_;
}

std::string UnavailableHost::init_error() const
{
    return {};
}

void UnavailableHost::pump()
{
}

bool UnavailableHost::dispatch_action(std::string_view)
{
    return false;
}

void UnavailableHost::request_close()
{
}

std::string UnavailableHost::status_text() const
{
    return message_;
}

bool UnavailableHost::initialize_host()
{
    const std::string& kind = launch_options().client_host_kind;
    const std::string subject = !launch_options().client_plugin_id.empty()
        ? "Plugin " + launch_options().client_plugin_id
        : (kind.empty() ? std::string("Host") : kind);
    message_ = detail_.empty()
        ? subject + " not available in this build"
        : subject + ": " + detail_;
    running_ = true;
    render_message();
    return true;
}

void UnavailableHost::on_viewport_changed()
{
    render_message();
}

void UnavailableHost::on_font_metrics_changed_impl()
{
    render_message();
}

std::string_view UnavailableHost::host_name() const
{
    return "Unavailable";
}

void UnavailableHost::render_message()
{
    const int cols = std::max(1, viewport().grid_size.x);
    const int rows = std::max(1, viewport().grid_size.y);
    apply_grid_size(cols, rows);
    grid().clear();
    const int row = rows / 2;
    const int start = std::max(0,
        (cols - static_cast<int>(message_.size())) / 2);
    const int count = std::min(
        static_cast<int>(message_.size()), cols - start);
    for (int i = 0; i < count; ++i)
    {
        grid().set_cell(start + i, row,
            std::string(1, message_[static_cast<size_t>(i)]),
            0, false);
    }
    flush_grid();
}

} // namespace draxul
