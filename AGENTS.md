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
- When working under `modules/megacity/`, also read
  `modules/megacity/AGENTS.md`.
