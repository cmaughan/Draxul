# Allow shell tabs from Kanban launches

An interactive `--host kanban` launch must retain access to the built-in
shell hosts. Today it starts a local-only session, so the palette advertises
`new_tab` but creating a Zsh tab fails.

**Work**

- [x] Start interactive client-local hosts in the shared Session and project their requested startup tab.
- [x] Preserve Kanban source paths and standalone screenshot/render behavior.
- [x] Cover CLI routing and Kanban-to-shell creation through the command palette.
- [x] Honor an explicitly selected shell kind when the server allocates the new tab.
- [x] Run the core aggregate, same-cache startup smoke, and relevant render check.

**Notes**

The local client registers Neovim, Markdown, and Kanban providers, but no local
shell provider. Shell tabs are server-owned. `should_use_shared_server` excludes
explicit client-local hosts and source-path launches, so the palette's shell
actions cannot complete from a Kanban launch. A normal interactive client-local
launch should create its first tab in the shared topology, as plugin launches
already do; screenshot/render flows keep their isolated behavior.

**Implementation (2026-09-30):** Interactive `--host kanban` and
`--host markdown` launches now join the shared Session and create a selected
client-local startup tab. The launch command carries the caller's working
directory and optional source path, so a Kanban board still resolves from the
directory where Draxul was started. Screenshot and render-test launches remain
local. Explicit palette shell choices now travel through the topology command
to the server terminal runtime; the choice is stored in the topology so a
restored Session can recreate the same shell. Server commands reject unknown
shell kinds.

**Validation:** CLI routing, a Kanban-to-shell palette app smoke test, the
topology mutation route, and server topology shell persistence passed focused
tests (133 assertions across six cases). A real Release `--host kanban
--source kanban --smoke-test` launch succeeded in an isolated runtime and
created a second tab in the shared Session; the isolated server shut down
cleanly. The Release core aggregate passed 25/25 CTest entries in 33.82s,
same-cache startup smoke passed in 0.49s, and the basic render scenario
passed in 1.04s. Hosted Windows CI covers the affected app, protocol, and
server test paths, so no separate manual gate is needed for this change.
Launching an explicitly selected shell still requires that shell executable
to be installed on the target machine; a missing optional executable is
reported as a process-start failure. Any CI regression will be triaged when
reported.
