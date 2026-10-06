# Investigate intermittent Windows validation failures

**Summary:** Find out why server-state checks and startup sometimes time out or observe old state, so a successful retry does not hide an unreliable test or real startup problem.

**Priority:** P2
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
