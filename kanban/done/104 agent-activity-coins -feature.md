# Agent activity coins in the Agents rail

**Summary:** Show TokenFu's spinning provider coin beside each discovered agent
pill and spin it according to that agent's activity.

**Priority:** P2

**Design:** [plans/agent-cards-token-activity.md](../../plans/agent-cards-token-activity.md)
(this card pulls the coin forward ahead of the full card redesign).
**Later:** machine-wide agent discovery will feed the same rail and coins.

## Slice 1 — coin beside pills, status-driven spin

- [x] `libs/draxul-activity-coin/`: `IActivityCoinPass` with Metal and Vulkan
      backends; procedural TokenFu mesh, provider marks (Codex/Claude/Grok/neutral),
      per-facet vertex-shader culling so no depth attachment or buffers are needed.
- [x] Renderer-free spin model (`activity_coin_motion.h` in app-shell), ported from
      TokenFu `update_coin()`: eased speed, face-on settle, exact rest.
- [x] Chrome layout reserves whole leading columns for the coin and shifts the
      pill/text; row hit region still focuses the agent.
- [x] ChromeHost maps agent kind → style and lifecycle/status → load (working spins,
      starting turns slowly, exited dims), keys motion by instance id, and schedules
      ~30 fps frames only while a coin moves.
- [x] Layout and motion tests; `docs/features.md` and `docs/module-map.md`.
- [x] macOS: verified live against an isolated server with stand-in Codex/Claude agents.
- [x] Windows/Vulkan: confirm coins render on first Windows run. The GLSL compiles
      with glslc, but the Vulkan path has not been exercised.

## Slice 2 — spin from measured per-agent token rate

- [x] Drop the `[working]`/`[idle]` pill suffixes; the coin conveys them.
- [x] `libs/draxul-agent-usage/`: incremental Codex/Claude session tails (TokenFu
      parsing: Codex `token_count` with duplicate-total suppression, Claude
      per-message-id usage growth), TokenFu rolling ten-interval rate window.
- [x] `AgentUsageMonitor`: resolves a session from `AgentSessionRef` (exact) or,
      without one, from the agent process working directory when it is the only
      agent of that kind there; ambiguous agents stay unattributed. Native
      `FileMonitor` notifications consumed on the 1 Hz agent refresh, 30 s
      reconcile, 4 MB per-update catch-up budget, watchers dropped when no agents.
- [x] Process probes record the agent working directory (macOS `proc_pidinfo`,
      Linux `/proc/<pid>/cwd`, Windows PEB current directory); server only.
- [x] Optional `activity` (rate, measured-at, session tokens) on the server agent
      projection and in `draxul agent list/get --json`; clients decay it over 60 s.
- [x] Coin load from the decayed rate on TokenFu's 50k tokens/s scale; status
      fallback when unattributed. Coin frames paced to ~30 fps.
- [x] Tests: multi-agent attribution, ambiguity, session-id binding, incremental
      append, Codex directory lookup, server projection/wire round-trip and decay.
- [x] macOS: verified live against a real Claude session (~19-48k tokens/s).
- [x] Windows: confirm Codex/Claude attribution and the coin on first Windows run
      (PEB working directory and Claude project-directory naming are untested there).

## Follow-ups

- [x] `do integrate` installs every agent integration (see
      kanban/done/69 agent-hook-executable-path -bug.md for the hook fix).
- [x] Session hooks also report for agents started by typing `claude`/`codex` in a
      shell pane: optional `--agent-instance`, server resolves the discovered agent
      by pane and kind, early reports held 30 s, stale/old-generation reports
      rejected; referenced files are excluded from directory matching.
Deferred follow-ups (persistent hook refs, sub-agent transcripts, machine-wide
discovery, narrow-rail label space) were transferred unchanged to
`kanban/pending/110 agent-coin-follow-ups -feature.md` when this card completed.

## Windows validation — 2026-10-08

Isolated Debug client/server (`scripts/windows_core_gate_probe.py`), with
`CLAUDE_CONFIG_DIR`/`CODEX_HOME` pointing at a fresh temporary home. A layout
created two server terminal panes with separate working directories, and each
ran a stand-in agent (`PING.EXE` copied to `claude.exe` / `codex.exe`). Claude
assistant lines (35k tokens per message) and Codex `token_count` events
(19k per tick) were appended once a second for 20 seconds.

- Discovery and attribution: `agent list --json` reported Claude `snug-clam`
  and Codex `smart-goat`, each attributed from its own process working
  directory through the PEB probe. Claude resolved its session under
  `projects/<C--…-work-claude>/` (Windows project-directory naming) and Codex
  through `session_meta.cwd`. Measured rates were 34.9k and 19.0k tokens/s at
  both snapshots (expected 35k and 19k), with session totals growing
  (315k→700k and 171k→380k).
- Vulkan coins: the capture shows Claude's orange mark and Codex's blue mark
  beside the two Agents-rail pills. The client log contains no Vulkan
  validation errors; the only `[error]` line is the unrelated stale `W:\p4`
  loader manifest. The client exited 0 and the isolated server shut down
  cleanly. Evidence: `%TEMP%/dgaw0poewh/` (`coins.bmp`, `gate104-agents.json`,
  logs).
