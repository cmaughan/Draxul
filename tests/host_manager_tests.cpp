#include <catch2/catch_all.hpp>

#include "host_manager.h"
#include "split_tree.h"

using namespace draxul;

TEST_CASE("host manager: split panes use the platform shell for non-shell primary hosts", "[host_manager]")
{
#ifdef _WIN32
    constexpr HostKind expected = HostKind::PowerShell;
#else
    constexpr HostKind expected = HostKind::Zsh;
#endif

    REQUIRE(HostManager::platform_default_split_host_kind() == expected);
    REQUIRE(HostManager::split_host_kind_for(HostKind::Nvim) == expected);
    REQUIRE(HostManager::split_host_kind_for(HostKind::MegaCity) == expected);
}

TEST_CASE("host manager: split panes preserve explicit shell host choices", "[host_manager]")
{
    REQUIRE(HostManager::split_host_kind_for(HostKind::PowerShell) == HostKind::PowerShell);
    REQUIRE(HostManager::split_host_kind_for(HostKind::Bash) == HostKind::Bash);
    REQUIRE(HostManager::split_host_kind_for(HostKind::Zsh) == HostKind::Zsh);
    REQUIRE(HostManager::split_host_kind_for(HostKind::Wsl) == HostKind::Wsl);
}

// --- SplitTree-level lifecycle tests ---
// These test the SplitTree that HostManager uses for layout and lifecycle.

TEST_CASE("split tree: reset creates single leaf", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    REQUIRE(root != kInvalidLeaf);
    REQUIRE(tree.leaf_count() == 1);
    REQUIRE(tree.focused() == root);
}

TEST_CASE("split tree: split creates two leaves with correct viewports", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId new_leaf = tree.split_leaf(root, SplitDirection::Vertical);

    REQUIRE(new_leaf != kInvalidLeaf);
    REQUIRE(tree.leaf_count() == 2);

    PaneDescriptor d1 = tree.descriptor_for(root);
    PaneDescriptor d2 = tree.descriptor_for(new_leaf);

    // Both panes should have non-zero dimensions
    REQUIRE(d1.pixel_size.x > 0);
    REQUIRE(d1.pixel_size.y > 0);
    REQUIRE(d2.pixel_size.x > 0);
    REQUIRE(d2.pixel_size.y > 0);

    // Combined widths should approximately equal total (minus divider)
    INFO("vertical split divides width between panes");
    REQUIRE(d1.pixel_size.x + d2.pixel_size.x + SplitTree::kDividerWidth <= 800);
}

TEST_CASE("split tree: horizontal split divides height", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId new_leaf = tree.split_leaf(root, SplitDirection::Horizontal);

    REQUIRE(new_leaf != kInvalidLeaf);

    PaneDescriptor d1 = tree.descriptor_for(root);
    PaneDescriptor d2 = tree.descriptor_for(new_leaf);

    INFO("horizontal split divides height between panes");
    REQUIRE(d1.pixel_size.y + d2.pixel_size.y + SplitTree::kDividerWidth <= 600);
    INFO("horizontal split preserves full width for both panes");
    REQUIRE(d1.pixel_size.x == 800);
    REQUIRE(d2.pixel_size.x == 800);
}

TEST_CASE("split tree: close leaf collapses tree and reassigns focus", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId new_leaf = tree.split_leaf(root, SplitDirection::Vertical);

    tree.set_focused(new_leaf);
    REQUIRE(tree.focused() == new_leaf);

    bool closed = tree.close_leaf(new_leaf);
    REQUIRE(closed);
    REQUIRE(tree.leaf_count() == 1);

    // Focus should be reassigned to the remaining leaf
    REQUIRE(tree.focused() == root);
}

TEST_CASE("split tree: cannot close last leaf", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);

    bool closed = tree.close_leaf(root);
    REQUIRE_FALSE(closed);
    REQUIRE(tree.leaf_count() == 1);
}

TEST_CASE("split tree: hit test returns correct leaf", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId right = tree.split_leaf(root, SplitDirection::Vertical);

    PaneDescriptor d_left = tree.descriptor_for(root);
    PaneDescriptor d_right = tree.descriptor_for(right);

    // Hit test in the left pane
    auto result_left = tree.hit_test(d_left.pixel_pos.x + 10, d_left.pixel_pos.y + 10);
    REQUIRE(std::holds_alternative<SplitTree::LeafHit>(result_left));
    REQUIRE(std::get<SplitTree::LeafHit>(result_left).id == root);

    // Hit test in the right pane
    auto result_right = tree.hit_test(d_right.pixel_pos.x + 10, d_right.pixel_pos.y + 10);
    REQUIRE(std::holds_alternative<SplitTree::LeafHit>(result_right));
    REQUIRE(std::get<SplitTree::LeafHit>(result_right).id == right);
}

TEST_CASE("split tree: recompute updates all descriptors proportionally", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId right = tree.split_leaf(root, SplitDirection::Vertical);

    PaneDescriptor d1_before = tree.descriptor_for(root);

    // Resize the window to 1600x1200
    tree.recompute(1600, 1200);

    PaneDescriptor d1_after = tree.descriptor_for(root);
    PaneDescriptor d2_after = tree.descriptor_for(right);

    INFO("recomputed pane is wider than before");
    REQUIRE(d1_after.pixel_size.x > d1_before.pixel_size.x);
    INFO("recomputed panes fit in new window dimensions");
    REQUIRE(d1_after.pixel_size.x + d2_after.pixel_size.x + SplitTree::kDividerWidth <= 1600);
    INFO("height adjusts to new window height");
    REQUIRE(d1_after.pixel_size.y == 1200);
}

TEST_CASE("split tree: for_each_leaf visits all leaves", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId second = tree.split_leaf(root, SplitDirection::Vertical);
    LeafId third = tree.split_leaf(second, SplitDirection::Horizontal);

    std::vector<LeafId> visited;
    tree.for_each_leaf([&](LeafId id, const PaneDescriptor&) {
        visited.push_back(id);
    });

    REQUIRE(visited.size() == 3);
    // All three IDs should be present
    REQUIRE(std::find(visited.begin(), visited.end(), root) != visited.end());
    REQUIRE(std::find(visited.begin(), visited.end(), second) != visited.end());
    REQUIRE(std::find(visited.begin(), visited.end(), third) != visited.end());
}

TEST_CASE("split tree: focus changes via set_focused", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId second = tree.split_leaf(root, SplitDirection::Vertical);

    REQUIRE(tree.focused() == root);

    tree.set_focused(second);
    REQUIRE(tree.focused() == second);

    tree.set_focused(root);
    REQUIRE(tree.focused() == root);
}

TEST_CASE("split tree: double split creates three panes", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId second = tree.split_leaf(root, SplitDirection::Vertical);
    LeafId third = tree.split_leaf(root, SplitDirection::Horizontal);

    REQUIRE(tree.leaf_count() == 3);

    // All descriptors should have non-zero dimensions
    REQUIRE(tree.descriptor_for(root).pixel_size.x > 0);
    REQUIRE(tree.descriptor_for(second).pixel_size.x > 0);
    REQUIRE(tree.descriptor_for(third).pixel_size.x > 0);
}
