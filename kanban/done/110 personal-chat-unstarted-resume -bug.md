# Unstarted personal conversations fail to resume

A new Codex thread has no persisted rollout until its first turn. Draxul saved its transient ID immediately, so opening a personal chat, restarting, then reopening produced no rollout found. Preserve backing instructions and real history.

- [x] Fix the source of invalid state without discarding user data.
- [x] Add a regression at the provider/session protocol boundary.
- [x] Run aggregate tests and same-cache smoke.
- [x] Verify live repair.

## Findings

Confirmed live Calendar and StocksFrom errors: `no rollout found for thread id ...`. Both local chat records contained zero messages and their IDs had no corresponding Codex rollout. AI News had real history and was preserved.

New unstarted conversations retain transient IDs only in memory. The local record saves a resumable ID after the provider's first `turn/started`; resumed durable threads keep theirs. The fake provider now rejects resume before the first turn, reproducing the lifecycle boundary. Regression covers restart before any message; existing integration covers actual history resume. Automatic replacement of missing established provider history is now separately covered by [card 112](112%20personal-assistant-automatic-recovery%20-feature.md), following the user’s explicit lifecycle clarification.

The two damaged empty records were backed up beside their JSON files with suffix `.json.before-unstarted-repair-20261007.bak`, then only `thread_id` and `interrupted` were cleared. All shared Dropbox files and AI News history were preserved. Live verification completed on 2026-10-07: Calendar, StocksFrom and AI News all reached Ready with empty error states. An earlier application launch left the metadata worker stalled while enumerating Dropbox; relaunching through the normal executable from the repository cleared the stall without changing shared data or OS permissions. The sample is retained at `/tmp/draxul-server-recovery-sample.txt`.

## Validation

Final `python3 do.py test release`: 56/56 core entries passed in 20.38 s; same-cache aggregate build 7.63 s, no configure/generate rerun. Prior aggregate 22.20 s caught a missing required evidence field in the new test fixture; corrected and reran. No separate focused run. Same-cache personal startup passed in 0.61 s. No renderer changes or reference reblessing; live terminal screenshot inspected. Windows/remote CI not run. `git diff --check` clean. Changes not committed/pushed.
