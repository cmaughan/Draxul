# WI 30 — Host telemetry in the diagnostics overlay (F12)

**Type:** feature  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 7

---

## Goal

Extend the F12 diagnostics overlay with per-host runtime statistics: RPC request latency, grid dirty-cell rates, and memory usage. Add a toast warning when RPC latency spikes above a threshold.

---

## Current state

The F12 overlay already shows: frame timing, dirty cell count, glyph atlas stats, startup phases. Missing: anything per-host, RPC health, memory trends.

---

## Implementation Plan

**Phase A — data collection:**
- [ ] In `NvimRpc`, track:
  - Rolling average RPC request latency (last 100 requests, `std::deque`-based).
  - Peak latency in the last 60 seconds.
  - Pending request count.
- [ ] In `GridHostBase` (or `NvimHost`), track:
  - Dirty cell count per frame (already available, expose it).
  - Approximate grid memory usage (cols × rows × sizeof(Cell)).
- [ ] Expose these via a `HostTelemetry` struct returned by a `IHost::telemetry()` virtual method (or optional interface).

**Phase B — overlay display:**
- [ ] In `DiagnosticsHost` (or wherever the F12 overlay content is built), add a "Hosts" section showing per-host telemetry.
- [ ] Format as a compact table: host name | RPC avg latency | dirty cells/frame | grid memory.

**Phase C — latency alert:**
- [ ] If rolling average RPC latency exceeds a configurable threshold (default 500ms), push a toast: `"Neovim RPC latency elevated: NNms avg over last 10 requests"`.
- [ ] Rate-limit the toast: at most once per 30 seconds to avoid spamming.
- [ ] Make the threshold configurable as `rpc_latency_warn_ms = 500` in `config.toml`.

---

## Notes for the agent

- Keep `HostTelemetry` as a simple value struct (no virtuals, no locks) — collect it on the main thread during the frame loop, never from the RPC reader thread.
- The latency threshold toast can share the `rpc_timeout_s` infrastructure from WI 28.

---

## Interdependencies

- WI 28 (rpc timeout user feedback) introduces the toast mechanism for RPC failures; this builds on top.
- Phase 1 bugs (WI 04-07) should be fixed first — telemetry should not mask underlying crashes.
- WI 24 (unified result type) is not strictly required but helps if telemetry collection returns errors.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
