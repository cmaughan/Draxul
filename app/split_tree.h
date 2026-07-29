#pragma once

#include <draxul/pane_descriptor.h>
#include <functional>
#include <memory>
#include <optional>
#include <variant>

namespace draxul
{

using LeafId = int;
using DividerId = int;
constexpr LeafId kInvalidLeaf = -1;
constexpr DividerId kInvalidDivider = -1;

enum class SplitDirection
{
    Vertical,
    Horizontal
};

enum class FocusDirection
{
    Left,
    Right,
    Up,
    Down
};

class SplitTree
{
public:
    static constexpr int kDividerWidth = 4;

    struct DividerRect
    {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        SplitDirection direction = SplitDirection::Vertical;
    };

    struct SnapshotNode
    {
        bool is_leaf = true;
        LeafId leaf_id = kInvalidLeaf;
        SplitDirection direction = SplitDirection::Vertical;
        float ratio = 0.5f;
        std::unique_ptr<SnapshotNode> first;
        std::unique_ptr<SnapshotNode> second;
    };

    struct Snapshot
    {
        std::unique_ptr<SnapshotNode> root;
        LeafId focused_id = kInvalidLeaf;
        LeafId next_leaf_id = 0;
    };

    struct LeafHit
    {
        LeafId id;
    };
    struct DividerHit
    {
        SplitDirection direction;
        DividerId id = kInvalidDivider;
    };
    using HitResult = std::variant<std::monostate, LeafHit, DividerHit>;

    SplitTree();
    ~SplitTree();
    SplitTree(SplitTree&&) noexcept;
    SplitTree& operator=(SplitTree&&) noexcept;

    // Reset to a single leaf filling the given dimensions.
    LeafId reset(int pixel_w, int pixel_h);

    // Split an existing leaf. The original becomes the first child (left/top).
    // Returns the new leaf's ID, or kInvalidLeaf on failure.
    LeafId split_leaf(LeafId id, SplitDirection dir);

    // Close a leaf. Its parent split is replaced by the sibling.
    // Returns false if this is the last leaf.
    bool close_leaf(LeafId id);

    // Swap the content identity of two leaves in the tree.
    // The tree structure and descriptors are unchanged — only the leaf IDs swap.
    // Returns false if either ID is not found.
    bool swap_leaves(LeafId a, LeafId b);

    // Returns the next leaf after |id| in spatial order (wraps around).
    // Returns kInvalidLeaf if the tree has fewer than 2 leaves.
    LeafId next_leaf_after(LeafId id) const;

    // Recompute all PaneDescriptors from root dimensions.
    // origin_x/origin_y offset all pane positions (e.g. to reserve space for a tab bar).
    void recompute(int pixel_w, int pixel_h);
    void recompute(int origin_x, int origin_y, int pixel_w, int pixel_h);

    // Hit-test a point in physical pixels.
    HitResult hit_test(int px, int py) const;

    // Reset all split ratios to 0.5 (equal sizing).
    void equalize_splits();

    // Set the split ratio for a divider identified by DividerId.
    // Ratio is clamped to [0.1, 0.9].
    void set_divider_ratio(DividerId id, float ratio);

    // Read or calculate a divider ratio without mutating the tree. The pixel
    // variant uses the same clamping and optional cell snapping as a drag.
    std::optional<float> divider_ratio(DividerId id) const;
    std::optional<float> divider_ratio_from_pixel(
        DividerId id, int px, int py, int snap_step = 0) const;

    // Update a divider's ratio from a mouse pixel position. The pixel is mapped
    // to a position within the divider's parent rect along the split axis.
    // If snap_step > 0, the resulting first-child dimension is quantized to a
    // multiple of snap_step pixels (tmux-style cell-aligned drag).
    void update_divider_from_pixel(DividerId id, int px, int py, int snap_step = 0);

    // Adjust a divider's ratio by `delta` (positive grows the first child).
    // Clamped to [0.1, 0.9]. If snap_step > 0, the resulting first-child
    // dimension is quantized to a multiple of snap_step pixels.
    void nudge_divider(DividerId id, float delta, int snap_step = 0);

    // Returns the split direction for a given divider id, or nullopt if none.
    std::optional<SplitDirection> divider_direction(DividerId id) const;

    // Find the nearest ancestor divider above `leaf` whose split direction
    // matches `direction` (Left/Right -> Vertical, Up/Down -> Horizontal).
    // Returns kInvalidDivider if none exists.
    DividerId find_ancestor_divider(LeafId leaf, FocusDirection direction) const;

    // Focus
    LeafId focused() const
    {
        return focused_id_;
    }
    void set_focused(LeafId id);

    // Find the adjacent leaf in the given direction, or kInvalidLeaf if none exists.
    LeafId find_neighbor(LeafId id, FocusDirection direction) const;

    // Get the computed descriptor for a leaf.
    PaneDescriptor descriptor_for(LeafId id) const;

    // Visit all leaves in spatial order (left-to-right, top-to-bottom).
    void for_each_leaf(const std::function<void(LeafId, const PaneDescriptor&)>& fn) const;

    int leaf_count() const;

    // Visit all dividers in the tree. Only called when there are splits.
    void for_each_divider(const std::function<void(const DividerRect&)>& fn) const;

    [[nodiscard]] Snapshot snapshot() const;
    bool restore(const Snapshot& snapshot, int pixel_w, int pixel_h);
    bool restore(const Snapshot& snapshot, int origin_x, int origin_y, int pixel_w, int pixel_h);

private:
    struct Node;

    std::unique_ptr<Node> root_;
    LeafId next_id_ = 0;
    DividerId next_divider_id_ = 0;
    LeafId focused_id_ = kInvalidLeaf;
    int origin_x_ = 0;
    int origin_y_ = 0;
    int total_w_ = 0;
    int total_h_ = 0;

    Node* find_leaf_node(LeafId id);
    const Node* find_leaf_node(LeafId id) const;
    Node* find_divider_node(DividerId id);
    const Node* find_divider_node(DividerId id) const;
    Node* find_parent_of(const Node* child);
    const Node* find_parent_of(const Node* child) const;
    static const Node* find_leaf_impl(const Node* node, LeafId id);
    static const Node* find_divider_impl(const Node* node, DividerId id);
    static const Node* find_parent_impl(const Node* node, const Node* child);
    static void visit_dividers(
        const Node* node, const std::function<void(const DividerRect&)>& fn);
    static void recompute_node(Node* node, int x, int y, int w, int h, int div_w);
    static HitResult hit_test_node(const Node* node, int px, int py, int div_w);
    static void visit_leaves(
        const Node* node, const std::function<void(LeafId, const PaneDescriptor&)>& fn);
    static int count_leaves(const Node* node);
    static LeafId first_leaf(const Node* node);
    static LeafId last_leaf(const Node* node);
    static std::unique_ptr<SnapshotNode> snapshot_node(const Node* node);
    std::unique_ptr<Node> restore_node(const SnapshotNode& snapshot_node, LeafId& max_leaf_id);
};

} // namespace draxul
