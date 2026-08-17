# Clear input routing before destroying a dead host

**Type:** test
**Priority:** 69

`App::close_dead_panes()` clears InputDispatcher routing before pane destruction, but the
ordering has no direct regression assertion. The old diagnostics/render-test scenario is
already covered by `tests/render_test_driver_tests.cpp`.

- [ ] Create a host that becomes dead after one pump and record destruction/input calls.
- [ ] Drive the close path and prove dispatcher routing is cleared before destruction.
- [ ] Verify no subsequent event reaches the dead host.
- [ ] Extend the closest App/tab invariant fixture rather than creating a second render-test harness.
