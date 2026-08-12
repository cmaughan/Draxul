#include <draxul/unavailable_host.h>

#include <algorithm>
#include <vector>

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

    const size_t line_width = static_cast<size_t>(std::max(1, cols - 4));
    std::vector<std::string_view> lines;
    size_t offset = 0;
    while (offset < message_.size() && lines.size() < static_cast<size_t>(rows))
    {
        while (offset < message_.size() && message_[offset] == ' ')
            ++offset;
        size_t end = std::min(message_.size(), offset + line_width);
        if (end < message_.size())
        {
            const size_t break_at = message_.rfind(' ', end);
            if (break_at != std::string::npos && break_at > offset)
                end = break_at;
        }
        lines.emplace_back(message_.data() + offset, end - offset);
        offset = end;
    }

    const int first_row = std::max(0,
        (rows - static_cast<int>(lines.size())) / 2);
    for (size_t line_index = 0; line_index < lines.size(); ++line_index)
    {
        const std::string_view line = lines[line_index];
        const int start = std::max(0,
            (cols - static_cast<int>(line.size())) / 2);
        for (size_t i = 0; i < line.size(); ++i)
        {
            grid().set_cell(start + static_cast<int>(i),
                first_row + static_cast<int>(line_index),
                std::string(1, line[i]), 0, false);
        }
    }
    flush_grid();
}

} // namespace draxul
