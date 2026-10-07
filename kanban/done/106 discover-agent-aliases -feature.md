# Discover and target named agents

Expose user-assigned agent pill labels to CLI/skill users, preserving stable IDs.

- [x] Publish an explicit alias in server and UI agent list/get output, including personal names.
- [x] Support --alias NAME for targeted agent CLI commands; reject ambiguous or missing aliases and pin waits to the resolved ID.
- [x] Update CLI help and Draxul skill; cover server rename propagation and CLI targeting.
- [x] Pass core aggregate tests and same-cache Release smoke.

## Decisions

Aliases are Session-scoped exact, case-sensitive names; quoted spaces are supported.
Instance IDs remain the unambiguous automation handle. Resolve aliases before sending
mutations, and fail without acting when multiple agents share the name.

## Implementation

- Server refresh derives regular aliases from persisted topology pane names and
  personal aliases from current collection metadata. Snapshots carry the alias
  explicitly; UI projections and list/get output expose it alongside instance_id.
- CLI `agent <verb> --alias "Name"` resolves a unique exact match before dispatch,
  including prompt/send/keys/restart/wait and UI-local focus. Duplicate/missing
  aliases do not send input. Waits retain the resolved ID and runtime generation.
- Human `agent list` prints alias=; JSON list/get carries alias and display_name.
  Stable-ID calls retain their existing one-request behavior. No compatibility
  shims or provider-name guessing are used.
- Updated built-in CLI help and Draxul skill. Skill quick validation passed.
- Actual server discovery/topology rename integration verifies both setting and
  clearing aliases reach agent clients/get without changing the instance ID.
  CLI tests cover all targeted verbs, missing/duplicate names and pinned waits.

## Validation

- Reused Release cache, no configure/generate. Initial core aggregate compilation
  took 50.70s; initial 56-entry CTest run took 25.76s and exposed rejection of empty
  aliases. Fixed the decoder to accept the intentional unnamed state. Incremental
  rebuild took 9.78s; full aggregate then passed 56/56 in 24.37s.
- No separate focused tests, render changes or remote CI run for this slice.
- Default-runtime smoke attempts exited 1. A Neovim-only same-cache smoke passed;
  shared-server smoke is being checked with an explicit isolated runtime. The
  first attempted environment-only override was ineffective for GUI launch;
  no isolated server was created in that attempt and the existing server was
  left running. Do not treat an environment override as equivalent to the CLI
  --server-runtime-dir flag for GUI validation.
- Final same-cache shared-server smoke passed in 0.64s with an explicitly owned
  server and short /tmp runtime; shutdown succeeded. macOS's long default temporary
  directory exceeded the Unix socket path limit in the first explicit-runtime
  attempt (30s bootstrap timeout); direct launch exposed the path-length error.
  Retesting with a short runtime resolved that validation-environment issue.
- Existing default runtime/server was not stopped. Restart the server and UI
  together to adopt the new required alias field. Work remains uncommitted.

## Related discussion (not implemented)

For a quiet native Personal Assistant chat, let the Draxul server own a structured
provider connection and publish conversation messages separately from tool events.
Codex app-server supports start/resume, turn/start, streamed agent messages with
commentary/final phases, approvals and interruption. Render final replies as bubbles
and keep activity collapsed. This is a proposed separate feature, not terminal
output filtering or an implemented part of the alias change.
