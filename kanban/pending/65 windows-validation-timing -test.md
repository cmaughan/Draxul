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
