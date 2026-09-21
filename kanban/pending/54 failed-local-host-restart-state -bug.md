# Keep a valid pane after local host restart failure

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`app/pane_manager.cpp:409` erases the old host before recreation. A failed non-plugin initialization at `:1405` removes its pane ID but leaves the tree leaf and launch options. Restarting Neovim after removing its executable can leave a pane that cannot be closed or restarted normally.

**Investigation**

- [ ] Fail recreation after an existing local host has been shut down.

**Fix strategy**

- [ ] Install a recoverable unavailable placeholder or perform complete structural rollback.
- [ ] Keep tree, host, pane identity, launch options, and input routing consistent.

**Acceptance criteria**

- [ ] The failed pane remains closable and retryable.
- [ ] Session snapshotting and other panes continue working.
- [ ] Preserve existing missing-plugin placeholder behavior.
