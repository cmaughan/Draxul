#include "split_tree.h"

#include <algorithm>
#include <cmath>
#include <draxul/log.h>
#include <draxul/perf_timing.h>

namespace draxul
{

// ---------------------------------------------------------------------------
// Node — variant of Leaf (terminal pane) or Split (binary partition)
// ---------------------------------------------------------------------------
struct SplitTree::Node
{
    struct LeafData
    {
        LeafId id = kInvalidLeaf;
        PaneDescriptor descriptor{};
    };
    struct SplitData
    {
        DividerId divider_id = kInvalidDivider;
        SplitDirection direction = SplitDirection::Vertical;
        float ratio = 0.5f;
        std::unique_ptr<Node> first; // left or top
        std::unique_ptr<Node> second; // right or bottom
        // Divider rect, computed during recompute().
        int div_x = 0;
        int div_y = 0;
        int div_w = 0;
        int div_h = 0;
    };

    std::variant<LeafData, SplitData> data;

    bool is_leaf() const
    {
        return std::holds_alternative<LeafData>(data);
    }
    LeafData& leaf()
    {
        return std::get<LeafData>(data);
    }
    const LeafData& leaf() const
    {
        return std::get<LeafData>(data);
    }
    SplitData& split()
    {
        return std::get<SplitData>(data);
    }
    const SplitData& split() const
    {
        return std::get<SplitData>(data);
    }
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
SplitTree::SplitTree() = default;
SplitTree::~SplitTree() = default;
SplitTree::SplitTree(SplitTree&&) noexcept = default;
SplitTree& SplitTree::operator=(SplitTree&&) noexcept = default;

// ---------------------------------------------------------------------------
// Core operations
// ---------------------------------------------------------------------------
LeafId SplitTree::reset(int pixel_w, int pixel_h)
{
    PERF_MEASURE();
    next_id_ = 0;
    next_divider_id_ = 0;
    root_ = std::make_unique<Node>();
    root_->data = Node::LeafData{ next_id_++, { { 0, 0 }, { pixel_w, pixel_h } } };
    focused_id_ = 0;
    total_w_ = pixel_w;
    total_h_ = pixel_h;
    return 0;
}

LeafId SplitTree::split_leaf(LeafId id, SplitDirection dir)
{
    PERF_MEASURE();
    Node* target = find_leaf_node(id);
    if (!target)
        return kInvalidLeaf;

    const LeafId new_id = next_id_;
    ++next_id_;
    const LeafId existing_id = target->leaf().id;

    auto first_child = std::make_unique<Node>();
    first_child->data = Node::LeafData{ existing_id, {} };

    auto second_child = std::make_unique<Node>();
    second_child->data = Node::LeafData{ new_id, {} };

    Node::SplitData split_data;
    split_data.divider_id = next_divider_id_++;
    split_data.direction = dir;
    split_data.ratio = 0.5f;
    split_data.first = std::move(first_child);
    split_data.second = std::move(second_child);
    target->data = std::move(split_data);

    recompute(total_w_, total_h_);
    return new_id;
}

bool SplitTree::close_leaf(LeafId id)
{
    PERF_MEASURE();
    if (leaf_count() <= 1)
        return false;

    Node* target = find_leaf_node(id);
    if (!target)
        return false;

    Node* parent = find_parent_of(target);
    if (!parent || parent->is_leaf())
        return false;

    auto& s = parent->split();
    const bool closing_first = (s.first.get() == target);

    // Pick next focus before we destroy nodes.
    LeafId new_focus = focused_id_;
    if (focused_id_ == id)
    {
        const Node* sibling = closing_first ? s.second.get() : s.first.get();
        new_focus = first_leaf(sibling);
    }

    // Detach the sibling subtree.
    auto sibling = closing_first ? std::move(s.second) : std::move(s.first);

    // Replace the parent with the sibling in the tree.
    if (parent == root_.get())
    {
        root_ = std::move(sibling);
    }
    else
    {
        Node* grandparent = find_parent_of(parent);
        if (grandparent && !grandparent->is_leaf())
        {
            auto& gs = grandparent->split();
            if (gs.first.get() == parent)
                gs.first = std::move(sibling);
            else
                gs.second = std::move(sibling);
        }
    }

    focused_id_ = new_focus;
    recompute(total_w_, total_h_);
    return true;
}

void SplitTree::recompute(int pixel_w, int pixel_h)
{
    PERF_MEASURE();
    total_w_ = pixel_w;
    total_h_ = pixel_h;
    if (root_)
        recompute_node(root_.get(), 0, 0, pixel_w, pixel_h, kDividerWidth);
}

SplitTree::HitResult SplitTree::hit_test(int px, int py) const
{
    PERF_MEASURE();
    if (!root_)
        return std::monostate{};
    return hit_test_node(root_.get(), px, py, kDividerWidth);
}

void SplitTree::set_divider_ratio(DividerId id, float ratio)
{
    PERF_MEASURE();
    if (id == kInvalidDivider)
        return;

    if (auto* node = find_divider_node(id); !node)
    {
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "Ignoring stale divider id %d during ratio update", id);
        return;
    }
    else if (node->is_leaf())
        return;
    else
        node->split().ratio = std::clamp(ratio, 0.1f, 0.9f);
    recompute(total_w_, total_h_);
}

void SplitTree::set_focused(LeafId id)
{
    PERF_MEASURE();
    if (find_leaf_node(id))
        focused_id_ = id;
}

PaneDescriptor SplitTree::descriptor_for(LeafId id) const
{
    PERF_MEASURE();
    if (const Node* node = find_leaf_node(id); node)
        return node->leaf().descriptor;
    return {};
}

void SplitTree::for_each_leaf(
    const std::function<void(LeafId, const PaneDescriptor&)>& fn) const
{
    PERF_MEASURE();
    if (root_)
        visit_leaves(root_.get(), fn);
}

int SplitTree::leaf_count() const
{
    PERF_MEASURE();
    return root_ ? count_leaves(root_.get()) : 0;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
const SplitTree::Node* SplitTree::find_leaf_node(LeafId id) const
{
    std::function<const Node*(const Node*)> search = [&](const Node* node) -> const Node* {
        if (!node)
            return nullptr;
        if (node->is_leaf())
            return node->leaf().id == id ? node : nullptr;
        const auto& s = node->split();
        if (const auto* found = search(s.first.get()))
            return found;
        return search(s.second.get());
    };
    return search(root_.get());
}

SplitTree::Node* SplitTree::find_leaf_node(LeafId id)
{
    std::function<Node*(Node*)> search = [&](Node* node) -> Node* {
        if (!node)
            return nullptr;
        if (node->is_leaf())
            return node->leaf().id == id ? node : nullptr;
        auto& s = node->split();
        if (auto* found = search(s.first.get()))
            return found;
        return search(s.second.get());
    };
    return search(root_.get());
}

const SplitTree::Node* SplitTree::find_divider_node(DividerId id) const
{
    std::function<const Node*(const Node*)> search = [&](const Node* node) -> const Node* {
        if (!node || node->is_leaf())
            return nullptr;
        const auto& s = node->split();
        if (s.divider_id == id)
            return node;
        if (const auto* found = search(s.first.get()))
            return found;
        return search(s.second.get());
    };
    return search(root_.get());
}

SplitTree::Node* SplitTree::find_divider_node(DividerId id)
{
    std::function<Node*(Node*)> search = [&](Node* node) -> Node* {
        if (!node || node->is_leaf())
            return nullptr;
        auto& s = node->split();
        if (s.divider_id == id)
            return node;
        if (auto* found = search(s.first.get()))
            return found;
        return search(s.second.get());
    };
    return search(root_.get());
}

const SplitTree::Node* SplitTree::find_parent_of(const Node* child) const
{
    std::function<const Node*(const Node*)> search = [&](const Node* node) -> const Node* {
        if (!node || node->is_leaf())
            return nullptr;
        const auto& s = node->split();
        if (s.first.get() == child || s.second.get() == child)
            return node;
        if (const auto* found = search(s.first.get()))
            return found;
        return search(s.second.get());
    };
    return search(root_.get());
}

SplitTree::Node* SplitTree::find_parent_of(const Node* child)
{
    std::function<Node*(Node*)> search = [&](Node* node) -> Node* {
        if (!node || node->is_leaf())
            return nullptr;
        auto& s = node->split();
        if (s.first.get() == child || s.second.get() == child)
            return node;
        if (auto* found = search(s.first.get()))
            return found;
        return search(s.second.get());
    };
    return search(root_.get());
}

void SplitTree::recompute_node(Node* node, int x, int y, int w, int h, int div_w)
{
    if (!node)
        return;

    if (node->is_leaf())
    {
        node->leaf().descriptor = { { x, y }, { w, h } };
        return;
    }

    auto& s = node->split();
    if (s.direction == SplitDirection::Vertical)
    {
        const int available = std::max(0, w - div_w);
        const int first_w = std::max(1,
            static_cast<int>(std::floor(static_cast<float>(available) * s.ratio)));
        const int second_w = std::max(1, available - first_w);
        s.div_x = x + first_w;
        s.div_y = y;
        s.div_w = div_w;
        s.div_h = h;
        recompute_node(s.first.get(), x, y, first_w, h, div_w);
        recompute_node(s.second.get(), x + first_w + div_w, y, second_w, h, div_w);
    }
    else
    {
        const int available = std::max(0, h - div_w);
        const int first_h = std::max(1,
            static_cast<int>(std::floor(static_cast<float>(available) * s.ratio)));
        const int second_h = std::max(1, available - first_h);
        s.div_x = x;
        s.div_y = y + first_h;
        s.div_w = w;
        s.div_h = div_w;
        recompute_node(s.first.get(), x, y, w, first_h, div_w);
        recompute_node(s.second.get(), x, y + first_h + div_w, w, second_h, div_w);
    }
}

SplitTree::HitResult SplitTree::hit_test_node(const Node* node, int px, int py, int div_w)
{
    if (!node)
        return std::monostate{};

    if (node->is_leaf())
    {
        if (const auto& d = node->leaf().descriptor;
            px >= d.pixel_pos.x && px < d.pixel_pos.x + d.pixel_size.x && py >= d.pixel_pos.y
            && py < d.pixel_pos.y + d.pixel_size.y)
            return LeafHit{ node->leaf().id };
        return std::monostate{};
    }

    const auto& s = node->split();
    // Check divider region first.
    if (px >= s.div_x && px < s.div_x + s.div_w && py >= s.div_y && py < s.div_y + s.div_h)
        return DividerHit{ s.direction, s.divider_id };

    // Recurse into children.
    if (auto result = hit_test_node(s.first.get(), px, py, div_w);
        !std::holds_alternative<std::monostate>(result))
        return result;
    return hit_test_node(s.second.get(), px, py, div_w);
}

void SplitTree::visit_leaves(
    const Node* node, const std::function<void(LeafId, const PaneDescriptor&)>& fn)
{
    if (!node)
        return;
    if (node->is_leaf())
    {
        fn(node->leaf().id, node->leaf().descriptor);
        return;
    }
    visit_leaves(node->split().first.get(), fn);
    visit_leaves(node->split().second.get(), fn);
}

int SplitTree::count_leaves(const Node* node)
{
    if (!node)
        return 0;
    if (node->is_leaf())
        return 1;
    return count_leaves(node->split().first.get()) + count_leaves(node->split().second.get());
}

LeafId SplitTree::first_leaf(const Node* node)
{
    if (!node)
        return kInvalidLeaf;
    if (node->is_leaf())
        return node->leaf().id;
    return first_leaf(node->split().first.get());
}

} // namespace draxul
