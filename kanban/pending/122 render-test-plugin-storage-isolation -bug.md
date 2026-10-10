# Isolate render tests from the user's plugin settings

**Summary:** Give render tests their own plugin storage so they neither read nor rewrite the user's real plugin preferences and layouts, which currently changes what product scenarios render and silently edits the user's settings.

**Priority:** P1 — render tests mutate user state and product references depend on it.

**Source:** `libs/draxul-host/src/plugin_storage.cpp` (empty `storage_root` falls back to the per-user Application Support folders); `libs/draxul-render-test/src/render_test.cpp` (`make_app_options` only disables the core user config).

Found on 2026-10-10 while reproducing MegaCity docking crashes. A MegaCity render test loaded `~/Library/Application Support/draxul/Plugin Config/dev.draxul.megacity/megacity-preferences.toml` (`show_ui_panels = false`, overlay settings) and `Plugins/dev.draxul.megacity/megacity_imgui.ini`, rendered a wireframe crosshatch, and wrote both files back on exit. With `HOME` pointed at an empty folder the same scenario rendered the textured city. This is the likely cause of `plugins/megacity/kanban/pending/18 city-render-reference-mismatch -test.md`. The user's two changed values were restored by hand.

- [ ] Pass a per-run temporary plugin storage root from render-test app options into `PluginHost`, removed after the run; keep normal launches unchanged.
- [ ] Re-run the MegaCity, SatView, ScoreView, Rezonality and Flashcards scenarios and re-bless only references whose change is explained by the isolation.
- [ ] Verify a render-test run leaves the user's plugin config/data folders untouched (timestamps and contents).
