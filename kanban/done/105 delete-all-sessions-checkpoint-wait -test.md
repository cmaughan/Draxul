# Wait for published checkpoints in the server delete-all Session test

**Priority:** P2
**Summary:** "server deletes all detached Sessions and stops their terminals" asserted that each renamed Session's checkpoint file existed immediately after `ServerClient::rename_session`, but renaming only schedules the write, so the test failed under host load.

- [x] Add a bounded `wait_for_session_checkpoint` test helper that polls `ServerClient::status` until the Session's checkpoint is published (`ok` plus the file on disk), failing early on `failed`.
- [x] Use it after the delete-all renames, and verify each published checkpoint carries the new Session name.
- [x] Replace the other 1-second `checkpoint_state == "ok"` polls in `tests/server_session_checkpoint_tests.cpp` (delete-one initial and post-rename waits, corrupt-checkpoint archive, partial restore, periodic checkpoint) with the helper or a 10-second deadline. The delete-one post-rename poll previously fell through silently on timeout.
- [x] Reproduce under fsync-heavy I/O load, then repeat the fixed `[delete-all]` case 20 times, and run the core aggregate and startup smoke from the same Release cache.

`ServerKernel::Impl::rename_session` (`libs/draxul-server/src/server_kernel_sessions.cpp`) calls `checkpoint_session`, which sets the Session to `writing`, saves a staged file on a detached thread and returns. `collect_checkpoint_results` moves the file into place later on the server loop. The test therefore depended on the writer thread and the next loop iteration having run before the client's next statement. On 2026-10-05 it failed 4/4 consecutive runs on a macOS Release build at load average ~7, then passed once load dropped. Disabling `AgentUsageMonitor` did not change the failure.

Because the handler sets `writing` before replying, a following `ok` state proves the rename's checkpoint was published, not an earlier periodic one. Assertions that run after `run_guard.join()` are already deterministic, since shutdown waits for the final checkpoint. Rename and delete already retry `checkpoint_busy` in `ServerClient` for up to 2 seconds. Product code is unchanged.

Pure CPU load (14 busy processes on 10 cores, load ~15) did not reproduce the failure: the old test passed 10/10. The writer `fsync`s the staged file and its directory, so the trigger is I/O latency from concurrent builds. With six processes repeatedly writing and `fsync`ing 64 MiB files, the old test failed on its first run at the `exists(alpha_checkpoint)` assertion. The fixed test passed 20/20 under the same load, and all 13 `[persistence]` cases passed 3/3 under it. The core Release aggregate passed all `draxul-test-server` shards. Its one failure, `draxul-do-py-tests`, came from the uninitialized Megacity submodule in the validation worktree and is unrelated to this change. The Release startup smoke passed.
