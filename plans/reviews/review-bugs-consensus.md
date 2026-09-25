**28 bugs confirmed: 5 CRITICAL, 17 HIGH, and 6 MEDIUM.** No reported bug was rejected; two unresolved leads remain excluded from work items.

Both indexed inputs were read completely. `01-anthropic-claude-claude-fable-5-1.md` contains only a completion notice, so it provides no review evidence or corroboration. All findings below originate in `02-openai-codex-gpt-6-astra.md`. This is therefore source-verified triage of the Codex findings, not agreement between two substantive reports.

Three read-only verification agents completed their assigned groups. Their supporting source was re-read before accepting findings. No files were changed, and no builds, tests, installations, or project binaries were run.

**Architecture and coverage**

| Boundary | Relevant architecture and verification |
|---|---|
| Application and configuration | App owns configuration reload, shared text services, input dispatch, chrome, and background host pumping. Checked parsing, save/merge behavior, focus transitions, and weather presentation. |
| Session and terminal input | Client coordinator serializes terminal commands; server owns revisioned topology. Checked input-byte transport, worker exception boundaries, and rejected topology mutations. Neovim has separate input and process adapters. |
| Fonts and native plugins | Grid pipelines share a glyph atlas. `PluginHost` owns ABI callbacks and lifecycle; `PluginStorage` owns persistence. Checked atlas invalidation, native font ownership, storage publication, shutdown callbacks, and Windows path encoding. |
| Products | Plugins own their runtime, services, and rendering. Checked MegaCity preference wiring; SatView settings, scheduling, labels, clouds; ScoreView timing and launch modes; PCBView routing and picking; Rezonality dimensions, animation, dependencies, and model transforms. |

The substantive report’s coverage was representative, not exhaustive. This synthesis verified its reported defects and relevant call paths; it does not establish repository-wide correctness. Windows behavior and GPU concurrency were inspected statically, without runtime reproduction.

**Tracker reconciliation**

Root `kanban/.draxul-kanban.toml` lists pending and ice-box filenames, but most corresponding card bodies are absent. The supplied done lane contains `134 default-kanban-column-order -feature.md`, which is unrelated. Rezonality’s product tracker metadata has empty lanes; other product tracker lanes are absent.

No available title establishes an exact duplicate of an accepted finding. Related existing work must remain separate:

| Existing card | Relationship |
|---|---|
| `kanban/pending/26 protocol-validation-hardening -bug.md` | Broad protocol validation; does not establish coverage of binary terminal-input serialization. |
| `kanban/pending/41 dynamic-plugin-hot-reload -feature.md` | Related lifecycle work; final storage callbacks are specifically rejected during quiescence. |
| `kanban/ice-box/27 atlas-dynamic-growth -feature.md` | Atlas capacity work; independent of invalidating other consumers after a reset. |
| `kanban/pending/31 pcbview-routing-cell-budget -bug.md` | Bounds grid allocation; does not bound clearance-derived integer conversions. |
| `kanban/pending/42 rezonality-image-storage-format -bug.md` | Image upload validation; procedural surface dimensions bypass those checks. |
| `kanban/pending/36 satview-vulkan-stream-buffer-lifetime -bug.md` | Vulkan stream buffers; distinct from mutation of a shared Metal cloud texture. |
| `kanban/pending/49 satview-concurrent-cache-publication -bug.md` | Concurrent writers; distinct from replacing a valid cache with undecodable bytes. |
| `kanban/ice-box/10 inputdispatcher-focus-loss -bug.md` | Dispatcher focus handling; Kanban retains its own repeat/navigation state after receiving focus loss. |
| `kanban/pending/88 pcbview-selection-core -refactor.md` | Selection extraction; does not establish coverage of missing routed-track visibility. |
| `kanban/pending/85 agent-integration-installer -refactor.md`, `kanban/pending/86 weather-service-library -refactor.md`, `kanban/pending/97 font-native-dependency-visibility -refactor.md` | Adjacent refactors, not evidence that the specific defects below are tracked. |

Missing card bodies prevent definitive full-scope deduplication. The proposed cards cover accepted defects with no demonstrated existing match; they do not recreate the indexed work above. Requested numbering starts at `00`, although existing metadata already uses some of these sequence numbers.

**Confirmed findings, ordered by severity**

1. **CRITICAL — Terminal input can terminate the GUI.**  
   **Location:** `libs/draxul-client/src/remote_session_coordinator.cpp:1659`. Strict JSON serialization receives arbitrary terminal bytes, and its exception escapes the worker. **Trigger:** legacy mouse reporting at zero-based column 95 emits `0x80`; byte-sized Unicode paste chunks can also contain invalid UTF-8 fragments. **Fix:** use lossless binary-safe input encoding across client/server and contain worker exceptions. **Reporter:** Codex #1.

2. **CRITICAL — Plugin storage temporary files collide across processes.**  
   **Location:** `libs/draxul-host/src/plugin_storage.cpp:467`. Temporary names use a process-local counter and are opened with truncation. **Trigger:** two clients save the same plugin/pane state concurrently; one can truncate the other’s temporary file or keep writing after it is published. **Fix:** exclusively create writer-unique temporary files, then atomically replace the target. **Reporter:** Codex #2.

3. **CRITICAL — Non-finite configuration reaches undefined integer conversion.**  
   **Location:** `libs/draxul-config/src/config_schema.cpp:553`; conversion at `libs/draxul-font/src/font_manager.cpp:61`. Float range rules accept NaN. **Trigger:** `font_size = nan` survives startup or checked reload and reaches conversion to `FT_F26Dot6`. **Fix:** reject non-finite values before range handling and defensively validate font inputs. **Reporter:** Codex #3.

4. **CRITICAL — Rezonality surface scales overflow dimension conversions.**  
   **Location:** `plugins/rezonality/src/native_backend_vulkan.cpp:448`; `plugins/rezonality/src/native_backend_metal.mm:454`. Both backends cast unchecked scaled dimensions. **Trigger:** a procedural surface with `scale: (1e30, 1)` passes parsing but produces an unrepresentable dimension. **Fix:** validate the computed dimensions against numeric and device limits before casting, preserving the last valid generation. **Reporter:** Codex #4.

5. **CRITICAL — PCB routing margins bypass numeric bounds.**  
   **Location:** `plugins/pcbview/src/autorouter.cpp:243`. Grid-cell limits do not bound clearance-derived radii or subsequent integer arithmetic. **Trigger:** an otherwise ordinary schema-2 board with a mount and `clearance_mm = 1e100` reaches an undefined floating-to-integer conversion. **Fix:** validate derived margins or clip floating bounds to the finite grid before conversion. **Reporter:** Codex #5.

6. **HIGH — Plugin shutdown rejects final state saves.**  
   **Location:** `libs/draxul-host/src/plugin_host.cpp:224`; callback rejection at `:595`. Shutdown disables callbacks before quiescing the plugin; reload repeats this ordering. **Trigger:** change SatView panel settings and close the pane; its quiescence save is rejected. **Fix:** permit valid main-thread storage calls through quiescence, then retire callbacks. **Reporter:** Codex #6.

7. **HIGH — Atlas overflow leaves other panes using obsolete glyph coordinates.**  
   **Location:** `libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp:303`. One pipeline consumes the shared reset flag and rebuilds only its own grid. **Trigger:** overflow the atlas in one pane while another remains clean. **Fix:** track atlas generations and invalidate every dependent consumer, including previously processed and hidden grids. **Reporter:** Codex #7.

8. **HIGH — Neovim receives ordinary Space and Backslash twice.**  
   **Location:** `libs/draxul-nvim/src/input.cpp:122`, `:128`, and `:183`. Keydown sends named printable keys without suppressing their ordinary text events. **Trigger:** type Space or Backslash in insert mode. **Fix:** deliver unmodified printable characters through text input once, retaining modified-key handling. **Reporter:** Codex #8.

9. **HIGH — Windows Neovim launch misinterprets Unicode paths and arguments.**  
   **Location:** `libs/draxul-nvim/src/nvim_process.cpp:169`. UTF-8 strings are passed to `CreateProcessA`. **Trigger:** launch using a path or argument outside the Windows ANSI code page, such as `C:\用户\...`. **Fix:** convert to UTF-16 and use `CreateProcessW` with a compatible Unicode environment block. **Reporter:** Codex #9.

10. **HIGH — Plugin resource paths violate the Windows UTF-8 ABI contract.**  
    **Location:** `libs/draxul-host/src/plugin_host.cpp:117`. A native `.string()` is published as `plugin_directory_utf8`, while consumers decode UTF-8. **Trigger:** stage plugins beneath a non-ASCII directory on a non-UTF-8 Windows code page. **Fix:** publish explicit UTF-8 bytes from `u8string()`. **Reporter:** Codex #10.

11. **HIGH — Disabled keybindings return after restart.**  
    **Location:** `libs/draxul-config/src/app_config_io.cpp:376`. Serialization omits removed bindings, and configuration merging replaces the entire keybindings table. **Trigger:** configure `copy = ""`, then exit and restart; the omitted action regains its default. **Fix:** serialize explicit empty values for disabled default actions. **Reporter:** Codex #11.

12. **HIGH — Integration installation can invalidate valid Codex TOML.**  
    **Location:** `libs/draxul-agent-integration/src/agent_integration.cpp:322`. Section recognition requires the line to end with `]`. **Trigger:** install into a configuration containing `[features] # comment`; another `[features]` table is appended, producing invalid TOML. **Fix:** recognize TOML structure correctly and validate the transformed document before publication. **Reporter:** Codex #12.

13. **HIGH — Kanban navigation repeats after focus loss.**  
    **Location:** `modules/kanban/draxul-kanban/src/kanban_host.cpp:300`; repeat execution at `:822`. Focus loss leaves held-key state intact, and background pumping continues repeats. **Trigger:** hold navigation, switch tabs, then release in the new host. **Fix:** clear repeat/navigation state on focus loss and prevent unfocused repeats and preview effects. **Reporter:** Codex #13.

14. **HIGH — MegaCity preferences neither load nor persist through its wrapper.**  
    **Location:** `plugins/megacity/src/megacity_plugin.cpp:143`. The wrapper leaves `config_document` null; the host loads defaults and its save helper returns immediately. **Trigger:** change renderer/camera preferences, close, and reopen. **Fix:** connect initialization and saving to product-owned durable storage. **Reporter:** Codex #14.

15. **HIGH — SatView sky labels cannot initialize in production plugin instances.**  
    **Location:** `plugins/satview/src/runtime/satview_runtime.cpp:2286`. Label text initialization requires a host text-service pointer that the wrapper never supplies. **Trigger:** enable cardinal or constellation labels; supplying a font through UI style does not satisfy the guard. **Fix:** initialize the product-owned text service from available font/style information. **Reporter:** Codex #15.

16. **HIGH — Restored SatView settings do not reach its simulation worker.**  
    **Location:** `plugins/satview/src/satview_plugin.cpp:200`. Saved configuration is applied after worker startup without synchronizing controls or marking render settings dirty. **Trigger:** restore nondefault time speed or track budgets; UI values change while worker values remain at defaults. **Fix:** restore before startup or explicitly synchronize all affected worker settings afterward. **Reporter:** Codex #16.

17. **HIGH — SatView pause state diverges between simulation and scheduling.**  
    **Location:** `plugins/satview/src/satview_plugin.cpp:361`. Wrapper scheduling uses a separate pause flag from the runtime panel. **Trigger:** pause with Space, then click Resume; the worker resumes while continuous presentation remains disabled. Captured Space can also desynchronize the flags. **Fix:** use one authoritative pause state for input, scheduling, and persistence. **Reporter:** Codex #17.

18. **HIGH — SatView cloud refresh races with Metal sampling.**  
    **Location:** `plugins/satview/src/render/satview_render.mm:245`. A same-sized refresh overwrites the persistent texture with CPU `replaceRegion` while an earlier frame may still sample it. **Trigger:** refresh clouds during normal asynchronous rendering with two frames in flight. **Fix:** publish a replacement texture with safe lifetime management or synchronize all outstanding readers. **Reporter:** Codex #18.

19. **HIGH — ScoreView judges MIDI processing time instead of arrival time.**  
    **Location:** `plugins/scoreview/product/draxul-scoreview/src/flow_controller.cpp:323`; pump ordering at `score_runtime.cpp:1517`. Judgment ignores recorded timestamps, and windows expire before queued events are polled. **Trigger:** at 60 QPM, a note arriving at quarter 1.44 for onset 1 is processed at 1.46, beyond the 0.45-quarter late window. **Fix:** map event timestamps to transport position and judge before expiring their windows. **Reporter:** Codex #19.

20. **HIGH — Rezonality animation includes paused time after resume.**  
    **Location:** `plugins/rezonality/src/rezonality_plugin.cpp:191`; pause toggle at `:306`. Resume does not reset the animation clock anchor. **Trigger:** pause an idle pane for several seconds, then resume; the next frame adds the elapsed paused interval. **Fix:** reanchor time on pause/resume transitions. **Reporter:** Codex #20.

21. **HIGH — Rezonality ignores edits to valid shader includes.**  
    **Location:** `plugins/rezonality/src/live_project.cpp:1189`. The fingerprint extension whitelist excludes valid include suffixes such as `.inc`. **Trigger:** compile a shader including `parameters.inc`, then edit only that file; no rebuild is scheduled. **Fix:** watch the actual dependency closure or use a conservative strategy covering arbitrary include suffixes. **Reporter:** Codex #21.

22. **HIGH — Nonuniform model scaling leaves incorrect shading vectors.**  
    **Location:** `plugins/rezonality/src/model_loader.cpp:211`. Positions are scaled while normals, tangents, and bitangents remain unchanged. **Trigger:** apply `(1,2,3)` scale to a sloped, normal-mapped model; the identity model uniform cannot compensate. **Fix:** transform normals with inverse transpose and transform/reorthogonalize the tangent basis. **Reporter:** Codex #22.

23. **MEDIUM — Rejected pane updates still mutate topology.**  
    **Location:** `libs/draxul-server/src/topology_service.cpp:1201`. Working-directory/source fields change before plugin identity validation fails, and the rejected command skips the revision increment. **Trigger:** submit an update with new paths and a mismatching plugin ID. **Fix:** finish validation before mutating authoritative state. **Reporter:** Codex #23.

24. **MEDIUM — Font configuration reload leaks native resources.**  
    **Location:** `libs/draxul-font/src/text_service.cpp:83`; replacement at `app/app.cpp:695`. Default move replacement destroys old internals without releasing raw FreeType/HarfBuzz ownership. **Trigger:** repeatedly reload changed font configuration. **Fix:** give resource-owning internals automatic, move-safe cleanup. **Reporter:** Codex #24.

25. **MEDIUM — Invalid cloud downloads replace the last good cache.**  
    **Location:** `plugins/satview/src/services/satview_cloud_service.cpp:287`. Downloaded bytes are published before image decoding; fallback rereads the invalid replacement. **Trigger:** a nonempty invalid image response replaces a good cache, whose fresh timestamp can then suppress retry after restart. **Fix:** validate before publication and retry invalid fresh caches. **Reporter:** Codex #25.

26. **MEDIUM — ScoreView’s `notick` mode enables beats.**  
    **Location:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp:269`. The later `"tick"` substring check overrides the earlier `"notick"` setting. **Trigger:** launch with `mode: "roll-notick"`. **Fix:** parse exact tokens or make metronome choices mutually exclusive. **Reporter:** Codex #26.

27. **MEDIUM — Hidden PCB routes intercept selection.**  
    **Location:** `plugins/pcbview/src/selection.cpp:90`. Route picking runs regardless of routed-track visibility and precedes airwire picking. **Trigger:** hide routed tracks and click where invisible copper crosses a visible airwire. **Fix:** pass route visibility into selection and gate copper picking accordingly. **Reporter:** Codex #27.

28. **MEDIUM — Disabling weather leaves its stale pill visible.**  
    **Location:** `libs/draxul-weather/src/weather_service.cpp:49`. Stopping the worker retains its published temperature and emoji, which chrome continues displaying. **Trigger:** after weather loads, clear `weather_location` and reload configuration. **Fix:** clear published state safely when disabling/changing location or explicitly gate presentation. **Reporter:** Codex #28.

**Rulings, unresolved leads, and dependencies**

There is no substantive inter-report disagreement: the Claude input supplies no position. Current source supports the Codex severities. Similar symptoms were kept separate where their causes differ—for example, host rejection of final saves, MegaCity’s missing persistence wiring, and SatView’s missing worker synchronization.

Two leads remain unresolved and receive **no cards**:

- **SatView Vulkan label-texture retirement:** `plugins/satview/src/render/satview_render_vk.cpp:853` destroys the old texture during replacement, but an independently reachable production upload path was not established while finding 15 blocks label initialization. Reassess retirement when restoring labels.
- **Mixed-DPI IME coordinates:** no complete coordinate-space trace establishes the suspected mismatch.

Fix dependencies:

- Coordinate findings **2 and 6**: allowing final saves increases reachable concurrent storage writes.
- Findings **6, 14, and 16** need independent persistence/restore coverage; fixing one does not fix the others. MegaCity must account for finding 6 if it saves during quiescence.
- Findings **16 and 17** share SatView’s wrapper/runtime control boundary and should share integration coverage.
- Finding **15** requires a Vulkan label-lifetime review before enabling the repaired upload path.
- Finding **25** shares publication code with existing `kanban/pending/49 satview-concurrent-cache-publication -bug.md`; preserve both validation and concurrency guarantees.
- Finding **1** requires coordinated client/protocol/server changes. Exception containment alone would still lose valid input.
- Findings **7 and 24** touch shared text infrastructure but address independent generation and resource-lifetime defects.

The following cards were created by the trusted runner during recovery.

### kanban/pending/00 terminal-input-binary-serialization -bug.md

# Preserve arbitrary terminal input across Session transport
**Severity:** CRITICAL  
**Source:** Codex #1; `libs/draxul-client/src/remote_session_coordinator.cpp:1659`.

Legacy mouse coordinates can contain `0x80`, and paste chunking can split UTF-8 characters. Strict JSON serialization throws outside the worker boundary and terminates the GUI.

**Investigation**

- [ ] Trace arbitrary input bytes and chunk boundaries through current client, protocol, and server paths.

**Fix strategy**

- [ ] Implement lossless binary-safe wire encoding and matching server decoding; retain current-format version enforcement.
- [ ] Contain worker exceptions and expose a recoverable failure without silently discarding accepted input.

**Acceptance criteria**

- [ ] Legacy mouse column 95, arbitrary bytes, and split multibyte pastes arrive intact without terminating the GUI.
- [ ] Run core aggregate tests and a same-cache smoke; record Windows and macOS transport validation.

### kanban/pending/01 plugin-storage-exclusive-temporary-files -bug.md

# Prevent cross-process plugin storage collisions
**Severity:** CRITICAL  
**Source:** Codex #2; `libs/draxul-host/src/plugin_storage.cpp:467`.

Separate clients saving shared plugin/pane state can select the same `.tmp-N` filename, truncate each other’s writes, or modify an already-published inode.

**Investigation**

- [ ] Exercise concurrent writers against the same storage key and inspect native creation/replacement behavior on both platforms.

**Fix strategy**

- [ ] Exclusively create unique temporary files per writer and publish only completed writes.
- [ ] Ensure failure cleanup removes only the calling writer’s temporary file.

**Acceptance criteria**

- [ ] Concurrent saves always leave one complete valid document, never mixed or truncated content.
- [ ] Cover replacement failures and abandoned temporary files; run core/plugin storage aggregate coverage and same-cache smoke.
- [ ] Coordinate with `kanban/pending/05 plugin-quiescence-final-storage-saves -bug.md`.

### kanban/pending/02 reject-nonfinite-config-values -bug.md

# Reject non-finite numeric configuration before resource initialization
**Severity:** CRITICAL  
**Source:** Codex #3; `libs/draxul-config/src/config_schema.cpp:553`.

`font_size = nan` survives range handling and reaches undefined integer conversion in font initialization, including through checked reload.

**Investigation**

- [ ] Trace float schema fields through startup, checked reload, and downstream integer conversions.

**Fix strategy**

- [ ] Reject or safely default non-finite values before range handling, with explicit checked-reload diagnostics.
- [ ] Validate numeric inputs defensively at the font initialization boundary.

**Acceptance criteria**

- [ ] NaN and infinities never reach native font-size conversions; failed reload retains the prior valid configuration.
- [ ] Verify finite boundary behavior, then run core aggregate tests and same-cache smoke.

### plugins/rezonality/kanban/pending/03 rezonality-checked-surface-dimensions -bug.md

# Validate procedural surface dimensions before GPU conversion
**Severity:** CRITICAL  
**Source:** Codex #4; `plugins/rezonality/src/native_backend_vulkan.cpp:448` and `native_backend_metal.mm:454`.

A procedural surface with `scale: (1e30, 1)` passes parsing and causes an out-of-range floating-to-integer conversion on both backends.

**Investigation**

- [ ] Trace procedural dimension calculation during activation and pane resize, including device limits.

**Fix strategy**

- [ ] Compute and validate dimensions before casting or allocating on Vulkan and Metal.
- [ ] Return actionable diagnostics while retaining the last valid generation.

**Acceptance criteria**

- [ ] Huge scales and overflow products fail safely; valid scales and resizing remain supported.
- [ ] Run the Rezonality aggregate, relevant render checks, and same-cache smoke; record both-platform evidence.
- [ ] Keep this scope distinct from `kanban/pending/42 rezonality-image-storage-format -bug.md`.

### plugins/pcbview/kanban/pending/04 pcbview-routing-margin-bounds -bug.md

# Bound routing margins before integer conversion
**Severity:** CRITICAL  
**Source:** Codex #5; `plugins/pcbview/src/autorouter.cpp:243`.

A finite but enormous clearance passes board validation and grid budgeting, then overflows margin-derived integer conversions and potentially center/radius arithmetic.

**Investigation**

- [ ] Audit every clearance-, pad-, and via-derived conversion at loader and programmatic routing boundaries.

**Fix strategy**

- [ ] Validate derived margins or clip floating bounds to grid bounds before conversion.
- [ ] Make subsequent integer range arithmetic safe.

**Acceptance criteria**

- [ ] A board with a mount and `clearance_mm = 1e100` fails safely without undefined conversion.
- [ ] Ordinary routing remains correct; run PCBView aggregate coverage and same-cache smoke.
- [ ] Preserve the separate grid-budget guarantees indexed by `kanban/pending/31 pcbview-routing-cell-budget -bug.md`.

### kanban/pending/05 plugin-quiescence-final-storage-saves -bug.md

# Keep valid storage callbacks available during plugin quiescence
**Severity:** HIGH  
**Source:** Codex #6; `libs/draxul-host/src/plugin_host.cpp:224`.

Shutdown and reload mark the host shutting down before quiescence, causing final storage saves—such as SatView preferences—to fail.

**Investigation**

- [ ] Trace callback validity, generation checks, and final saves through close, shutdown, and reload.

**Fix strategy**

- [ ] Permit legitimate main-thread storage calls during quiescence.
- [ ] Retire callbacks afterward while continuing to reject late asynchronous and stale-generation calls.

**Acceptance criteria**

- [ ] SatView panel preferences survive close/reopen and reload.
- [ ] Callback retirement remains safe; run shared plugin aggregate coverage and same-cache smoke.
- [ ] Coordinate with `01 plugin-storage-exclusive-temporary-files -bug.md`.

### kanban/pending/06 shared-atlas-reset-invalidation -bug.md

# Invalidate every glyph-atlas consumer after overflow
**Severity:** HIGH  
**Source:** Codex #7; `libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp:303`.

An overflowing pane consumes the shared atlas reset and rebuilds only itself, leaving other panes with obsolete glyph coordinates.

**Investigation**

- [ ] Identify all shared atlas consumers and their update ordering, including chrome and hidden grids.

**Fix strategy**

- [ ] Introduce generation-aware invalidation so every dependent consumer rebuilds before using replaced atlas contents.
- [ ] Handle resets occurring after another consumer has already been processed.

**Acceptance criteria**

- [ ] Overflow triggered by one pane preserves correct text in clean and subsequently revealed panes without manual test invalidation.
- [ ] Run core aggregate tests, relevant multi-pane render checks, and same-cache smoke on supported backends.

### kanban/pending/07 nvim-printable-key-duplication -bug.md

# Deliver Space and Backslash to Neovim once
**Severity:** HIGH  
**Source:** Codex #8; `libs/draxul-nvim/src/input.cpp:122`.

Ordinary Space and Backslash produce both named key input and unsuppressed text input, duplicating characters in insert mode.

**Investigation**

- [ ] Trace ordinary and modified key/text event pairs through dispatcher and Neovim input handling.

**Fix strategy**

- [ ] Give printable input one delivery path while preserving modified-key notation and text composition behavior.

**Acceptance criteria**

- [ ] A keydown/text pair inserts one Space or Backslash; modified combinations retain their intended behavior.
- [ ] Run core aggregate tests and same-cache smoke; verify actual typing on Windows and macOS.

### kanban/pending/08 windows-nvim-unicode-launch -bug.md

# Launch Windows Neovim using Unicode process APIs
**Severity:** HIGH  
**Source:** Codex #9; `libs/draxul-nvim/src/nvim_process.cpp:169`.

UTF-8 executable, argument, and working-directory strings are interpreted through the Windows ANSI code page.

**Investigation**

- [ ] Trace launch string encoding, argument quoting, and environment construction.

**Fix strategy**

- [ ] Convert launch values to UTF-16 and use `CreateProcessW` with wide startup structures and a compatible environment block.

**Acceptance criteria**

- [ ] Neovim launches with non-ASCII executable paths, working directories, and arguments on a non-UTF-8 Windows code page.
- [ ] Preserve quoting and environment behavior; run Windows core aggregate tests and same-cache smoke, with macOS regression coverage.

### kanban/pending/09 plugin-directory-utf8-abi -bug.md

# Publish plugin resource directories as explicit UTF-8
**Severity:** HIGH  
**Source:** Codex #10; `libs/draxul-host/src/plugin_host.cpp:117`.

The host publishes `.string()` bytes through `plugin_directory_utf8`; plugins decode them as UTF-8, breaking non-ASCII Windows staging paths.

**Investigation**

- [ ] Trace the ABI directory field and representative plugin resource consumers.

**Fix strategy**

- [ ] Populate the field using explicit UTF-8 conversion with lifetime extending through instance creation.

**Acceptance criteria**

- [ ] Plugins load assets from non-ASCII Windows directories under non-UTF-8 ANSI settings.
- [ ] Run shared plugin aggregate coverage and same-cache smoke; verify macOS path behavior remains correct.

### kanban/done/10 persist-disabled-keybindings -bug.md

# Preserve explicitly disabled default keybindings
**Severity:** HIGH  
**Source:** Codex #11; `libs/draxul-config/src/app_config_io.cpp:376`.

Removed bindings are omitted during serialization, so shutdown saving erases explicit disables and defaults return at restart.

**Investigation**

- [ ] Trace empty binding parsing, serialization, document merging, and shutdown persistence.

**Fix strategy**

- [ ] Serialize explicit empty entries for disabled default actions while preserving configured chords and unrelated settings.

**Acceptance criteria**

- [ ] `copy = ""` remains disabled after serialization, document merge, and restart.
- [ ] Verify enabled bindings still round-trip; run core aggregate tests and same-cache smoke.

### kanban/done/11 integration-installer-toml-sections -bug.md

# Preserve valid TOML while enabling integration hooks
**Severity:** HIGH  
**Source:** Codex #12; `libs/draxul-agent-integration/src/agent_integration.cpp:322`.

A valid `[features] # comment` header is missed, causing installation to append a duplicate table and invalidate the configuration.

**Investigation**

- [ ] Examine section recognition with comments, whitespace, quoted keys, and neighboring tables.

**Fix strategy**

- [ ] Transform TOML structure correctly while preserving unrelated configuration.
- [ ] Validate transformed content before atomic publication.

**Acceptance criteria**

- [ ] Installation handles commented headers without duplicate tables or misplaced assignments and remains idempotent.
- [ ] Invalid transformations never replace the original file; run core aggregate tests and same-cache smoke.

### kanban/done/12 kanban-focus-loss-repeat-state -bug.md

# Stop Kanban navigation when the host loses focus
**Severity:** HIGH  
**Source:** Codex #13; `modules/kanban/draxul-kanban/src/kanban_host.cpp:300`.

Holding navigation while switching tabs leaves repeat state active because the release reaches another host; background pumping continues selection changes.

**Investigation**

- [ ] Trace held-key/navigation state, focus transitions, background pumping, and pinned preview callbacks.

**Fix strategy**

- [ ] Clear repeat and navigation state on focus loss and guard unfocused repeat execution.

**Acceptance criteria**

- [ ] Hold, switch tabs, and release produces no subsequent background selection or preview changes.
- [ ] Refocusing requires fresh input and normal repeat still works; run core aggregate tests and same-cache smoke.

### plugins/megacity/kanban/pending/13 megacity-plugin-preference-storage -bug.md

# Connect MegaCity preferences to durable plugin storage
**Severity:** HIGH  
**Source:** Codex #14; `plugins/megacity/src/megacity_plugin.cpp:143`.

The wrapper supplies no configuration document, so the host always loads defaults and preference-save calls return without writing.

**Investigation**

- [ ] Inventory renderer and camera preference load/save paths for MegaCity and BioView.

**Fix strategy**

- [ ] Wire product-owned durable configuration through wrapper initialization and preference updates.
- [ ] Account for quiescence callback availability if final saves occur during close.

**Acceptance criteria**

- [ ] Renderer and camera preferences survive pane close/reopen and reload in both modes.
- [ ] Run MegaCity aggregate tests, relevant pane checks, and same-cache smoke.
- [ ] Coordinate any final-save dependency with `kanban/pending/05 plugin-quiescence-final-storage-saves -bug.md`.

### plugins/satview/kanban/pending/14 satview-plugin-sky-labels -bug.md

# Initialize SatView label text from plugin UI style
**Severity:** HIGH  
**Source:** Codex #15; `plugins/satview/src/runtime/satview_runtime.cpp:2286`.

Production wrappers never provide the host text-service pointer required by label initialization, so sky-label atlases are not created.

**Investigation**

- [ ] Trace UI-style font information through label service creation and both backend upload paths.
- [ ] Reassess Vulkan label-texture retirement before making uploads reachable.

**Fix strategy**

- [ ] Initialize the product-owned text service from available font/style metrics and refresh it safely on style changes.

**Acceptance criteria**

- [ ] Cardinal and constellation labels render in actual plugin instances.
- [ ] Run SatView aggregate tests, label render checks, and same-cache smoke; verify safe Vulkan and Metal resource lifetimes.

### plugins/satview/kanban/pending/15 satview-restored-worker-settings -bug.md

# Synchronize restored SatView settings with its worker
**Severity:** HIGH  
**Source:** Codex #16; `plugins/satview/src/satview_plugin.cpp:200`.

Saved preferences are applied after simulation startup without publishing restored time speed or track budgets to the worker.

**Investigation**

- [ ] Compare restored runtime fields against worker startup controls and subsequent synchronization paths.

**Fix strategy**

- [ ] Restore before worker startup or explicitly synchronize every affected setting after applying configuration.

**Acceptance criteria**

- [ ] Nondefault restored speed and track budgets affect worker output before any corrective user action.
- [ ] Cover restore alongside pause-state transitions; run SatView aggregate tests and same-cache smoke.
- [ ] Coordinate with `plugins/satview/kanban/pending/16 satview-authoritative-pause-state -bug.md`.

### plugins/satview/kanban/pending/16 satview-authoritative-pause-state -bug.md

# Unify SatView pause state and frame scheduling
**Severity:** HIGH  
**Source:** Codex #17; `plugins/satview/src/satview_plugin.cpp:361`.

Wrapper and runtime pause flags diverge: Space pause followed by panel Resume restarts simulation while continuous presentation remains stopped.

**Investigation**

- [ ] Trace Space, captured keyboard input, panel controls, persistence, and tick/render deadlines.

**Fix strategy**

- [ ] Establish one authoritative pause state and synchronize worker controls, scheduling, and stored state through it.

**Acceptance criteria**

- [ ] Space and panel Pause/Resume combinations keep simulation, presentation, and stored state consistent.
- [ ] Captured Space causes no unintended transition; run SatView aggregate tests and same-cache smoke.
- [ ] Share restore coverage with `plugins/satview/kanban/pending/15 satview-restored-worker-settings -bug.md`.

### plugins/satview/kanban/pending/17 satview-metal-cloud-texture-refresh -bug.md

# Refresh Metal cloud textures without mutating in-flight data
**Severity:** HIGH  
**Source:** Codex #18; `plugins/satview/src/render/satview_render.mm:245`.

Same-sized cloud updates overwrite a texture that an earlier asynchronous frame may still sample.

**Investigation**

- [ ] Trace cloud revision publication, texture ownership, and outstanding frame usage.

**Fix strategy**

- [ ] Upload into a replacement texture with safe retirement, or synchronize every outstanding reader before mutation.

**Acceptance criteria**

- [ ] Repeated same-sized refreshes remain correct with multiple frames in flight.
- [ ] Run SatView aggregate tests, Metal refresh/render checks, and same-cache smoke; inspect Vulkan parity.
- [ ] Keep this scope separate from `kanban/pending/36 satview-vulkan-stream-buffer-lifetime -bug.md`.

### plugins/scoreview/kanban/pending/18 scoreview-midi-arrival-time-judgment -bug.md

# Judge ScoreView MIDI events by their arrival time
**Severity:** HIGH  
**Source:** Codex #19; `plugins/scoreview/product/draxul-scoreview/src/flow_controller.cpp:323`.

Roll judgment uses processing position and expires windows before queued input is read, turning valid delayed-delivery events into misses.

**Investigation**

- [ ] Trace MIDI timestamps through input polling, transport advancement, judgment, and expiration.

**Fix strategy**

- [ ] Map event times onto the transport timeline and judge queued events before expiring their corresponding windows.
- [ ] Preserve correct mapping through tempo changes, pauses, and delayed pumps.

**Acceptance criteria**

- [ ] At 60 QPM, the onset-1 event arriving at 1.44 remains valid when processed at 1.46.
- [ ] Truly late events remain misses; run ScoreView aggregate tests and same-cache smoke.

### plugins/rezonality/kanban/pending/19 rezonality-pause-clock-anchor -bug.md

# Exclude paused intervals from Rezonality animation time
**Severity:** HIGH  
**Source:** Codex #20; `plugins/rezonality/src/rezonality_plugin.cpp:191`.

Resume leaves the previous render timestamp as the animation anchor, so an idle paused interval is added on the next frame.

**Investigation**

- [ ] Trace animation time through pause, sparse redraws, resume, and state restoration.

**Fix strategy**

- [ ] Reset or reanchor the animation clock at pause/resume transitions.

**Acceptance criteria**

- [ ] Long pauses cause no animation jump, with or without incidental paused redraws.
- [ ] Verify both backends and reload behavior; run Rezonality aggregate tests, relevant render checks, and same-cache smoke.

### plugins/rezonality/kanban/pending/20 rezonality-shader-include-watching -bug.md

# Watch all shader include dependencies
**Severity:** HIGH  
**Source:** Codex #21; `plugins/rezonality/src/live_project.cpp:1189`.

The watcher ignores extensions such as `.inc` even though shader compilation accepts them, leaving valid include-only edits unapplied.

**Investigation**

- [ ] Trace dependency discovery, fingerprinting, and rebuild scheduling for direct and nested includes.

**Fix strategy**

- [ ] Watch the actual dependency closure or conservatively cover include files without relying on the current suffix whitelist.
- [ ] Preserve last-valid-generation behavior when an include edit breaks compilation.

**Acceptance criteria**

- [ ] Editing, breaking, and repairing an included `.inc` schedules corresponding generations and diagnostics automatically.
- [ ] Run Rezonality aggregate and real-module edit/recovery coverage, followed by same-cache smoke.

### plugins/rezonality/kanban/pending/21 rezonality-scaled-model-tangent-basis -bug.md

# Transform model shading vectors with baked scale
**Severity:** HIGH  
**Source:** Codex #22; `plugins/rezonality/src/model_loader.cpp:211`.

Nonuniform scale changes positions but leaves normals and tangent-space vectors unchanged, producing incorrect lighting and normal mapping.

**Investigation**

- [ ] Trace imported vertex bases through baked scale and shared renderer uniforms.

**Fix strategy**

- [ ] Apply inverse-transpose normal transformation and transform/reorthogonalize tangent vectors.
- [ ] Define safe behavior for singular and mirrored scales.

**Acceptance criteria**

- [ ] A sloped normal-mapped model scaled by `(1,2,3)` has the expected shading on Vulkan and Metal.
- [ ] Run Rezonality aggregate tests, relevant render checks, and same-cache smoke.

### kanban/pending/22 rejected-pane-update-atomicity -bug.md

# Keep rejected pane updates free of mutations
**Severity:** MEDIUM  
**Source:** Codex #23; `libs/draxul-server/src/topology_service.cpp:1201`.

A mismatching plugin ID is rejected after changing source and working-directory fields, leaving altered topology under the old revision.

**Investigation**

- [ ] Trace validation and mutation ordering for `UpdateClientPane`, including command result and revision handling.

**Fix strategy**

- [ ] Complete immutable-identity validation before committing any field changes.

**Acceptance criteria**

- [ ] A rejected update leaves the complete snapshot and revision unchanged.
- [ ] A valid update publishes all intended fields with the expected revision advance; run core aggregate tests and same-cache smoke.

### kanban/pending/23 font-reload-native-resource-cleanup -bug.md

# Release native font resources during text-service replacement
**Severity:** MEDIUM  
**Source:** Codex #24; `libs/draxul-font/src/text_service.cpp:83` and `app/app.cpp:695`.

Reload move-assignment destroys old text-service internals without releasing their raw FreeType/HarfBuzz resources.

**Investigation**

- [ ] Audit resource ownership through initialization failure, shutdown, destruction, and move replacement.

**Fix strategy**

- [ ] Add automatic cleanup at the owning layer and preserve safe moved-from and repeated-shutdown behavior.

**Acceptance criteria**

- [ ] Repeated successful and failed reloads do not accumulate native resources or double-release them.
- [ ] Live hosts remain valid after replacement; run core aggregate tests and same-cache smoke.

### plugins/satview/kanban/pending/24 satview-validate-cloud-cache-before-publish -bug.md

# Preserve the last valid cloud cache after invalid downloads
**Severity:** MEDIUM  
**Source:** Codex #25; `plugins/satview/src/services/satview_cloud_service.cpp:287`.

Nonempty downloaded bytes replace the cache before decoding, destroying fallback data and potentially suppressing retries after restart.

**Investigation**

- [ ] Trace download validation, cache publication, fallback, and freshness decisions.

**Fix strategy**

- [ ] Decode and validate before atomic publication; retain the prior cache on failure.
- [ ] Retry invalid fresh caches instead of treating freshness alone as sufficient.

**Acceptance criteria**

- [ ] Invalid downloads preserve a usable prior cache, including across restart.
- [ ] Run SatView aggregate tests and same-cache smoke.
- [ ] Coordinate publication changes with existing `kanban/pending/49 satview-concurrent-cache-publication -bug.md`.

### plugins/scoreview/kanban/pending/25 scoreview-notick-mode-parsing -bug.md

# Honor ScoreView’s `notick` launch mode
**Severity:** MEDIUM  
**Source:** Codex #26; `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp:269`.

`roll-notick` selects Off and then matches the later `"tick"` substring check, enabling beats.

**Investigation**

- [ ] Trace plugin mode strings and interactions among `notick`, `tick`, `tick8`, and `locktempo`.

**Fix strategy**

- [ ] Parse exact tokens or make tick choices mutually exclusive, keeping unrelated options independent.

**Acceptance criteria**

- [ ] `roll-notick` disables metronome ticks; `tick` and `tick8` retain their intended levels.
- [ ] Verify combined mode options; run ScoreView aggregate tests and same-cache smoke.

### plugins/pcbview/kanban/pending/26 pcbview-hidden-route-selection -bug.md

# Exclude hidden routed tracks from PCB picking
**Severity:** MEDIUM  
**Source:** Codex #27; `plugins/pcbview/src/selection.cpp:90`.

Routed-track drawing respects visibility, but picking does not; invisible copper can intercept a visible airwire and activate selection effects.

**Investigation**

- [ ] Trace display flags through runtime selection input and picking precedence.

**Fix strategy**

- [ ] Add routed-track visibility to selection inputs and gate route picking accordingly.

**Acceptance criteria**

- [ ] Hidden routes cannot intercept selection; visible airwires and pads remain selectable.
- [ ] Visible-route precedence and layer filtering remain correct; run PCBView aggregate tests and same-cache smoke.
- [ ] Keep the fix distinct from `kanban/pending/88 pcbview-selection-core -refactor.md`.

### kanban/pending/27 weather-disable-clears-presentation -bug.md

# Clear stale weather presentation when disabled
**Severity:** MEDIUM  
**Source:** Codex #28; `libs/draxul-weather/src/weather_service.cpp:49`.

Clearing `weather_location` stops the worker but retains its published emoji and temperature, leaving a permanently stale chrome pill.

**Investigation**

- [ ] Trace weather state through location changes, cancellation, worker completion, and chrome presentation.

**Fix strategy**

- [ ] Clear published state safely when disabling/changing location, or gate presentation explicitly.
- [ ] Prevent a completing old worker from republishing stale location data.

**Acceptance criteria**

- [ ] Clearing the location and reloading removes the pill; switching locations does not retain misleading old data.
- [ ] Re-enabling weather publishes fresh values; run core aggregate tests and same-cache smoke.

---

Tracker placement after publication: 14 product-owned cards were moved to their product submodules; 14 core/shared cards remain in the root tracker.

Authorship: `gpt-6-astra`.
