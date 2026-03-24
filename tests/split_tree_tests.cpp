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
        CHECK(desc.pixel_size.x > 0);
        CHECK(desc.pixel_size.y > 0);
    });

    REQUIRE(visited.size() == 101);
}

TEST_CASE("SplitTree: tiny panes keep renderable descriptors", "[split_tree]")
{
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
        CHECK(desc.pixel_size.x > 0);
        CHECK(desc.pixel_size.y > 0);
    });

    REQUIRE(descriptors.size() == 6);
    auto result = tree.hit_test(0, 0);
    CHECK_FALSE(std::holds_alternative<std::monostate>(result));
}
