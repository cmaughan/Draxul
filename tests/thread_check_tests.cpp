#include <draxul/grid.h>
#include <draxul/thread_check.h>

#include <catch2/catch_all.hpp>

using namespace draxul;

TEST_CASE("MainThreadChecker passes on constructing thread", "[thread_check]")
{
    MainThreadChecker checker;
    // Should not fire — we are on the constructing thread.
    checker.assert_main_thread("test: same thread");
}

TEST_CASE("Grid thread assertions do not fire on constructing thread", "[thread_check]")
{
    Grid grid;
    grid.resize(10, 5);
    grid.set_cell(0, 0, "A", 0, false);
    grid.scroll(0, 5, 0, 10, 1);
    grid.mark_dirty(0, 0);
    grid.mark_all_dirty();
    grid.clear_dirty();
    // If we got here without aborting, all assertions passed on the main thread.
}
