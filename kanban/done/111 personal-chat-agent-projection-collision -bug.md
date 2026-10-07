# Personal chat collisions blank terminal panes

Structured and terminal-backed projections can claim the same agent ID. The duplicate invalidates the full session poll response, starving terminal attach/output and hiding new tabs even though their shells run.

- [x] Fix the source of invalid state without discarding user data.
- [x] Add a regression at the provider/session protocol boundary.
- [x] Run aggregate tests and same-cache smoke.
- [x] Verify live repair.

## Findings

The live agent snapshot contained each personal identity twice: terminal and structured routes. The strict client decoder correctly rejected it, taking the entire Session poll (including new topology and terminal attach/output) with it. ServerAgentService now reserves structured IDs before discovery and emits one projection per identity, preferring the authoritative structured conversation. Multiple views cannot duplicate a projection. No stored topology or terminal contents are deleted.

Extended the existing Session poll integration test with colliding terminal/structured IDs and duplicate structured views; the full response still decodes and both independent terminal subscriptions attach and receive updates. Rebuilt/restarted the current Draxul installation, focused the user's existing pane-208, and captured `/tmp/draxul-terminal-recovery.png`: the shell prompt is visibly rendered again. The extra Zsh tabs the user created already existed server-side.

## Validation

Final `python3 do.py test release`: 56/56 core entries passed in 20.38 s; same-cache aggregate build 7.63 s, no configure/generate rerun. Prior aggregate 22.20 s caught a missing required evidence field in the new test fixture; corrected and reran. No separate focused run. Same-cache personal startup passed in 0.61 s. No renderer changes or reference reblessing; live terminal screenshot inspected. Windows/remote CI not run. `git diff --check` clean. Changes not committed/pushed.
