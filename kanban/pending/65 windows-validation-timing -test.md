# Investigate intermittent Windows validation failures

**Summary:** Find out why server-state checks and startup sometimes time out or observe old state, so a successful retry does not hide an unreliable test or real startup problem.

**Priority:** P2 — Resolve recurring Windows startup and observer timing failures.

## Windows gate sweep — 2026-10-08

`py do.py validate debug` reproduced the default-profile smoke timeout (30.64s)
before CTest began: the client reported a server control-request deadline and no
running server. The preflight had confirmed no live server. No existing user
server was stopped. The same executable passed `py do.py smoke debug --skip-build`
with fresh `APPDATA`/`LOCALAPPDATA` at
`D:/dev/Draxul/build-windows-smoke-profile`; this isolates a profile-dependent
startup difference, not a proven fix or explanation.

The aggregate also reproduced the existing remote initial-state failure at
`remote_terminal_host_tests.cpp:1159` (observer 0x0, deadline exceeded). It ran
73/79 entries successfully in 232.92s. Logs are retained under
`build-ninja-debug/validation-logs/20261008-150658-521060/`.
Keep this card pending; a successful isolated-profile run does not erase either
failure. The newly exposed blocked-write cancellation failure is owned by
`71 cancellable-nvim-output -bug.md`, not folded into this timing investigation.

The final core + Rezonality aggregate still failed remote initial-state receipt
(64/70 entries passed, 306.89 s). Its paired fresh-profile Debug smoke passed
(35.273 s including environment setup), and fresh-profile Release smoke passed
(33.867 s). A separate Release attempt failed before application startup because
the machine ran out of resources while initializing MSVC; PowerShell also
reported CoreCLR error `0x800705AA`. Recovery allowed startup without code changes.
This environmental incident is not a diagnosis of the original default-profile
timeout. Final logs are in `build-ninja-debug/windows-gates/`.
**Source:** Windows validation of [62 shared-zlib-install-exports -bug.md](../done/62%20shared-zlib-install-exports%20-bug.md), 2026-10-01.

- [x] Record failures and confirm that the same-cache reruns pass.
- [ ] Reproduce with retained logs and controlled concurrency; distinguish test synchronization errors from product failures.
- [ ] Correct the proven cause without merely increasing timeouts or accepting stale state.
- [ ] Run the affected aggregate and same-cache smoke with repeated focused checks where useful, then final Release startup.

Observed in the 68-entry core/Rezonality aggregate:

- `remote terminal host renders shared state and can take control` failed `received_initial_state` at `tests/remote_terminal_host_tests.cpp:1159`, reporting `deadline_exceeded` and an observer of size 0x0.
- `server periodically checkpoints topology without a UI` read `Space 1` instead of `Periodically Saved` at `tests/server_session_checkpoint_tests.cpp:926`. The test accepts any `checkpoint_state == "ok"` before checking the persisted rename; investigate whether this can acknowledge an earlier checkpoint.
- Both affected CTest shards passed on serial rerun (17.22 and 13.75 seconds).
- Debug smoke timed out at its 30-second wrapper limit while other validation was active; same-cache retry passed. Release startup also passed. No existing user server was stopped.

These observations do not establish a common root cause. Split this card if investigation finds unrelated fixes.

2026-10-02: the stale periodic-checkpoint acceptance was corrected as part of
`kanban/done/83 retired-server-checkpoint-ownership -bug.md`. An earlier successful
checkpoint can contain the initial layout; the test now waits until the requested
rename is present in the durable snapshot. Both server CTest shards passed in the
final core/product aggregate. This resolves that test-synchronization observation;
remote initial-state and default-profile smoke timeouts remain open.

During the later review-runner update on 2026-10-01, `py do.py smoke debug --skip-build` timed out again at 30 seconds (owned PID 52052 terminated by the wrapper). No application code changed in that turn. The tooling tests passed; the recurring startup problem remains open here.

During Flashcards validation later that day, two standard same-cache Debug smoke
attempts timed out at 30 seconds (owned PIDs 19964 and 58496, stopped by the wrapper).
The retry's `build-ninja-debug/flashcards-core-smoke-trace.log` reached Vulkan
swapchain creation. A direct Debug smoke selecting `dev.draxul.flashcards` exited
0, as did final Release plugin startup. Release compilation overlapped these
checks; that concurrency is evidence to investigate, not an established cause.
The existing user server was left running. This card continues to own host smoke
investigation; the product's validation record is
`plugins/flashcards/docs/validation.md`.

## Upstream integration validation (2026-10-02)

The published checkpoint merged upstream Personal Assistant/plugin tab reuse with
the five critical fixes and Flashcards native cues. The existing Ninja Debug cache
regenerated and built 239 steps in 165.88s (separate configure/compile times were not
measured). `py do.py test debug --flashcards` passed 56/60 CTest entries in 151.42s.
The emoji, remote initial-state and scope-selection failures remain unchanged.
The native Flashcards cue lifecycle passed in 128.77s; the ordinary Flashcards
render failed its cached-pronunciation reveal/replay assertion in 22.22s and remains
within the product cue/audio follow-up. No source fix is inferred from this sync.
Same-cache Debug smoke passed with a short isolated profile in 8.99s including owned
server cleanup. The user server was left running. No Release or remote-CI result
is claimed for this Git integration turn.

## Flashcards human-audio validation, 2026-10-02

`py do.py test debug --flashcards` passed 58/61 entries; all five product groups
passed. The existing remote-terminal initial-state deadline, emoji and scope
selection failures remained. After a final product-only adjustment, 5/5 product
groups passed again. Standard same-cache Debug smoke timed out at 30 s (owned
PID 53680 stopped; outer 36.84 s); isolated Flashcards startup passed in 2.89 s.
Release built successfully; first isolated startup returned 1 without a retained
app diagnostic, then a fresh same-executable launch with logging passed in 2.95 s.
The cause of that first Release result is unknown. Existing user instances were
left running; no user state was changed. Costs and logs remain in the product
`docs/validation.md` and ignored build tree. These new audio edits are uncommitted.

During the subsequent printed-word visual slice, all five product groups passed
again (194.53 s); the standard same-cache Debug smoke passed in 27.79 s outer time.
Final isolated Release plugin startup also passed (13.34 s build, 15.48 s outer).
This passing run does not establish a cause or resolution for the earlier timing
failures. No broader core repeat was needed for the product-only visual change.

## Flashcards queue/cooldown checkpoint, 2026-10-06

All six current product groups passed on the synchronized host (288.84 s).
Standard same-cache Debug smoke again timed out at its 30-second bound; owned
PID 47340 was stopped, outer 34.48 s. A supervised isolated Flashcards startup
passed in 2.475 s with frame readiness logged. This does not establish a cause or
fix for the default-profile timeout. Product timings, failures and normal/narrow
captures are in `plugins/flashcards/docs/validation.md` and the ignored Debug tree.

## Flashcards shared scheduling checkpoint, 2026-10-06

All eight product groups passed (364.60 s), including actual offline grades,
three-device convergence and stale revealed-answer rejection. The standard
same-cache Debug smoke again hit its 30-second bound; owned PID 76388 was stopped.
Actual-profile Flashcards startup/setup passed exit 0 with frame readiness and
byte-for-byte preservation of existing recall. This does not establish a cause
or fix for the default-profile timeout. Product timing, diagnosis and final
normal/narrow footer evidence are in `plugins/flashcards/docs/validation.md`.

After fast-forwarding the independently published personal-agent host update
`95493166`, all eight Flashcards groups passed again (365.51 s). Standard
same-cache Debug smoke timed out at 30 s again and stopped owned PID 79208.
Updated-host Release Flashcards startup passed exit 0. No default-profile timing
cause or fix is inferred; no broad core runtime result is claimed.

## Flashcards draggable queue / immediate rounds, 2026-10-06

The standard same-cache Debug smoke again timed out at 30 seconds (exit 124);
the runner stopped owned PID 47604. A supervised Flashcards-only startup in the
same Debug cache passed exit 0 in 20.08 seconds. Final Release Flashcards startup
also passed exit 0 after a 94.50-second incremental build. No default-profile
timeout cause or fix is inferred. Current native capture diagnostics and costs
are retained in `plugins/flashcards/docs/validation.md` and ignored build logs.
These product-only edits do not claim a broader core runtime pass.

## Windows Personal Assistant root repair, 2026-10-06

Core aggregate passed 54/56 groups in 60.40s; the two failures remain the existing
family-grapheme font and test-scope fixture issues, cards 63 and 64. Standard
same-cache Debug smoke again timed out at 30s (exit 124); owned PID 86912 stopped.
Actual-profile Personal Assistant startup passed exit 0 in 19.69s from that Debug
cache, and final Release Personal startup passed. The default server now uses the
current Release helper, configured shared Dropbox collection and schema 3, with
three error-free definitions. No default-profile timeout fix or cause is claimed.
See `kanban/done/97 windows-personal-assistant-root -bug.md`; ignored logs are
`build-ninja-debug/pa-root-*.log`.

## Default-profile smoke: cause proven and corrected — 2026-10-08

**Cause (test isolation, not a startup hang).** `do.py smoke` launched
`draxul.exe --console --smoke-test` against the default runtime, which belongs
to the user's live server (here PID 45472, a `D:` Release cache). The smoke
client therefore attached to the live Session and, inside the first
`pump_once()` of `App::run_smoke_test()`, projected and synchronously
initialized every pane of every Space before its 3-second readiness check ran.
A `cdb` stack sample at 15 s showed the main thread in
`App::consume_remote_session_state → … → PaneManager::create_host_for_leaf →
PersonalAssistantHost::initialize → … → VkRenderer::set_atlas_texture`.
Uncapped, the same default-profile smoke exited **0 after 54 s** (live Session:
1 Space, 9 panes: 3 plugins, Kanban, Markdown, Personal Assistant, 3
terminals). The duration therefore measured live user state, which explains why
every fresh-profile run passed and the default profile intermittently exceeded
30 s.

**Fix.** `run_isolated_smoke()` in `do.py` gives both `do.py smoke` and the
smoke step of `do.py validate` a short temporary server runtime
(`--server-runtime-dir` plus `DRAXUL_SERVER_RUNTIME_DIR`) and, on Windows, fresh
`APPDATA`/`LOCALAPPDATA`. It always shuts down the server the smoke launched
and removes the directory, retrying for 5 s because the final checkpoint lands
after the shutdown acknowledgement. The 30 s bound is unchanged. Results:
`py do.py smoke debug --skip-build` passed 4/4 (about 6 s each) with no
leftover servers or directories; `tests/do_py_tests.py` 88 tests OK (2 skipped),
including the updated smoke-command assertion. The user's live server was never
stopped.

- [x] Default-profile smoke: reproduce with retained logs, distinguish test from product, correct without raising the timeout.

**Remote initial-state (`remote_terminal_host_tests.cpp:1159`) — still open.**
It passed in the 2026-10-08 core+products aggregate (90 entries, 533 s), in
20/20 serial repeats (`--repeat 20`, 53 s), and in 24 runs at 8-way process
concurrency. No reproduction, so no cause is claimed and the deadline was not
changed. Keep the first two unchecked items for this case.

**Product follow-up noticed, not fixed here.** Attaching a UI to a Session
initializes every projected host synchronously on the main thread (about 50 s
for this 9-pane Session in Debug), so an attaching window is unresponsive while
plugins and the Personal Assistant initialize.
