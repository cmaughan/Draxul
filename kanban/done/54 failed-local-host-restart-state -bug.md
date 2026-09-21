# Keep a valid pane after local host restart failure

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`app/pane_manager.cpp:409` erases the old host before recreation. A failed non-plugin initialization at `:1405` removes its pane ID but leaves the tree leaf and launch options. Restarting Neovim after removing its executable can leave a pane that cannot be closed or restarted normally.

**Investigation**

- [x] Fail recreation after an existing local host has been shut down.

**Fix strategy**

- [x] Install a recoverable unavailable placeholder or perform complete structural rollback.
- [x] Keep tree, host, pane identity, launch options, and input routing consistent.

**Acceptance criteria**

- [x] The failed pane remains closable and retryable.
- [x] Session snapshotting and other panes continue working.
- [x] Preserve existing missing-plugin placeholder behavior.
