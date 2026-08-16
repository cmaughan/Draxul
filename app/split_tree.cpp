#include "split_tree.h"

#include <algorithm>
#include <cmath>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/topology_protocol.h>
#include <vector>

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
        // Full parent rect (the area the two children + divider occupy),
        // computed during recompute(). Used to convert mouse pixels into ratios.
        int rect_x = 0;
        int rect_y = 0;
        int rect_w = 0;
        int rect_h = 0;
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

    recompute(origin_x_, origin_y_, total_w_, total_h_);
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
    recompute(origin_x_, origin_y_, total_w_, total_h_);
    return true;
}

void SplitTree::recompute(int pixel_w, int pixel_h)
{
    recompute(0, 0, pixel_w, pixel_h);
}

void SplitTree::recompute(int origin_x, int origin_y, int pixel_w, int pixel_h)
{
    PERF_MEASURE();
    // Clamp negative inputs to zero so leaves never receive negative dimensions.
    // Negative widths/heights would propagate straight through recompute_node()
    // for whichever axis is not the split direction, producing UB downstream.
    origin_x = std::max(0, origin_x);
    origin_y = std::max(0, origin_y);
    pixel_w = std::max(0, pixel_w);
    pixel_h = std::max(0, pixel_h);
    origin_x_ = origin_x;
    origin_y_ = origin_y;
    total_w_ = pixel_w;
    total_h_ = pixel_h;
    if (root_)
        recompute_node(root_.get(), origin_x, origin_y, pixel_w, pixel_h, kDividerWidth);
}

SplitTree::HitResult SplitTree::hit_test(int px, int py) const
{
    PERF_MEASURE();
    if (!root_)
        return std::monostate{};
    return hit_test_node(root_.get(), px, py, kDividerWidth);
}

void SplitTree::equalize_splits()
{
    PERF_MEASURE();
    std::function<void(Node*)> equalize = [&](Node* node) {
        if (!node || node->is_leaf())
            return;
        node->split().ratio = 0.5f;
        equalize(node->split().first.get());
        equalize(node->split().second.get());
    };
    equalize(root_.get());
    recompute(origin_x_, origin_y_, total_w_, total_h_);
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
        node->split().ratio = std::clamp(ratio,
            kTopologyMinSplitRatio, kTopologyMaxSplitRatio);
    recompute(origin_x_, origin_y_, total_w_, total_h_);
}

namespace
{
// Quantize a ratio so the first-child dimension lands on a multiple of
// snap_step pixels. `available` is the parent dimension minus the divider
// thickness (the same denominator update_divider_from_pixel uses).
float snap_ratio_to_step(float ratio, int available, int snap_step)
{
    if (snap_step <= 0 || available <= 0)
        return ratio;
    const float first_px = ratio * static_cast<float>(available);
    const int snapped_px = static_cast<int>(std::lround(first_px / static_cast<float>(snap_step))) * snap_step;
    return static_cast<float>(snapped_px) / static_cast<float>(available);
}
} // namespace

std::optional<float> SplitTree::divider_ratio(DividerId id) const
{
    if (id == kInvalidDivider)
        return std::nullopt;
    const Node* node = find_divider_node(id);
    if (!node || node->is_leaf())
        return std::nullopt;
    return node->split().ratio;
}

std::optional<float> SplitTree::divider_ratio_from_pixel(
    DividerId id, int px, int py, int snap_step) const
{
    if (id == kInvalidDivider)
        return std::nullopt;
    const Node* node = find_divider_node(id);
    if (!node || node->is_leaf())
        return std::nullopt;
    const auto& s = node->split();
    const int eff_div = (s.direction == SplitDirection::Vertical)
        ? std::min(kDividerWidth, s.rect_w)
        : std::min(kDividerWidth, s.rect_h);
    const int available = (s.direction == SplitDirection::Vertical)
        ? std::max(1, s.rect_w - eff_div)
        : std::max(1, s.rect_h - eff_div);
    float ratio = s.direction == SplitDirection::Vertical
        ? static_cast<float>(px - s.rect_x) / static_cast<float>(available)
        : static_cast<float>(py - s.rect_y) / static_cast<float>(available);
    ratio = snap_ratio_to_step(ratio, available, snap_step);
    return std::clamp(ratio,
        kTopologyMinSplitRatio, kTopologyMaxSplitRatio);
}

void SplitTree::update_divider_from_pixel(DividerId id, int px, int py, int snap_step)
{
    PERF_MEASURE();
    const auto ratio
        = divider_ratio_from_pixel(id, px, py, snap_step);
    if (!ratio)
        return;
    set_divider_ratio(id, *ratio);
}

void SplitTree::nudge_divider(DividerId id, float delta, int snap_step)
{
    PERF_MEASURE();
    if (id == kInvalidDivider)
        return;
    Node* node = find_divider_node(id);
    if (!node || node->is_leaf())
        return;
    auto& s = node->split();
    float new_ratio = std::clamp(s.ratio + delta,
        kTopologyMinSplitRatio, kTopologyMaxSplitRatio);
    const int eff_div = (s.direction == SplitDirection::Vertical)
        ? std::min(kDividerWidth, s.rect_w)
        : std::min(kDividerWidth, s.rect_h);
    const int available = (s.direction == SplitDirection::Vertical)
        ? std::max(1, s.rect_w - eff_div)
        : std::max(1, s.rect_h - eff_div);
    new_ratio = snap_ratio_to_step(new_ratio, available, snap_step);
    s.ratio = std::clamp(new_ratio,
        kTopologyMinSplitRatio, kTopologyMaxSplitRatio);
    recompute(origin_x_, origin_y_, total_w_, total_h_);
}

std::optional<SplitDirection> SplitTree::divider_direction(DividerId id) const
{
    if (id == kInvalidDivider)
        return std::nullopt;
    const Node* node = find_divider_node(id);
    if (!node || node->is_leaf())
        return std::nullopt;
    return node->split().direction;
}

DividerId SplitTree::find_ancestor_divider(LeafId leaf, FocusDirection direction) const
{
    PERF_MEASURE();
    const Node* target = find_leaf_node(leaf);
    if (!target)
        return kInvalidDivider;
    const SplitDirection relevant
        = (direction == FocusDirection::Left || direction == FocusDirection::Right)
        ? SplitDirection::Vertical
        : SplitDirection::Horizontal;
    const Node* child = target;
    const Node* parent = find_parent_of(child);
    while (parent)
    {
        if (!parent->is_leaf() && parent->split().direction == relevant)
            return parent->split().divider_id;
        child = parent;
        parent = find_parent_of(child);
    }
    return kInvalidDivider;
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

SplitTree::Snapshot SplitTree::snapshot() const
{
    PERF_MEASURE();
    Snapshot snapshot;
    snapshot.root = snapshot_node(root_.get());
    snapshot.focused_id = focused_id_;
    snapshot.next_leaf_id = next_id_;
    return snapshot;
}

bool SplitTree::restore(const Snapshot& snapshot, int pixel_w, int pixel_h)
{
    return restore(snapshot, 0, 0, pixel_w, pixel_h);
}

bool SplitTree::restore(
    const Snapshot& snapshot, int origin_x, int origin_y, int pixel_w, int pixel_h)
{
    PERF_MEASURE();
    if (!snapshot.root)
        return false;

    LeafId max_leaf_id = kInvalidLeaf;
    std::unique_ptr<Node> restored_root = restore_node(*snapshot.root, max_leaf_id);
    if (!restored_root)
        return false;

    root_ = std::move(restored_root);
    next_id_ = std::max(snapshot.next_leaf_id, max_leaf_id + 1);
    focused_id_ = snapshot.focused_id;
    if (!find_leaf_node(focused_id_))
        focused_id_ = first_leaf(root_.get());

    next_divider_id_ = 0;
    const auto assign_divider_ids = [this](const auto& self, Node* node) -> void {
        if (!node || node->is_leaf())
            return;
        node->split().divider_id = next_divider_id_++;
        self(self, node->split().first.get());
        self(self, node->split().second.get());
    };
    assign_divider_ids(assign_divider_ids, root_.get());

    recompute(origin_x, origin_y, pixel_w, pixel_h);
    return true;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
// Plain recursive helpers — avoid the per-call std::function heap allocation
// the previous lambda-based searches incurred.
const SplitTree::Node* SplitTree::find_leaf_impl(const Node* node, LeafId id)
{
    if (!node)
        return nullptr;
    if (node->is_leaf())
        return node->leaf().id == id ? node : nullptr;
    const auto& s = node->split();
    if (const auto* found = find_leaf_impl(s.first.get(), id))
        return found;
    return find_leaf_impl(s.second.get(), id);
}

const SplitTree::Node* SplitTree::find_divider_impl(const Node* node, DividerId id)
{
    if (!node || node->is_leaf())
        return nullptr;
    const auto& s = node->split();
    if (s.divider_id == id)
        return node;
    if (const auto* found = find_divider_impl(s.first.get(), id))
        return found;
    return find_divider_impl(s.second.get(), id);
}

const SplitTree::Node* SplitTree::find_parent_impl(const Node* node, const Node* child)
{
    if (!node || node->is_leaf())
        return nullptr;
    const auto& s = node->split();
    if (s.first.get() == child || s.second.get() == child)
        return node;
    if (const auto* found = find_parent_impl(s.first.get(), child))
        return found;
    return find_parent_impl(s.second.get(), child);
}

const SplitTree::Node* SplitTree::find_leaf_node(LeafId id) const
{
    return find_leaf_impl(root_.get(), id);
}

SplitTree::Node* SplitTree::find_leaf_node(LeafId id)
{
    return const_cast<Node*>(find_leaf_impl(root_.get(), id));
}

const SplitTree::Node* SplitTree::find_divider_node(DividerId id) const
{
    return find_divider_impl(root_.get(), id);
}

SplitTree::Node* SplitTree::find_divider_node(DividerId id)
{
    return const_cast<Node*>(find_divider_impl(root_.get(), id));
}

const SplitTree::Node* SplitTree::find_parent_of(const Node* child) const
{
    return find_parent_impl(root_.get(), child);
}

SplitTree::Node* SplitTree::find_parent_of(const Node* child)
{
    return const_cast<Node*>(find_parent_impl(root_.get(), child));
}

void SplitTree::recompute_node(Node* node, int x, int y, int w, int h, int div_w)
{
    if (!node)
        return;

    if (node->is_leaf())
    {
        node->leaf().descriptor
            = { { x, y }, { std::max(0, w), std::max(0, h) } };
        return;
    }

    const int safe_w = std::max(0, w);
    const int safe_h = std::max(0, h);

    auto& s = node->split();
    s.rect_x = x;
    s.rect_y = y;
    s.rect_w = safe_w;
    s.rect_h = safe_h;
    if (s.direction == SplitDirection::Vertical)
    {
        // Clamp the divider so it never extends beyond the parent rect; the
        // remaining `available` space is split between the two children.
        const int eff_div_w = std::min(div_w, safe_w);
        const int available = safe_w - eff_div_w;
        const int first_w = (available > 0)
            ? std::max(1,
                  std::min(available,
                      static_cast<int>(std::floor(static_cast<float>(available) * s.ratio))))
            : 0;
        const int second_w = std::max(0, available - first_w);
        s.div_x = x + first_w;
        s.div_y = y;
        s.div_w = eff_div_w;
        s.div_h = safe_h;
        recompute_node(s.first.get(), x, y, first_w, safe_h, div_w);
        recompute_node(s.second.get(), x + first_w + eff_div_w, y, second_w, safe_h, div_w);
    }
    else
    {
        const int eff_div_h = std::min(div_w, safe_h);
        const int available = safe_h - eff_div_h;
        const int first_h = (available > 0)
            ? std::max(1,
                  std::min(available,
                      static_cast<int>(std::floor(static_cast<float>(available) * s.ratio))))
            : 0;
        const int second_h = std::max(0, available - first_h);
        s.div_x = x;
        s.div_y = y + first_h;
        s.div_w = safe_w;
        s.div_h = eff_div_h;
        recompute_node(s.first.get(), x, y, safe_w, first_h, div_w);
        recompute_node(s.second.get(), x, y + first_h + eff_div_h, safe_w, second_h, div_w);
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

bool SplitTree::swap_leaves(LeafId a, LeafId b)
{
    if (a == b)
        return true;
    Node* node_a = find_leaf_node(a);
    Node* node_b = find_leaf_node(b);
    if (!node_a || !node_b)
        return false;

    // Swap the leaf IDs — the tree structure and descriptors stay the same.
    std::swap(node_a->leaf().id, node_b->leaf().id);

    // Update focus to follow the swap.
    if (focused_id_ == a)
        focused_id_ = b;
    else if (focused_id_ == b)
        focused_id_ = a;

    return true;
}

LeafId SplitTree::next_leaf_after(LeafId id) const
{
    if (leaf_count() < 2)
        return kInvalidLeaf;

    // Collect all leaf IDs in spatial order.
    std::vector<LeafId> ids;
    for_each_leaf([&ids](LeafId lid, const PaneDescriptor&) { ids.push_back(lid); });

    for (size_t i = 0; i < ids.size(); ++i)
    {
        if (ids[i] == id)
            return ids[(i + 1) % ids.size()];
    }
    return kInvalidLeaf;
}

LeafId SplitTree::last_leaf(const Node* node)
{
    if (!node)
        return kInvalidLeaf;
    if (node->is_leaf())
        return node->leaf().id;
    return last_leaf(node->split().second.get());
}

std::unique_ptr<SplitTree::SnapshotNode> SplitTree::snapshot_node(const Node* node)
{
    if (!node)
        return nullptr;

    auto snapshot = std::make_unique<SnapshotNode>();
    if (node->is_leaf())
    {
        snapshot->is_leaf = true;
        snapshot->leaf_id = node->leaf().id;
        return snapshot;
    }

    snapshot->is_leaf = false;
    snapshot->direction = node->split().direction;
    snapshot->ratio = node->split().ratio;
    snapshot->first = snapshot_node(node->split().first.get());
    snapshot->second = snapshot_node(node->split().second.get());
    return snapshot;
}

std::unique_ptr<SplitTree::Node> SplitTree::restore_node(
    const SnapshotNode& snapshot_node, LeafId& max_leaf_id)
{
    auto node = std::make_unique<Node>();
    if (snapshot_node.is_leaf)
    {
        if (snapshot_node.leaf_id == kInvalidLeaf)
            return nullptr;
        node->data = Node::LeafData{ snapshot_node.leaf_id, {} };
        max_leaf_id = std::max(max_leaf_id, snapshot_node.leaf_id);
        return node;
    }

    if (!snapshot_node.first || !snapshot_node.second)
        return nullptr;

    auto first = restore_node(*snapshot_node.first, max_leaf_id);
    auto second = restore_node(*snapshot_node.second, max_leaf_id);
    if (!first || !second)
        return nullptr;

    Node::SplitData split_data;
    split_data.direction = snapshot_node.direction;
    split_data.ratio = std::clamp(snapshot_node.ratio,
        kTopologyMinSplitRatio, kTopologyMaxSplitRatio);
    split_data.first = std::move(first);
    split_data.second = std::move(second);
    node->data = std::move(split_data);
    return node;
}

LeafId SplitTree::find_neighbor(LeafId id, FocusDirection direction) const
{
    PERF_MEASURE();
    const Node* target = find_leaf_node(id);
    if (!target)
        return kInvalidLeaf;

    // Determine which split direction and which child side we need.
    // Left/Right navigate across Vertical splits; Up/Down across Horizontal splits.
    const SplitDirection relevant_split = (direction == FocusDirection::Left || direction == FocusDirection::Right)
        ? SplitDirection::Vertical
        : SplitDirection::Horizontal;
    // "want_first_child" means we want to move left/up (toward the first child).
    const bool want_first_child = (direction == FocusDirection::Left || direction == FocusDirection::Up);

    // Walk up the tree to find an ancestor split where:
    // - the split direction matches the navigation direction
    // - the target is in the child on the side we're moving away from
    const Node* child = target;
    const Node* parent = find_parent_of(child);
    while (parent)
    {
        if (!parent->is_leaf())
        {
            const auto& s = parent->split();
            if (s.direction == relevant_split)
            {
                const bool child_is_first = (s.first.get() == child);
                // If moving left/up and child is in second, the neighbor is in first.
                // If moving right/down and child is in first, the neighbor is in second.
                if (want_first_child && !child_is_first)
                    return last_leaf(s.first.get());
                if (!want_first_child && child_is_first)
                    return first_leaf(s.second.get());
            }
        }
        child = parent;
        parent = find_parent_of(child);
    }

    return kInvalidLeaf;
}

void SplitTree::visit_dividers(
    const Node* node, const std::function<void(const DividerRect&)>& fn)
{
    if (!node || node->is_leaf())
        return;
    const auto& s = node->split();
    DividerRect r;
    r.x = s.div_x;
    r.y = s.div_y;
    r.w = s.div_w;
    r.h = s.div_h;
    r.direction = s.direction;
    fn(r);
    visit_dividers(s.first.get(), fn);
    visit_dividers(s.second.get(), fn);
}

void SplitTree::for_each_divider(const std::function<void(const DividerRect&)>& fn) const
{
    visit_dividers(root_.get(), fn);
}

} // namespace draxul
