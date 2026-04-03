#pragma once

#include <draxul/pane_descriptor.h>
#include <functional>
#include <memory>
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
    void recompute(int pixel_w, int pixel_h);

    // Hit-test a point in physical pixels.
    HitResult hit_test(int px, int py) const;

    // Set the split ratio for a divider identified by DividerId.
    // Ratio is clamped to [0.1, 0.9].
    void set_divider_ratio(DividerId id, float ratio);

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

private:
    struct Node;

    std::unique_ptr<Node> root_;
    LeafId next_id_ = 0;
    DividerId next_divider_id_ = 0;
    LeafId focused_id_ = kInvalidLeaf;
    int total_w_ = 0;
    int total_h_ = 0;

    Node* find_leaf_node(LeafId id);
    const Node* find_leaf_node(LeafId id) const;
    Node* find_divider_node(DividerId id);
    const Node* find_divider_node(DividerId id) const;
    Node* find_parent_of(const Node* child);
    const Node* find_parent_of(const Node* child) const;
    static void recompute_node(Node* node, int x, int y, int w, int h, int div_w);
    static HitResult hit_test_node(const Node* node, int px, int py, int div_w);
    static void visit_leaves(
        const Node* node, const std::function<void(LeafId, const PaneDescriptor&)>& fn);
    static int count_leaves(const Node* node);
    static LeafId first_leaf(const Node* node);
    static LeafId last_leaf(const Node* node);
};

} // namespace draxul
