# Draxul Agent Guide

**[CLAUDE.md](CLAUDE.md) is the canonical agent guide for this repository.** Read it
before making changes.

Shared build commands, architecture, cross-platform expectations, tracker paths,
validation rules, and known pitfalls live there so they cannot drift between agent
families. The detailed current library and product-module graph is in
[docs/module-map.md](docs/module-map.md); CMake remains authoritative.

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
