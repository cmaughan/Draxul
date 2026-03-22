#pragma once

#include <draxul/pane_descriptor.h>
#include <functional>
#include <memory>
#include <variant>

namespace draxul
{

using LeafId = int;
constexpr LeafId kInvalidLeaf = -1;

enum class SplitDirection
{
    Vertical,
    Horizontal
};

class SplitTree
{
public:
    static constexpr int kDividerWidth = 4;

    struct LeafHit
    {
        LeafId id;
    };
    struct DividerHit
    {
        SplitDirection direction;
        void* node = nullptr; // opaque handle for set_divider_ratio()
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

    // Recompute all PaneDescriptors from root dimensions.
    void recompute(int pixel_w, int pixel_h);

    // Hit-test a point in physical pixels.
    HitResult hit_test(int px, int py) const;

    // Set the split ratio for a divider (from DividerHit::node).
    // Ratio is clamped to [0.1, 0.9].
    void set_divider_ratio(void* node, float ratio);

    // Focus
    LeafId focused() const
    {
        return focused_id_;
    }
    void set_focused(LeafId id);

    // Get the computed descriptor for a leaf.
    PaneDescriptor descriptor_for(LeafId id) const;

    // Visit all leaves in spatial order (left-to-right, top-to-bottom).
    void for_each_leaf(const std::function<void(LeafId, const PaneDescriptor&)>& fn) const;

    int leaf_count() const;

private:
    struct Node;

    std::unique_ptr<Node> root_;
    LeafId next_id_ = 0;
    LeafId focused_id_ = kInvalidLeaf;
    int total_w_ = 0;
    int total_h_ = 0;

    Node* find_leaf_node(LeafId id) const;
    Node* find_parent_of(const Node* child) const;
    static void recompute_node(Node* node, int x, int y, int w, int h, int div_w);
    static HitResult hit_test_node(const Node* node, int px, int py, int div_w);
    static void visit_leaves(
        const Node* node, const std::function<void(LeafId, const PaneDescriptor&)>& fn);
    static int count_leaves(const Node* node);
    static LeafId first_leaf(const Node* node);
};

} // namespace draxul
