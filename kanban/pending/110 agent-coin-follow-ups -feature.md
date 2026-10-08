# Agent activity coin follow-ups

**Summary:** Finish the activity-coin follow-ups deferred from the first two slices: persistent hook references, sub-agent token counting, machine-wide discovery, and narrow-rail label space.

**Priority:** P3 — refinements to a working, validated feature.

Transferred from `kanban/done/104 agent-activity-coins -feature.md` when its macOS and Windows gates completed on 2026-10-08.

- [ ] Persist hook-reported session refs for discovered agents so attribution survives a server restart instead of falling back to directory matching until the next session start.
- [ ] Count Claude sub-agent transcripts (`<session>/subagents/*.jsonl`) toward the owning agent.
- [ ] Reuse `AgentUsageMonitor` with the same requests for machine-wide agent discovery.
- [ ] Recover label space in narrow rails: the coin takes about two columns, so names truncate sooner (seen at an 8-column sidebar on Windows: `1: CL…`).
