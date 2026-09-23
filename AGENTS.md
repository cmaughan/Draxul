# Draxul Agent Guide

**[CLAUDE.md](CLAUDE.md) is the canonical agent guide for this repository.** Read it
before making changes.

Shared build commands, architecture, cross-platform expectations, tracker paths,
validation rules, and known pitfalls live there so they cannot drift between agent
families. The detailed current library and product-module graph is in
[docs/module-map.md](docs/module-map.md); CMake remains authoritative.

## Kanban tracking

- Automatically create a card in `kanban/pending/` for every non-trivial new
  task, feature, refactor, or bug fix. Treat work as non-trivial when it spans
  multiple implementation steps, needs validation or follow-up gates, or carries
  enough context that another agent may need to continue it. Create and maintain
  the card as part of the work; do not wait for the user to request one.
- Keep small, self-contained edits that need no meaningful follow-up out of the
  tracker.

- Do not place or leave a card in any `kanban/done/` lane until every checkbox
  in that card is checked off, including manual and platform-specific gates.
- If an item cannot be completed now, keep the card out of `done`. When scope is
  intentionally deferred or transferred, update the card before moving it and
  link the separate tracking card that owns the remaining work; do not leave an
  unchecked box in `done`.

## Codex-specific notes

- Do not use MaaS MCP tools or servers for work in this repository.
- During Draxul client/server development and validation, Codex may terminate
  running Draxul instances when necessary. Resolve the exact Draxul process
  targets first and avoid affecting unrelated processes.
- For a completed slice, run one scope-appropriate aggregate test command and
  one same-cache smoke as defined in `CLAUDE.md`. Use focused filters only while
  iterating or diagnosing; do not run them immediately before an aggregate pass
  that repeats the same cases unless they provide a material risk or time benefit.
- When working under `plugins/megacity/`, also read
  `plugins/megacity/product/AGENTS.md`.
