#include "split_tree.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

using namespace draxul;

TEST_CASE("SplitTree: single leaf", "[split_tree]")
{
    SplitTree tree;
    auto id = tree.reset(1000, 800);

    REQUIRE(tree.leaf_count() == 1);
    REQUIRE(tree.focused() == id);

    auto desc = tree.descriptor_for(id);
    CHECK(desc.pixel_pos.x == 0);
    CHECK(desc.pixel_pos.y == 0);
    CHECK(desc.pixel_size.x == 1000);
    CHECK(desc.pixel_size.y == 800);

    // Can't close the last leaf.
    CHECK_FALSE(tree.close_leaf(id));

    // Root must still be intact after the rejected close.
    REQUIRE(tree.leaf_count() == 1);

    // The leaf descriptor must still be valid and unchanged.
    auto after = tree.descriptor_for(id);
    CHECK(after.pixel_pos.x == 0);
    CHECK(after.pixel_pos.y == 0);
    CHECK(after.pixel_size.x == 1000);
    CHECK(after.pixel_size.y == 800);
}

TEST_CASE("SplitTree: vertical split", "[split_tree]")
{
    SplitTree tree;
    auto left = tree.reset(1000, 800);
    auto right = tree.split_leaf(left, SplitDirection::Vertical);

    REQUIRE(tree.leaf_count() == 2);
    REQUIRE(right != kInvalidLeaf);
    REQUIRE(right != left);

    auto ld = tree.descriptor_for(left);
    auto rd = tree.descriptor_for(right);

    // 1000 total, 4px divider → 996 available, 498 each
    CHECK(ld.pixel_pos.x == 0);
    CHECK(ld.pixel_pos.y == 0);
    CHECK(ld.pixel_size.x == 498);
    CHECK(ld.pixel_size.y == 800);

    CHECK(rd.pixel_pos.x == 502);
    CHECK(rd.pixel_pos.y == 0);
    CHECK(rd.pixel_size.x == 498);
    CHECK(rd.pixel_size.y == 800);
}

TEST_CASE("SplitTree: horizontal split", "[split_tree]")
{
    SplitTree tree;
    auto top = tree.reset(1000, 800);
    auto bottom = tree.split_leaf(top, SplitDirection::Horizontal);

    REQUIRE(tree.leaf_count() == 2);

    auto td = tree.descriptor_for(top);
    auto bd = tree.descriptor_for(bottom);

    // 800 total, 4px divider → 796 available, 398 each
    CHECK(td.pixel_pos.x == 0);
    CHECK(td.pixel_pos.y == 0);
    CHECK(td.pixel_size.x == 1000);
    CHECK(td.pixel_size.y == 398);

    CHECK(bd.pixel_pos.x == 0);
    CHECK(bd.pixel_pos.y == 402);
    CHECK(bd.pixel_size.x == 1000);
    CHECK(bd.pixel_size.y == 398);
}

TEST_CASE("SplitTree: recursive split", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);
    // Tree: Split(V, A[498x800], B[498x800])

    auto c = tree.split_leaf(b, SplitDirection::Horizontal);
    // Tree: Split(V, A[498x800], Split(H, B[498x398], C[498x398]))

    REQUIRE(tree.leaf_count() == 3);

    auto ad = tree.descriptor_for(a);
    auto bd = tree.descriptor_for(b);
    auto cd = tree.descriptor_for(c);

    // A: left half
    CHECK(ad.pixel_pos.x == 0);
    CHECK(ad.pixel_size.x == 498);
    CHECK(ad.pixel_size.y == 800);

    // B: right-top
    CHECK(bd.pixel_pos.x == 502);
    CHECK(bd.pixel_pos.y == 0);
    CHECK(bd.pixel_size.x == 498);
    CHECK(bd.pixel_size.y == 398);

    // C: right-bottom
    CHECK(cd.pixel_pos.x == 502);
    CHECK(cd.pixel_pos.y == 402);
    CHECK(cd.pixel_size.x == 498);
    CHECK(cd.pixel_size.y == 398);
}

TEST_CASE("SplitTree: close leaf in 2-pane", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);

    REQUIRE(tree.close_leaf(b));
    REQUIRE(tree.leaf_count() == 1);

    auto ad = tree.descriptor_for(a);
    CHECK(ad.pixel_pos.x == 0);
    CHECK(ad.pixel_pos.y == 0);
    CHECK(ad.pixel_size.x == 1000);
    CHECK(ad.pixel_size.y == 800);
}

TEST_CASE("SplitTree: close leaf in 3-pane collapses parent", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);
    auto c = tree.split_leaf(b, SplitDirection::Horizontal);
    // Tree: Split(V, A, Split(H, B, C))

    // Close B → right side becomes just C
    REQUIRE(tree.close_leaf(b));
    REQUIRE(tree.leaf_count() == 2);

    auto ad = tree.descriptor_for(a);
    CHECK(ad.pixel_pos.x == 0);
    CHECK(ad.pixel_size.x == 498);

    auto cd = tree.descriptor_for(c);
    CHECK(cd.pixel_pos.x == 502);
    CHECK(cd.pixel_size.x == 498);
    CHECK(cd.pixel_size.y == 800); // full height — parent split collapsed
}

TEST_CASE("SplitTree: close leaf promotes sibling subtree", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);
    auto c = tree.split_leaf(a, SplitDirection::Horizontal);
    // Tree: Split(V, Split(H, A, C), B)

    // Close B → root becomes Split(H, A, C)
    REQUIRE(tree.close_leaf(b));
    REQUIRE(tree.leaf_count() == 2);

    auto ad = tree.descriptor_for(a);
    auto cd = tree.descriptor_for(c);

    // Now horizontally split across full window
    CHECK(ad.pixel_pos.x == 0);
    CHECK(ad.pixel_size.x == 1000);
    CHECK(ad.pixel_size.y == 398);

    CHECK(cd.pixel_pos.x == 0);
    CHECK(cd.pixel_pos.y == 402);
    CHECK(cd.pixel_size.x == 1000);
    CHECK(cd.pixel_size.y == 398);
}

TEST_CASE("SplitTree: close focused leaf moves focus", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);

    tree.set_focused(b);
    REQUIRE(tree.focused() == b);

    tree.close_leaf(b);
    CHECK(tree.focused() == a);
}

TEST_CASE("SplitTree: hit_test finds correct leaf", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);

    // Left pane: 0..497
    auto result = tree.hit_test(100, 400);
    REQUIRE(std::holds_alternative<SplitTree::LeafHit>(result));
    CHECK(std::get<SplitTree::LeafHit>(result).id == a);

    // Right pane: 502..999
    result = tree.hit_test(700, 400);
    REQUIRE(std::holds_alternative<SplitTree::LeafHit>(result));
    CHECK(std::get<SplitTree::LeafHit>(result).id == b);
}

TEST_CASE("SplitTree: hit_test finds divider", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    tree.split_leaf(a, SplitDirection::Vertical);

    // Divider at x=498..501
    auto result = tree.hit_test(499, 400);
    REQUIRE(std::holds_alternative<SplitTree::DividerHit>(result));
    CHECK(std::get<SplitTree::DividerHit>(result).direction == SplitDirection::Vertical);
    CHECK(std::get<SplitTree::DividerHit>(result).id != kInvalidDivider);
}

TEST_CASE("SplitTree: hit_test outside returns monostate", "[split_tree]")
{
    SplitTree tree;
    tree.reset(1000, 800);

    auto result = tree.hit_test(1500, 400);
    CHECK(std::holds_alternative<std::monostate>(result));
}

TEST_CASE("SplitTree: recompute on resize", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);

    tree.recompute(2000, 1000);

    auto ad = tree.descriptor_for(a);
    auto bd = tree.descriptor_for(b);

    // 2000 total, 4px divider → 1996, 998 each
    CHECK(ad.pixel_size.x == 998);
    CHECK(ad.pixel_size.y == 1000);
    CHECK(bd.pixel_pos.x == 1002);
    CHECK(bd.pixel_size.x == 998);
    CHECK(bd.pixel_size.y == 1000);
}

TEST_CASE("SplitTree: set_divider_ratio", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);

    // Find the divider
    auto result = tree.hit_test(499, 400);
    REQUIRE(std::holds_alternative<SplitTree::DividerHit>(result));
    auto& hit = std::get<SplitTree::DividerHit>(result);

    // Set ratio to 0.3 (left pane gets 30%)
    tree.set_divider_ratio(hit.id, 0.3f);

    auto ad = tree.descriptor_for(a);
    auto bd = tree.descriptor_for(b);

    // 996 available, floor(996 * 0.3) = 298
    CHECK(ad.pixel_size.x == 298);
    CHECK(bd.pixel_size.x == 698);
    CHECK(bd.pixel_pos.x == 302); // 298 + 4
}

TEST_CASE("SplitTree: set_divider_ratio clamps", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);

    auto result = tree.hit_test(499, 400);
    auto& hit = std::get<SplitTree::DividerHit>(result);

    // Extreme low — clamped to 0.1
    tree.set_divider_ratio(hit.id, 0.01f);
    CHECK(tree.descriptor_for(a).pixel_size.x > 0);
    CHECK(tree.descriptor_for(b).pixel_size.x > 0);

    // Extreme high — clamped to 0.9
    tree.set_divider_ratio(hit.id, 0.99f);
    CHECK(tree.descriptor_for(a).pixel_size.x > 0);
    CHECK(tree.descriptor_for(b).pixel_size.x > 0);
}

TEST_CASE("SplitTree: update_divider_from_pixel maps mouse to ratio", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);

    auto result = tree.hit_test(499, 400);
    REQUIRE(std::holds_alternative<SplitTree::DividerHit>(result));
    const DividerId id = std::get<SplitTree::DividerHit>(result).id;
    REQUIRE(tree.divider_ratio(id) == Catch::Approx(0.5f));
    const auto calculated
        = tree.divider_ratio_from_pixel(id, 300, 400);
    REQUIRE(calculated);
    CHECK(*calculated == Catch::Approx(300.0f / 996.0f));
    CHECK(tree.divider_ratio(id) == Catch::Approx(0.5f));

    // Drop the divider near x=300 — left pane should occupy roughly that width.
    tree.update_divider_from_pixel(id, 300, 400);
    auto ad = tree.descriptor_for(a);
    auto bd = tree.descriptor_for(b);
    CHECK(ad.pixel_size.x >= 295);
    CHECK(ad.pixel_size.x <= 305);
    CHECK(ad.pixel_size.x + SplitTree::kDividerWidth + bd.pixel_size.x == 1000);

    // Drag far past the right edge — clamps to 0.9 (~896 px on the left).
    tree.update_divider_from_pixel(id, 5000, 400);
    ad = tree.descriptor_for(a);
    CHECK(ad.pixel_size.x > 850);
    CHECK(ad.pixel_size.x < 900);

    // Drag far left — clamps to 0.1 (~99 px).
    tree.update_divider_from_pixel(id, -100, 400);
    ad = tree.descriptor_for(a);
    CHECK(ad.pixel_size.x > 80);
    CHECK(ad.pixel_size.x < 120);
}

TEST_CASE("SplitTree: nudge_divider adjusts ratio by delta", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    tree.split_leaf(a, SplitDirection::Vertical);

    auto result = tree.hit_test(499, 400);
    const DividerId id = std::get<SplitTree::DividerHit>(result).id;

    const int initial = tree.descriptor_for(a).pixel_size.x;
    tree.nudge_divider(id, 0.1f);
    const int after_grow = tree.descriptor_for(a).pixel_size.x;
    CHECK(after_grow > initial);

    tree.nudge_divider(id, -0.2f);
    const int after_shrink = tree.descriptor_for(a).pixel_size.x;
    CHECK(after_shrink < after_grow);
}

TEST_CASE("SplitTree: find_ancestor_divider walks up the tree", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical); // vertical divider above
    auto c = tree.split_leaf(b, SplitDirection::Horizontal); // horizontal under b

    // From c: nearest horizontal ancestor is the b/c split.
    DividerId h_div = tree.find_ancestor_divider(c, FocusDirection::Down);
    DividerId v_div = tree.find_ancestor_divider(c, FocusDirection::Left);
    CHECK(h_div != kInvalidDivider);
    CHECK(v_div != kInvalidDivider);
    CHECK(h_div != v_div);

    // From a: only the vertical divider exists above.
    CHECK(tree.find_ancestor_divider(a, FocusDirection::Left) != kInvalidDivider);
    CHECK(tree.find_ancestor_divider(a, FocusDirection::Down) == kInvalidDivider);
}

TEST_CASE("SplitTree: snapshot restore preserves layout, ids, and focus", "[split_tree]")
{
    SplitTree original;
    auto left = original.reset(1000, 800);
    auto right = original.split_leaf(left, SplitDirection::Vertical);
    auto bottom_right = original.split_leaf(right, SplitDirection::Horizontal);

    const auto root_divider_hit = original.hit_test(499, 400);
    REQUIRE(std::holds_alternative<SplitTree::DividerHit>(root_divider_hit));
    original.set_divider_ratio(std::get<SplitTree::DividerHit>(root_divider_hit).id, 0.3f);
    original.set_focused(bottom_right);

    const auto snapshot = original.snapshot();

    SplitTree restored;
    REQUIRE(restored.restore(snapshot, 1000, 800));
    REQUIRE(restored.leaf_count() == 3);
    CHECK(restored.focused() == bottom_right);

    const auto left_desc = restored.descriptor_for(left);
    const auto right_desc = restored.descriptor_for(right);
    const auto bottom_right_desc = restored.descriptor_for(bottom_right);

    CHECK(left_desc.pixel_pos.x == 0);
    CHECK(left_desc.pixel_size.x == original.descriptor_for(left).pixel_size.x);
    CHECK(right_desc.pixel_pos.x == original.descriptor_for(right).pixel_pos.x);
    CHECK(right_desc.pixel_size.y == original.descriptor_for(right).pixel_size.y);
    CHECK(bottom_right_desc.pixel_pos.y == original.descriptor_for(bottom_right).pixel_pos.y);
    CHECK(bottom_right_desc.pixel_size.y == original.descriptor_for(bottom_right).pixel_size.y);
}

TEST_CASE("SplitTree: stale divider id is ignored", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);

    auto result = tree.hit_test(499, 400);
    REQUIRE(std::holds_alternative<SplitTree::DividerHit>(result));
    const DividerId stale_id = std::get<SplitTree::DividerHit>(result).id;

    REQUIRE(tree.close_leaf(b));

    const PaneDescriptor before = tree.descriptor_for(a);
    tree.set_divider_ratio(stale_id, 0.3f);
    const PaneDescriptor after = tree.descriptor_for(a);

    CHECK(after.pixel_pos.x == before.pixel_pos.x);
    CHECK(after.pixel_pos.y == before.pixel_pos.y);
    CHECK(after.pixel_size.x == before.pixel_size.x);
    CHECK(after.pixel_size.y == before.pixel_size.y);
}

TEST_CASE("SplitTree: for_each_leaf visits all in order", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);
    auto c = tree.split_leaf(b, SplitDirection::Horizontal);

    std::vector<LeafId> visited;
    tree.for_each_leaf(
        [&](LeafId id, const PaneDescriptor&) { visited.push_back(id); });

    REQUIRE(visited.size() == 3);
    CHECK(visited[0] == a);
    CHECK(visited[1] == b);
    CHECK(visited[2] == c);
}

TEST_CASE("SplitTree: split invalid id", "[split_tree]")
{
    SplitTree tree;
    tree.reset(1000, 800);
    CHECK(tree.split_leaf(999, SplitDirection::Vertical) == kInvalidLeaf);
}

TEST_CASE("SplitTree: descriptor_for invalid id", "[split_tree]")
{
    SplitTree tree;
    tree.reset(1000, 800);
    auto desc = tree.descriptor_for(999);
    CHECK(desc.pixel_size.x == 0);
    CHECK(desc.pixel_size.y == 0);
}

TEST_CASE("SplitTree: pixel conservation", "[split_tree]")
{
    // Verify total pixels are conserved across all leaves + dividers.
    SplitTree tree;
    auto a = tree.reset(1001, 801); // odd dimensions
    auto b = tree.split_leaf(a, SplitDirection::Vertical);
    auto c = tree.split_leaf(b, SplitDirection::Horizontal);
    auto d = tree.split_leaf(a, SplitDirection::Horizontal);

    // 4 leaves in a complex tree
    REQUIRE(tree.leaf_count() == 4);

    // Every pixel coordinate within the window should be accounted for
    // by exactly one leaf or one divider.
    int leaf_pixels = 0;
    tree.for_each_leaf([&](LeafId, const PaneDescriptor& desc) {
        leaf_pixels += desc.pixel_size.x * desc.pixel_size.y;
    });
    // Some pixels belong to dividers, so leaf_pixels < total.
    // But all leaf rects should be non-overlapping and within bounds.
    tree.for_each_leaf([&](LeafId, const PaneDescriptor& desc) {
        CHECK(desc.pixel_pos.x >= 0);
        CHECK(desc.pixel_pos.y >= 0);
        CHECK(desc.pixel_pos.x + desc.pixel_size.x <= 1001);
        CHECK(desc.pixel_pos.y + desc.pixel_size.y <= 801);
        CHECK(desc.pixel_size.x > 0);
        CHECK(desc.pixel_size.y > 0);
    });
}

TEST_CASE("SplitTree: deep recursive split 4 levels", "[split_tree]")
{
    SplitTree tree;
    auto a = tree.reset(2000, 1000);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);
    auto c = tree.split_leaf(b, SplitDirection::Horizontal);
    auto d = tree.split_leaf(c, SplitDirection::Vertical);

    REQUIRE(tree.leaf_count() == 4);

    // All leaves should have positive dimensions
    tree.for_each_leaf([&](LeafId, const PaneDescriptor& desc) {
        CHECK(desc.pixel_size.x > 0);
        CHECK(desc.pixel_size.y > 0);
    });
}

TEST_CASE("SplitTree: 100-deep split keeps recursion safe", "[split_tree]")
{
    SplitTree tree;
    const LeafId root = tree.reset(2000, 1000);

    for (int i = 0; i < 100; ++i)
    {
        REQUIRE(tree.split_leaf(root, SplitDirection::Vertical) != kInvalidLeaf);
    }

    tree.recompute(2000, 1000);

    REQUIRE(tree.leaf_count() == 101);

    auto result = tree.hit_test(123, 456);
    CHECK_FALSE(std::holds_alternative<std::monostate>(result));

    std::vector<LeafId> visited;
    tree.for_each_leaf([&](LeafId id, const PaneDescriptor& desc) {
        visited.push_back(id);
        // Deeply nested splits eventually collapse the innermost panes to 0
        // width (each divider eats 4px), but everything must still be
        // non-negative and inside the 2000x1000 window.
        CHECK(desc.pixel_size.x >= 0);
        CHECK(desc.pixel_size.y >= 0);
        CHECK(desc.pixel_pos.x >= 0);
        CHECK(desc.pixel_pos.x + desc.pixel_size.x <= 2000);
        CHECK(desc.pixel_pos.y + desc.pixel_size.y <= 1000);
    });

    REQUIRE(visited.size() == 101);
}

TEST_CASE("SplitTree: tiny panes stay in bounds", "[split_tree]")
{
    // When the available space cannot accommodate all dividers, panes collapse
    // to zero width but must remain inside the window — never OOB. (See WI 08
    // splittree-oob-placement.)
    SplitTree tree;
    const LeafId root = tree.reset(8, 8);

    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(tree.split_leaf(root, SplitDirection::Vertical) != kInvalidLeaf);
    }

    tree.recompute(8, 8);

    std::vector<PaneDescriptor> descriptors;
    tree.for_each_leaf([&](LeafId, const PaneDescriptor& desc) {
        descriptors.push_back(desc);
        CHECK(desc.pixel_size.x >= 0);
        CHECK(desc.pixel_size.y >= 0);
        CHECK(desc.pixel_pos.x >= 0);
        CHECK(desc.pixel_pos.x + desc.pixel_size.x <= 8);
        CHECK(desc.pixel_pos.y + desc.pixel_size.y <= 8);
    });

    REQUIRE(descriptors.size() == 6);
}

// ---------------------------------------------------------------------------
// Zero / degenerate dimension regression tests (WI 68)
// ---------------------------------------------------------------------------
//
// recompute() must remain well-defined for tiny / zero / negative inputs (e.g.
// during minimise, or before the first SDL resize lands). The contract these
// tests pin down:
//   * No leaf descriptor has a negative width or height for any input.
//   * When the parent rect cannot fit a divider, panes collapse to 0 in the
//     split direction (rather than being clamped to 1px and pushed out of
//     bounds — see WI 08 splittree-oob-placement).
//   * Leaves are always contained within the parent rectangle.
//   * Origins never go negative.
//   * recompute(0, 0) followed by a real recompute() restores the layout.

namespace
{

void check_no_negative_dimensions(const SplitTree& tree)
{
    tree.for_each_leaf([](LeafId, const PaneDescriptor& desc) {
        CHECK(desc.pixel_pos.x >= 0);
        CHECK(desc.pixel_pos.y >= 0);
        CHECK(desc.pixel_size.x >= 0);
        CHECK(desc.pixel_size.y >= 0);
    });
}

} // namespace

TEST_CASE("SplitTree zero dim: reset(0, 0) yields a well-defined single leaf", "[split_tree][zero]")
{
    SplitTree tree;
    auto id = tree.reset(0, 0);

    REQUIRE(tree.leaf_count() == 1);
    auto desc = tree.descriptor_for(id);
    CHECK(desc.pixel_pos.x == 0);
    CHECK(desc.pixel_pos.y == 0);
    CHECK(desc.pixel_size.x == 0);
    CHECK(desc.pixel_size.y == 0);
}

TEST_CASE("SplitTree zero dim: recompute(0, 0) on a 2-pane vertical split is non-negative",
    "[split_tree][zero]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);

    tree.recompute(0, 0);
    check_no_negative_dimensions(tree);

    // When the parent rect can't fit a divider, both panes collapse to 0 width
    // and stay anchored within the (empty) parent rect — no out-of-bounds.
    auto ad = tree.descriptor_for(a);
    auto bd = tree.descriptor_for(b);
    CHECK(ad.pixel_size.x == 0);
    CHECK(bd.pixel_size.x == 0);
    CHECK(ad.pixel_pos.x == 0);
    CHECK(bd.pixel_pos.x == 0);
    CHECK(ad.pixel_size.y == 0);
    CHECK(bd.pixel_size.y == 0);
}

TEST_CASE("SplitTree zero dim: recompute(0, 0) on a 2-pane horizontal split is non-negative",
    "[split_tree][zero]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Horizontal);

    tree.recompute(0, 0);
    check_no_negative_dimensions(tree);

    auto ad = tree.descriptor_for(a);
    auto bd = tree.descriptor_for(b);
    CHECK(ad.pixel_size.y == 0);
    CHECK(bd.pixel_size.y == 0);
    CHECK(ad.pixel_pos.y == 0);
    CHECK(bd.pixel_pos.y == 0);
}

TEST_CASE("SplitTree zero dim: layout recovers after recompute(0, 0)", "[split_tree][zero]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);

    // Squash to zero, then restore.
    tree.recompute(0, 0);
    tree.recompute(1000, 800);

    auto ad = tree.descriptor_for(a);
    auto bd = tree.descriptor_for(b);
    CHECK(ad.pixel_size.x == 498);
    CHECK(bd.pixel_size.x == 498);
    CHECK(ad.pixel_size.y == 800);
    CHECK(bd.pixel_size.y == 800);
    CHECK(bd.pixel_pos.x == 502);
}

TEST_CASE("SplitTree zero dim: set_divider_ratio + recompute(0, 0) keeps divider non-negative",
    "[split_tree][zero]")
{
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);

    auto hit = tree.hit_test(499, 400);
    REQUIRE(std::holds_alternative<SplitTree::DividerHit>(hit));
    tree.set_divider_ratio(std::get<SplitTree::DividerHit>(hit).id, 0.5f);

    tree.recompute(0, 0);

    bool saw_any = false;
    tree.for_each_divider([&](const SplitTree::DividerRect& d) {
        saw_any = true;
        CHECK(d.x >= 0);
        CHECK(d.y >= 0);
        CHECK(d.w >= 0);
        CHECK(d.h >= 0);
    });
    REQUIRE(saw_any);
    (void)b;
}

TEST_CASE("SplitTree zero dim: deeply nested tree stays non-negative at zero size",
    "[split_tree][zero]")
{
    // Three nested splits → 4 leaves. Worst case for tiny rectangles.
    SplitTree tree;
    auto a = tree.reset(800, 600);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);
    auto c = tree.split_leaf(b, SplitDirection::Horizontal);
    auto d = tree.split_leaf(c, SplitDirection::Vertical);

    tree.recompute(0, 0);
    check_no_negative_dimensions(tree);

    // And with a 1x1 origin offset for the tab bar reservation case.
    tree.recompute(1, 1, 0, 0);
    check_no_negative_dimensions(tree);
    (void)d;
}

TEST_CASE("SplitTree zero dim: pane smaller than divider stays in bounds (WI 08)",
    "[split_tree][zero]")
{
    // Regression: when w <= div_w on a vertical split, the second child used
    // to be placed at x + 1 + div_w (one pixel past the parent's right edge).
    // After WI 08 the second child must remain inside the parent rect.
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    auto b = tree.split_leaf(a, SplitDirection::Vertical);

    // 3 < kDividerWidth (4), so the divider cannot fit.
    tree.recompute(3, 800);

    auto ad = tree.descriptor_for(a);
    auto bd = tree.descriptor_for(b);

    CHECK(ad.pixel_pos.x >= 0);
    CHECK(bd.pixel_pos.x >= 0);
    CHECK(ad.pixel_pos.x + ad.pixel_size.x <= 3);
    CHECK(bd.pixel_pos.x + bd.pixel_size.x <= 3);
    CHECK(ad.pixel_size.x >= 0);
    CHECK(bd.pixel_size.x >= 0);

    // And the same for the horizontal axis.
    SplitTree htree;
    auto t = htree.reset(800, 1000);
    auto bot = htree.split_leaf(t, SplitDirection::Horizontal);

    htree.recompute(800, 3);

    auto td = htree.descriptor_for(t);
    auto bdh = htree.descriptor_for(bot);
    CHECK(td.pixel_pos.y + td.pixel_size.y <= 3);
    CHECK(bdh.pixel_pos.y + bdh.pixel_size.y <= 3);
    CHECK(td.pixel_size.y >= 0);
    CHECK(bdh.pixel_size.y >= 0);
}

TEST_CASE("SplitTree zero dim: negative dimensions are clamped, not propagated",
    "[split_tree][zero]")
{
    // Defensive: even though SDL never sends negative sizes, recompute() must
    // not produce undefined geometry if it ever does.
    SplitTree tree;
    auto a = tree.reset(1000, 800);
    tree.split_leaf(a, SplitDirection::Vertical);

    tree.recompute(-100, -50);
    check_no_negative_dimensions(tree);
}
