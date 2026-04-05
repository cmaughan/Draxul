#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{

class IHost;
class IFrameContext;

// A node in the render tree. Hosts are leaves; containers (workspaces) have
// children but no host. The tree is walked depth-first, pre-order. Hidden
// nodes and their entire subtree are skipped.
struct RenderNode
{
    IHost* host = nullptr; // if non-null, this node is drawable
    bool visible = true; // if false, skip this node and all children
    std::string tag; // optional label for debugging / lookup
    std::vector<RenderNode> children;
};

// Walk the tree depth-first, drawing every visible, running host.
void walk_draw(const RenderNode& node, IFrameContext& frame);

// Walk the tree depth-first, pumping every visible, running host.
void walk_pump(const RenderNode& node);

// Walk the tree and return the earliest next_deadline() across all visible,
// running hosts.
std::optional<std::chrono::steady_clock::time_point> walk_deadline(const RenderNode& node);

// Walk the tree and return true if any visible host reports is_running().
bool walk_any_running(const RenderNode& node);

} // namespace draxul
