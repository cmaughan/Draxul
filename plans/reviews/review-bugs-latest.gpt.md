Read-only static review of `repomix-output.xml`, with four read-only reviewers and coordinator verification. No files were changed; no builds, installations, tests, or project binaries were run. Coverage was representative across the repository, not exhaustive.

The packed Kanban metadata lists many cards whose bodies are absent. Clear overlaps with those tracked titles were excluded.

1. **CRITICAL — Terminal input can terminate the GUI during JSON serialization.**  
   **Location:** `libs/draxul-client/src/remote_session_coordinator.cpp:1659`  
   With legacy mouse reporting enabled, clicking zero-based column 95 generates raw byte `0x80`. Input reaches strict JSON serialization unchanged, throwing outside the worker’s exception boundary. Large Unicode pastes split inside a multibyte character reach the same failure.  
   **Fix:** Encode terminal input losslessly as binary-safe wire data and contain worker exceptions.

2. **CRITICAL — Plugin storage temporary files collide across UI processes.**  
   **Location:** `libs/draxul-host/src/plugin_storage.cpp:467`  
   Two clients attached to the same plugin pane share its storage path, but temporary filenames contain only a process-local counter. Concurrent saves can truncate the same temporary file or continue writing after another process publishes it, corrupting saved JSON.  
   **Fix:** Exclusively create temporary files with process-unique/random names before atomic replacement.

3. **CRITICAL — Non-finite configuration values reach integer conversions.**  
   **Location:** `libs/draxul-config/src/config_schema.cpp:553`  
   TOML `font_size = nan` survives the range checks and `std::clamp`. Application initialization forwards it to `FontManager`, whose conversion to `FT_F26Dot6` at `libs/draxul-font/src/font_manager.cpp:61` has undefined behavior. Checked reload accepts it too.  
   **Fix:** Reject non-finite floating-point configuration values before applying range rules.

4. **CRITICAL — Rezonality surface scales overflow GPU dimension conversions.**  
   **Location:** `plugins/rezonality/src/native_backend_vulkan.cpp:448`; `plugins/rezonality/src/native_backend_metal.mm:454`  
   A procedural surface with `scale: (1e30, 1)` passes parsing. Multiplying this by the pane width and converting to the backend’s integer dimension type causes undefined behavior. Image-storage validation does not cover procedural surfaces.  
   **Fix:** Validate scaled dimensions against representable and device limits before casting.

5. **CRITICAL — PCB routing margins bypass numeric bounds.**  
   **Location:** `plugins/pcbview/src/autorouter.cpp:243`  
   A schema-2 board with `routing.clearance_mm = 1e100` passes finite-number validation and the grid-cell budget. Routing then casts `ceil(via_radius / grid.step_mm)` to `int`, causing undefined behavior. The bounded grid does not bound clearance-derived radii.  
   **Fix:** Validate routing margins or clip their floating-point bounds before integer conversion.

6. **HIGH — Plugin shutdown rejects final state saves.**  
   **Location:** `libs/draxul-host/src/plugin_host.cpp:224`  
   Shutdown sets `shutting_down_` before calling plugin quiescence. Storage callbacks consequently fail through `callback_host()` at line 595. SatView saves panel preferences during quiescence, so changing settings and closing the pane loses them. Reload uses the same ordering.  
   **Fix:** Keep main-thread storage available through quiescence, then invalidate callbacks.

7. **HIGH — Atlas overflow corrupts text in other panes.**  
   **Location:** `libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp:303`  
   Grid hosts share one glyph atlas. Overflow resets that atlas, but only the pipeline consuming the reset rebuilds its cells. Other clean panes retain obsolete glyph coordinates and display missing or incorrect text. The multi-host test manually invalidates both grids; production lacks that broadcast.  
   **Fix:** Track atlas generations and invalidate every dependent grid after a reset.

8. **HIGH — Neovim receives ordinary Space and Backslash twice.**  
   **Location:** `libs/draxul-nvim/src/input.cpp:122`  
   Printable keydown events send `<Space>` or `<Bslash>`. Line 183 suppresses the following text event only for Ctrl/Alt, so ordinary SDL text input sends the character again. Insert mode receives duplicate characters.  
   **Fix:** Handle unmodified printable characters through text input alone, or suppress their matching text events.

9. **HIGH — Windows Neovim launch misinterprets Unicode paths.**  
   **Location:** `libs/draxul-nvim/src/nvim_process.cpp:169`  
   UTF-8 executable, argument, and working-directory strings are passed to `CreateProcessA`. On Windows with a non-UTF-8 ANSI code page, paths such as `C:\用户\...` fail or resolve incorrectly, and Unicode arguments are mangled.  
   **Fix:** Convert to UTF-16 and use `CreateProcessW` with a matching environment block.

10. **HIGH — Plugin resource paths violate the UTF-8 ABI contract on Windows.**  
    **Location:** `libs/draxul-host/src/plugin_host.cpp:117`  
    `manifest.directory.string()` populates `plugin_directory_utf8`, while plugin consumers decode it using `u8path()`. A non-ASCII staging directory on a non-UTF-8 Windows code page can therefore cause conversion failures or missing assets.  
    **Fix:** Populate the ABI field from `u8string()`.

11. **HIGH — Disabled keybindings return after restarting.**  
    **Location:** `libs/draxul-config/src/app_config_io.cpp:376`  
    Setting `[keybindings] copy = ""` correctly removes the binding in memory. Serialization omits missing bindings, and shutdown replaces the entire keybindings table with that serialization. On the next launch, the omitted action regains its default binding.  
    **Fix:** Serialize explicit empty values for disabled default actions.

12. **HIGH — Integration installation can invalidate valid Codex configuration.**  
    **Location:** `libs/draxul-agent-integration/src/agent_integration.cpp:322`  
    A valid header such as `[features] # comment` is not recognized because the line does not end in `]`. Installation appends another `[features]` table and saves the result, producing invalid TOML with a duplicate table.  
    **Fix:** Parse TOML section structure correctly while preserving unrelated configuration.

13. **HIGH — Kanban navigation continues after losing focus.**  
    **Location:** `modules/kanban/draxul-kanban/src/kanban_host.cpp:300`  
    Hold a navigation key, switch tabs, then release it. The release reaches the new host, while Kanban retains its repeat command. Background pumping continues moving its selection; a pinned preview can also invoke callbacks against the currently active pane manager.  
    **Fix:** Clear held-key and navigation state on focus loss, and prevent unfocused repeat execution.

14. **HIGH — MegaCity preferences never load or persist through its plugin wrapper.**  
    **Location:** `plugins/megacity/src/megacity_plugin.cpp:143`  
    The wrapper leaves `config_document` null. `MegaCityHost` consequently initializes with defaults, and its settings-saving helper immediately returns. Rendering and camera changes disappear when the pane is reopened.  
    **Fix:** Connect initialization and saving to plugin-owned durable storage.

15. **HIGH — SatView’s sky labels are disabled in production plugin instances.**  
    **Location:** `plugins/satview/src/runtime/satview_runtime.cpp:2286`  
    The wrapper supplies no host text-service pointer. `refresh_scene_text_service()` therefore always returns, even after UI-style synchronization supplies a font path. Enabling constellation or cardinal labels never creates their text atlas.  
    **Fix:** Initialize the product-owned text service from the supplied font/style information.

16. **HIGH — Restored SatView settings do not update its simulation worker.**  
    **Location:** `plugins/satview/src/satview_plugin.cpp:200`  
    Initialization starts the worker with defaults, then applies saved preferences only to runtime fields. A restored nondefault time speed or track budget appears in the UI while the worker retains its initial settings. Ordinary pumping does not synchronize them.  
    **Fix:** Restore before worker startup or explicitly synchronize all restored controls afterward.

17. **HIGH — SatView can resume simulation without resuming presentation.**  
    **Location:** `plugins/satview/src/satview_plugin.cpp:361`  
    Press Space to pause, then click the panel’s Resume button. The button clears runtime pause state, but the wrapper remains paused and stops scheduling ticks. The worker resumes without waking presentation. Captured Space input can also desynchronize these flags.  
    **Fix:** Use one authoritative pause state for input, persistence, and scheduling.

18. **HIGH — SatView cloud refresh races with Metal texture sampling.**  
    **Location:** `plugins/satview/src/render/satview_render.mm:245`  
    A same-sized live-cloud refresh calls CPU `replaceRegion` on the existing texture. The previous frame can still be sampling it: the renderer permits two frames in flight and waits only for an available slot. This permits corrupted cloud rendering.  
    **Fix:** Upload into a replacement texture or synchronize against all outstanding readers.

19. **HIGH — ScoreView judges MIDI delivery time instead of arrival time.**  
    **Location:** `plugins/scoreview/product/draxul-scoreview/src/flow_controller.cpp:323`  
    Roll judgment ignores recorded event timestamps. Runtime pumping also expires note windows before polling queued input. At 60 QPM, a valid note arriving at quarter 1.44 for onset 1 can become a miss when processed at 1.46, after the 0.45-quarter window closes.  
    **Fix:** Judge timestamped events before expiring their corresponding windows.

20. **HIGH — Rezonality animation advances across paused time.**  
    **Location:** `plugins/rezonality/src/rezonality_plugin.cpp:191`  
    Resuming clears `paused` without resetting the animation clock. The next render adds the interval since the last paused render, making animation jump forward. Paused rendering supplies no animation deadline, so this interval can be substantial in an idle pane.  
    **Fix:** Reanchor the animation clock on pause/resume.

21. **HIGH — Rezonality misses edits to valid shader includes.**  
    **Location:** `plugins/rezonality/src/live_project.cpp:1189`  
    The watcher fingerprints an extension whitelist that excludes `.inc`. A shader can successfully include `parameters.inc`, but subsequent edits solely to that file leave the fingerprint unchanged and never rebuild the shader.  
    **Fix:** Watch the actual dependency closure, including arbitrary include suffixes.

22. **HIGH — Nonuniform model scaling leaves incorrect normals and tangents.**  
    **Location:** `plugins/rezonality/src/model_loader.cpp:211`  
    Model scale is baked into positions while normals, tangents, and bitangents remain unchanged. With supported scale `(1,2,3)`, sloped surfaces receive incorrect lighting and normal mapping; the identity model uniform cannot compensate.  
    **Fix:** Apply inverse-transpose normal transformation and transform/reorthogonalize the tangent basis.

23. **MEDIUM — Rejected pane updates still mutate topology.**  
    **Location:** `libs/draxul-server/src/topology_service.cpp:1201`  
    An update with a different plugin ID writes new working-directory/source fields before returning `plugin_id_mismatch`. The failed command skips the revision increment, leaving changed topology under its old revision.  
    **Fix:** Validate immutable identities before modifying any pane fields.

24. **MEDIUM — Font configuration reload leaks native resources.**  
    **Location:** `libs/draxul-font/src/text_service.cpp:83`; `app/app.cpp:695`  
    Successful reload move-assigns a staged service over the live service. Default destruction does not call font shutdown; `FontManager` retains raw FreeType/HarfBuzz resources. Repeated font reloads accumulate allocations.  
    **Fix:** Give text-service internals RAII cleanup that also handles move replacement safely.

25. **MEDIUM — Invalid cloud downloads overwrite the last good cache.**  
    **Location:** `plugins/satview/src/services/satview_cloud_service.cpp:287`  
    A nonempty HTTP response replaces the cache before image decoding. If decoding fails, fallback rereads the corrupted replacement. After restart, its fresh timestamp can suppress another download while the cache remains unusable.  
    **Fix:** Validate downloaded image data before publishing it over the cache.

26. **MEDIUM — ScoreView’s `notick` mode enables ticks.**  
    **Location:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp:269`  
    `roll-notick` first disables the metronome, then the independent `"tick"` substring check matches `"notick"` and enables Beats. Playback consequently produces ticks despite the requested mode.  
    **Fix:** Parse exact tokens or make the tick choices mutually exclusive.

27. **MEDIUM — Hidden PCB routes still intercept selection.**  
    **Location:** `plugins/pcbview/src/selection.cpp:90`  
    Disabling “Show routed tracks” hides their drawing but leaves route picking enabled. Clicking an invisible route can select it, dim other content, and intercept a visible airwire.  
    **Fix:** Pass routed-track visibility into selection and gate route picking accordingly.

28. **MEDIUM — Disabling weather leaves its stale pill visible.**  
    **Location:** `libs/draxul-weather/src/weather_service.cpp:49`  
    After weather loads, set `weather_location = ""` and reload configuration. The worker stops, but its temperature and emoji remain populated. Chrome renders those values without checking the disabled configuration, leaving a permanently stale weather pill.  
    **Fix:** Clear published weather state when disabling/changing location, or gate chrome presentation on configuration.

**Unresolved leads—not accepted findings:** SatView’s Vulkan label-texture retirement needs an independently reachable upload path; current production label creation is blocked by finding 15. The suspected IME coordinate mismatch needs a complete mixed-DPI coordinate trace. Neither warrants a new card from this review alone.

**Coverage and remaining gaps**

All delegated work completed; there were no delegation failures. Accepted worker findings were checked against source and surrounding behavior by the coordinator.

| Area | Representative paths/flows examined | Inspector | Remaining gaps |
|---|---|---|---|
| Application/configuration | `app.cpp`, pane/Space/topology projection, input dispatch, config parse/save/reload | Coordinator | Complete CLI/status and palette paths |
| Agents/weather | Agent projections, integration installer, weather lifecycle and chrome presentation | Coordinator | Complete discovery rules and native HTTP internals |
| Client/server/control | Stream/poll input, topology mutation, lifecycle, persistence, framing | Session reviewer + coordinator | Complete dispatch, validation and Windows synchronous transport |
| Terminal/Neovim | PTY/ConPTY ownership, input/mouse/paste, RPC/process lifecycle, representative tests | Session reviewer + coordinator | Complete redraw, snapshot and SGR handling |
| Rendering/fonts/platforms | Vulkan/Metal frame ownership, atlas reset, font lifetime, SDL events | Renderer reviewer + coordinator | Complete shaders, GPU failure paths, rasterization and printing |
| SDK/plugin infrastructure | Loading/staging, callbacks, storage, reload, support adapters, reference plugin | Renderer reviewer + coordinator | Complete support backends and external-plugin behavior |
| Built-in modules | Kanban scan/mutation/focus/preview; Markdown parsing, host and layout flows | Coordinator | Native file-monitor backends and Markdown GPU passes |
| MegaCity | Wrapper/preferences, scanner, semantic publication, scene/resource paths | Product reviewer A + coordinator | Full geometry, layout algorithms, panels and shaders |
| SatView | Restore/input/ticks, simulation, cloud/cache, text and texture flows | Product reviewer A + coordinator | Full astronomy, catalog parsing and shader parity |
| ScoreView | MIDI/audio lifecycle, importer/engraving ownership, transport and judgment | Product reviewer B + coordinator | Full composer, DSP, canvas and analysis internals |
| PCBView | Board validation, routing bounds, runtime selection and visibility | Product reviewer B + coordinator | Complete routing search/legalization algorithms |
| Rezonality | Project/watch pipeline, activation, animation, models and platform resource creation | Product reviewer B + coordinator | Complete ray/pipeline implementations and Neovim integration |
| Build/tests/docs/tracker | Root/product CMake, `do.py`, package publication, relevant tests and guidance | Coordinator + reviewers | Full script/test inventory; most indexed Kanban card bodies absent |
