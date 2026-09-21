# Agent integration installer boundary

This note records the compatibility contract for extracting native-session hook
installation from application CLI presentation. `draxul-agent-integration` owns
document transformation, generated hook scripts, persistence, permissions, and
typed inspection. The app resolves environment and home directories, parses the
command line, converts typed status to text or JSON, and selects exit codes.

## Preserved provider contracts

Both providers use integration version 2 and install one managed
`SessionStart` command with a ten-second timeout. Reinstallation first removes
only entries that reference the explicit managed-hook path, then appends one
current entry. Other events, hook groups, settings, and top-level keys remain
unchanged. Uninstall edits registration before removing a script and refuses to
remove a script without its provider-specific Draxul ownership marker.

Codex paths are `<CODEX_HOME>/draxul-agent-session.{sh,ps1}`, `hooks.json`, and
`config.toml`. Its registration group has no matcher. Installation sets
`hooks = true` in `[features]`, including an indented section header, without
changing other TOML lines. Status is current only when the versioned script is
registered and that feature is enabled.

Claude paths are `<CLAUDE_CONFIG_DIR>/hooks/draxul-agent-session.{sh,ps1}` and
`settings.json`. Its registration group retains `matcher: "*"`; Claude does not
use the Codex features document. Status is current when the versioned owned
script is registered.

The POSIX scripts remain executable and use Python only after validating Draxul
pane/session environment. PowerShell commands retain `-NoProfile` and
`-ExecutionPolicy Bypass` quoting. Both payloads route server epoch, runtime
generation, and server runtime directory together. Codex reports its native
session reference directly; Claude also rejects nested-agent payloads and sends
`--ref-kind id`.

## Typed API and tests

Callers pass all directory, hook, registration, and feature paths through
`AgentIntegrationPaths`; the library never reads process environment. Requests
select a provider and install/uninstall action. Results contain success/error
and an inspected typed status (`unavailable`, `not_installed`, `current`,
`outdated`, or `invalid`) without exposing the private JSON implementation.

Focused tests use temporary directories directly and cover idempotency,
unrelated-content preservation, malformed documents, current/outdated status,
owned-only removal, generated payload routing, POSIX permissions, and file
errors. A link-isolation consumer includes only the public header and links only
`draxul-agent-integration`.
