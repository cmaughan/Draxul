# Preserve plugin discovery IDs across refresh

Found while completing `00 test-graph-and-scope -refactor.md`: the existing bundled-plugin CLI smoke, newly included in the complete integration inventory, reported an installed Spinning Triangle plugin as missing.

`PluginManager::load` copies its ID but looked up the original borrowed view after `refresh` replaced the manifests. `prepare_reload` had the same lifetime risk. The CLI also iterated that replaced vector while loading.

- [x] Own the lookup ID across refresh in load/reload and snapshot manifests for CLI iteration.
- [x] Extend the existing real native-module integration case to load and reload using IDs borrowed from the inventory.
- [x] Pass aggregate integration tests, including bundled-plugin discovery, and same-cache smoke.

Validation is shared with `00 test-graph-and-scope -refactor.md`; no separate test-only aggregate is required. Platform-neutral lifetime fix; Windows remains covered by normal CI.

Evidence: the corrected bundled-plugin CLI smoke and real native-module load/reload integration passed in the 72-entry Debug aggregate; same-cache startup smoke passed in 1.68 s. The unrelated SDK fixture was subsequently corrected and its targeted CTest passed in 231.09 s.

- [x] Complete final Release startup confirmation (app build and smoke passed in 35.54 s total).
