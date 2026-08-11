// scrollback_viewport_resize_tests.cpp
//
// Tests for ScrollbackBuffer viewport correctness when the terminal column
// count changes via resize().
//
// POLICY (WI 98): resize() PRESERVES existing scrollback rows. Columns are
// clamped (when shrinking) or extended with default cells (when growing),
// and the viewport offset is reset to 0 because row layout has changed.
// Horizontal terminal resizes therefore do not erase the user's history.
//
// API used:
//   ScrollbackBuffer::resize(int cols)   — reallocate; preserves rows
//   ScrollbackBuffer::next_write_slot()  — get writable row pointer
//   ScrollbackBuffer::commit_push()      — commit written row
//   ScrollbackBuffer::cols() const       — column width of the ring buffer
//   ScrollbackBuffer::size() const       — number of rows stored
//   ScrollbackBuffer::is_scrolled_back() — true if viewport offset > 0
//   ScrollbackBuffer::scroll(int delta)  — scroll viewport
//   ScrollbackBuffer::reset()            — clear state, keep storage

#include <catch2/catch_test_macros.hpp>

#include <draxul/grid.h>
#include <draxul/scrollback_buffer.h>

#include <string>
#include <vector>

using namespace draxul;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

// Build a minimal Callbacks struct with no-op grid accessors.
// The resize tests do not exercise scroll() or scroll_to_live(), so the
// callbacks are never actually invoked.  We provide them anyway so that a
// future test that does trigger update_display() or restore_live_snapshot()
// will not crash with a null functor.
ScrollbackBuffer::Callbacks make_noop_callbacks(int cols = 80, int rows = 24)
{
    // A small shared cell store to satisfy get_cell / set_cell.
    auto cells = std::make_shared<std::vector<Cell>>(
        static_cast<size_t>(cols) * rows, Cell{});
    const int stored_cols = cols;
    const int stored_rows = rows;

    ScrollbackBuffer::Callbacks cbs;
    cbs.grid_cols = [stored_cols]() { return stored_cols; };
    cbs.grid_rows = [stored_rows]() { return stored_rows; };
    cbs.get_cell = [cells, stored_cols](int col, int row) -> Cell {
        const size_t idx = static_cast<size_t>(row) * stored_cols + col;
        if (idx < cells->size())
            return (*cells)[idx];
        return Cell{};
    };
    cbs.set_cell = [cells, stored_cols](int col, int row, const Cell& c) {
        const size_t idx = static_cast<size_t>(row) * stored_cols + col;
        if (idx < cells->size())
            (*cells)[idx] = c;
    };
    cbs.force_full_redraw = [] {};
    cbs.flush_grid = [] {};
    return cbs;
}

// Push `count` rows of default-constructed cells into `buf`.
// Caller must have called resize() first so that next_write_slot() is valid.
void push_rows(ScrollbackBuffer& buf, int count)
{
    for (int i = 0; i < count; ++i)
    {
        Cell* slot = buf.next_write_slot();
        REQUIRE(slot != nullptr);
        // Leave cells default-initialised — content does not matter for these tests.
        buf.commit_push();
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("scrollback resize: resize preserves existing rows", "[scrollback]")
{
    // WI 98: resize() preserves stored rows across column changes so the
    // user's scrollback history survives horizontal terminal resizes.
    ScrollbackBuffer buf(make_noop_callbacks(80, 24));

    buf.resize(80);
    push_rows(buf, 50);
    REQUIRE(buf.size() == 50);

    // Resize to a narrower column count — rows are preserved (columns clamped).
    buf.resize(40);
    REQUIRE(buf.size() == 50);
    REQUIRE(buf.cols() == 40);
}

TEST_CASE("scrollback resize: viewport offset reset after resize", "[scrollback]")
{
    // After resize() the viewport offset is reset to 0 because row layout has
    // changed, even though the rows themselves are preserved. This avoids
    // leaving the viewport pointing into a stale row mapping.
    ScrollbackBuffer buf(make_noop_callbacks(80, 24));

    buf.resize(80);
    push_rows(buf, 30);
    REQUIRE(buf.size() == 30);

    // Scroll back into the history.
    buf.scroll(10);
    REQUIRE(buf.is_scrolled_back());

    // Resize — preserves rows but resets offset.
    buf.resize(120);

    REQUIRE_FALSE(buf.is_scrolled_back());
    REQUIRE(buf.size() == 30);
}

TEST_CASE("scrollback resize: cols() reflects new width after resize", "[scrollback]")
{
    // cols() must always reflect the width passed to the most recent resize()
    // call, regardless of how many rows are stored.
    ScrollbackBuffer buf(make_noop_callbacks(80, 24));

    buf.resize(80);
    REQUIRE(buf.cols() == 80);

    buf.resize(120);
    REQUIRE(buf.cols() == 120);

    // Can also shrink.
    buf.resize(40);
    REQUIRE(buf.cols() == 40);
}

TEST_CASE("scrollback resize: resize to same cols is safe", "[scrollback]")
{
    // Calling resize() with the same column count that is already active must
    // be a no-op (the implementation early-exits when cols == cols_).
    // Verify: no crash, and rows_stored() is still 0 if called on fresh buffer.
    ScrollbackBuffer buf(make_noop_callbacks(80, 24));

    buf.resize(80);
    REQUIRE(buf.size() == 0);

    // Second resize to the same width — must be safe and not change state.
    buf.resize(80);
    REQUIRE(buf.size() == 0);
    REQUIRE(buf.cols() == 80);
}

TEST_CASE("scrollback resize: rows pushed before resize remain accessible after resize",
    "[scrollback]")
{
    // WI 98: rows pushed before a resize are preserved and remain accessible.
    // Subsequent pushes use the new column width and accumulate on top.
    ScrollbackBuffer buf(make_noop_callbacks(80, 24));

    buf.resize(80);
    push_rows(buf, 10);
    REQUIRE(buf.size() == 10);

    buf.resize(40);
    REQUIRE(buf.size() == 10);
    REQUIRE(buf.cols() == 40);

    // Push more rows using the new column width — should accumulate.
    push_rows(buf, 5);
    REQUIRE(buf.size() == 15);
    REQUIRE(buf.cols() == 40);
}

TEST_CASE("scrollback storage can degrade and be initialized again",
    "[scrollback][resource-bounds]")
{
    ScrollbackBuffer buf(
        make_noop_callbacks(80, 24));
    buf.resize(80);
    push_rows(buf, 4);
    REQUIRE(buf.size() == 4);
    REQUIRE(buf.next_write_slot() != nullptr);

    buf.release_storage();
    CHECK(buf.cols() == 0);
    CHECK(buf.size() == 0);
    CHECK(buf.next_write_slot() == nullptr);

    buf.resize(40);
    CHECK(buf.cols() == 40);
    CHECK(buf.size() == 0);
    CHECK(buf.next_write_slot() != nullptr);
}
