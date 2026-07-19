# GEMINI.md - Draxul Project Context

**[CLAUDE.md](CLAUDE.md) is the canonical agent guide for this repository.** Read it first.

It is the single source of truth for the shared rules every agent needs — build and run
commands (per platform), the architecture and dependency graph, threading invariants, the
`kanban/` work-item tracker, validation expectations, and known pitfalls. This file used to
restate all of that for Gemini-based tools, which meant the same rules lived in two places
and drifted apart. To stop that divergence, the shared content now lives only in CLAUDE.md.

For the feature inventory (hosts, config keys, CLI flags, build options, CI), see
**[docs/features.md](docs/features.md)**, with per-host detail under `docs/features/`.

## Gemini-specific notes

There are currently no Gemini-only rules. If a genuinely Gemini-specific instruction ever
becomes necessary, add it here as a clearly-scoped note — never by copying a shared rule out
of CLAUDE.md, so the two cannot diverge.
