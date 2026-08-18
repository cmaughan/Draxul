# Preserve focused panes across tab switches

**Type:** test
**Priority:** 00

## Gap

Current tab invariants cover active tab identities, but do not directly prove that each
tab's `PaneManager` restores its own focused host and that input follows it after repeated
switches and pane closure.

## Work

- [ ] Create two `TabController` tabs with distinct multi-pane `PaneManager` state.
- [ ] Focus a different leaf in each tab and switch both directions repeatedly.
- [ ] Verify `PaneManager::focused_host()` and InputDispatcher routing follow the active tab.
- [ ] Close the focused pane in one tab and prove the other tab's focus is unaffected.
- [ ] Keep the test renderer- and Neovim-free using current App/PaneManager fixtures.
