#include "render_tree.h"

#include <draxul/host.h>

namespace draxul
{

void walk_draw(const RenderNode& node, IFrameContext& frame)
{
    if (!node.visible)
        return;
    if (node.host && node.host->is_running())
        node.host->draw(frame);
    for (const auto& child : node.children)
        walk_draw(child, frame);
}

void walk_pump(const RenderNode& node)
{
    if (!node.visible)
        return;
    if (node.host && node.host->is_running())
        node.host->pump();
    for (const auto& child : node.children)
        walk_pump(child);
}

std::optional<std::chrono::steady_clock::time_point> walk_deadline(const RenderNode& node)
{
    if (!node.visible)
        return std::nullopt;

    std::optional<std::chrono::steady_clock::time_point> earliest;
    if (node.host)
    {
        auto d = node.host->next_deadline();
        if (d && (!earliest || *d < *earliest))
            earliest = d;
    }
    for (const auto& child : node.children)
    {
        auto d = walk_deadline(child);
        if (d && (!earliest || *d < *earliest))
            earliest = d;
    }
    return earliest;
}

bool walk_any_running(const RenderNode& node)
{
    if (!node.visible)
        return false;
    if (node.host && node.host->is_running())
        return true;
    for (const auto& child : node.children)
    {
        if (walk_any_running(child))
            return true;
    }
    return false;
}

} // namespace draxul
