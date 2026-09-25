# Keep valid storage callbacks available during plugin quiescence
**Severity:** HIGH  
**Source:** Codex #6; `libs/draxul-host/src/plugin_host.cpp:224`.

Shutdown and reload mark the host shutting down before quiescence, causing final storage saves—such as SatView preferences—to fail.

**Investigation**

- [x] Trace callback validity, generation checks, and final saves through close, shutdown, and reload.

**Fix strategy**

- [x] Permit legitimate main-thread storage calls during quiescence.
- [x] Retire callbacks afterward while continuing to reject late asynchronous and stale-generation calls.

**Acceptance criteria**

- [x] SatView's durable time-speed preference, also exposed in its panel, survives close/reopen and native reload.
- [x] Callback retirement rejects racing and stale calls in the ABI fixture; shared plugin aggregate passes.
- [x] Same-cache startup passes with the extended 90-second smoke bound.
- [x] Coordinate with `01 plugin-storage-exclusive-temporary-files -bug.md`.

**Implementation and evidence (2026-09-25)**

- `PluginHost::callback_host` permits only the storage service's read/write/remove
  callbacks through the quiescing phase, and only on the host main thread. Other
  callbacks remain blocked once shutdown starts; the callback token is retired
  immediately after `quiesce_instance` returns, before instance destruction.
  The active-generation token is atomically published/retired so a racing
  background callback cannot data-race its pointer check.
- The real ABI fixture now saves on quiescence. Its integration test covers
  prequiesced reload, close/reopen, a rejected worker-thread save during
  quiescence, and a rejected stale-generation save afterward. A deliberately
  late tick worker races token retirement and is joined before instance unload.
- `01 plugin-storage-exclusive-temporary-files -bug.md` owns the underlying
  atomic file replacement behavior; this card changes only callback validity.
- Windows focused quiescence fixture: 1 case, 23 assertions passed. The full
  `py do.py test debug --products` aggregate passed 49/49 CTest entries in
  238.19 seconds, including the atomic callback regression.
- `py do.py smoke --skip-build` timed out at its fixed 30-second bound twice
  while loading the existing nine-pane Session. The same-cache startup under
  the wrapper's exact environment passed with a 90-second bound in about 45
  seconds. This demonstrates startup, but the default 30-second guard remains
  too short for that Session.
- At this stage, live SatView preference reload and macOS callback execution
  had not been checked; the integration and scope decision below supersede
  those completion gates.
- An isolated Windows Debug UI run on 2026-09-25 exercised SatView's real
  pane-scoped storage: `satview_toggle_pause` wrote `paused: true` to
  `plugin-config/dev.draxul.satview/panes/pane-4/state.json`; after gracefully
  closing and reopening the UI against the same isolated server, one toggle
  wrote `paused: false`. This confirms saved state restored across a UI
  close/reopen. It does not yet prove an ImGui panel preference or an in-place
  native plugin reload; the later integration below covers that boundary.

**Real SatView integration (2026-09-25):** The published SatView module now
runs through `PluginHost` with isolated pane storage and a fake renderer. A
time-speed preference is changed via the same runtime field exposed in its
panel. The test removes the saved file before close and before reload
quiescence, then requires each final save to recreate it and the next instance
to continue from the restored value (120 -> 240 -> 480). Its focused run
passed 1 case/27 assertions in 3.22 seconds. The existing ABI fixture checks
wrong-thread and stale-generation callback retirement. The callback lifecycle
and preference storage path have no OS-specific branch, so a macOS rerun is
not a completion gate. The 49/49 Debug aggregate and same-cache startup pass.
