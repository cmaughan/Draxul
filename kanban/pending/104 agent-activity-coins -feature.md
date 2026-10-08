# Agent activity coins in the Agents rail

**Summary:** Show TokenFu's spinning provider coin beside each discovered agent
pill and spin it according to that agent's activity.

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
- [ ] Windows/Vulkan: confirm coins render on first Windows run. The GLSL compiles
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
- [ ] Windows: confirm Codex/Claude attribution and the coin on first Windows run
      (PEB working directory and Claude project-directory naming are untested there).

## Follow-ups

- [x] `do integrate` installs every agent integration (see
      kanban/pending/69 agent-hook-executable-path -bug.md for the hook fix).
- [x] Session hooks also report for agents started by typing `claude`/`codex` in a
      shell pane: optional `--agent-instance`, server resolves the discovered agent
      by pane and kind, early reports held 30 s, stale/old-generation reports
      rejected; referenced files are excluded from directory matching.
- [ ] Hook-reported refs for discovered agents are runtime-only; after a server
      restart they fall back to directory attribution until the next session start.

- [ ] Claude sub-agent transcripts (`<session>/subagents/*.jsonl`) are not yet
      counted toward the owning agent.
- [ ] Machine-wide discovery can reuse `AgentUsageMonitor` with the same requests.
- [ ] The coin takes ~2 columns from the label; narrow rails truncate names sooner.
