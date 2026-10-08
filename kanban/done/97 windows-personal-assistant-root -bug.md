# Windows Personal Assistant collection and server

The Windows Personal Assistant is unavailable because local config points at the
example collection and the default Release server still runs the previous
Personal Assistant protocol. The shared collection already exists under
`C:/Users/cmaughan/Dropbox/Vault/PA`, with schema 3 and three agent folders.

- [x] Inspect config, shared collection, current repositories and server state.
- [x] Preserve local settings and change only `[agents].personal_root`.
- [x] Build current Draxul and reload the default server without disrupting terminals.
- [x] Verify the shared collection loads without errors and startup succeeds.
- [x] Run the appropriate aggregate and same-cache smoke; record validation costs.

Initial evidence: default server PID 16560 uses the Release cache, has zero
connected clients, zero agents, zero terminals and a healthy checkpoint. Its
personal snapshot reports `sample-personal-assistant`, the example path and an
unsupported schema error. Preserve the real collection identity and all agent
data. Do not create example agents or submit prompts as part of diagnostics.
Git sync found root and all six product origins current; unrelated local
Flashcards work remains intact.

## Resolution, 2026-10-06

Backed up the local config beside it as `config.toml.pa-root-20261006.bak` and
replaced only the example root. Startup rewrote the configured font path to the
Debug bundle; restored that
automatic change afterward and verified the complete config differs from the
backup only by the intended Personal Assistant root replacement.
The original shared collection identity and its three existing definitions
(AI News and two named Codex) load with schema 3 and
empty collection/agent errors. No agent was created, renamed, deleted or sent a
prompt. No provider conversation was launched to verify live authentication.

Both caches had an old Windows helper even though their clients were current.
After inspecting zero-client/zero-agent startup-test servers and their idle shell
screens, shut down diagnostic PIDs 54208 and 61620 and the old default server.
An initial direct helper launch reproduced schema 1 rejection; the supported
client launch refreshes the helper. An immediate refresh before shutdown finished
was rejected with Windows error 5; waited for shutdown and retried successfully.
The final default server is Release PID 85520, healthy restored checkpoint,
current schema 3, the correct root and three error-free definitions. Documented
Windows configuration and normal client/helper restart in the canonical guide.

| Validation | Evidence and cost |
|---|---|
| Configure/generate | Both existing Ninja caches reused; no configure/generate pass. |
| Debug build | App target passed, 15.13s build window, 8 staging/generation steps; no C++ recompilation. |
| Core aggregate | `py do.py test debug`: core aggregate build passed, 16.19s / 8 steps. 54/56 CTest groups passed, 60.40s CTest / 60.55s runner. Personal host, server, config and CLI groups passed. |
| Existing aggregate failures | Joined family grapheme remains seven glyphs; scope fixture still selects an empty set. Owned by `kanban/done/63 joined-family-emoji-fallback -bug.md` and `kanban/done/64 windows-test-scope-selection -bug.md`; no assertions weakened or unrelated fixes made. |
| Standard same-cache smoke | `py do.py smoke --skip-build`: timed out at 30s, exit 124, owned PID 86912 stopped. Existing gate remains with `kanban/pending/65 windows-validation-timing -test.md`. |
| Actual-profile Personal startup | Debug `--smoke-test --host personal-assistant` passed exit 0 in 19.69s; no rebuild. |
| Final Release | `py do.py run release --console -- --smoke-test --host personal-assistant` passed exit 0; build 17.06s, 10 steps including incremental Flashcards compilation/linking. Startup not separately timed. |
| Real collection | Repeated read-only CLI checks passed after current Debug and final Release server reload. Final Release reload/verification approximately 2s. |
| Other | Whitespace check passed. No added tests, render snapshots, product-suite rerun, macOS execution or remote CI. |

Logs are retained in the ignored Debug tree: `pa-root-core-aggregate.log`,
`pa-root-smoke.log`, `pa-root-personal-smoke.log` and `pa-root-release.log`.
The configuration repair is complete. Existing generic test/smoke failures
remain with the distinct cards linked above; this card does not claim to fix them.
