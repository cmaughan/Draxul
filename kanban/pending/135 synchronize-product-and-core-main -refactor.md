# Synchronize product and core main branches

**Type:** refactor
**Priority:** P1 / sequence 135
**Raised by:** user, 2026-09-25

## Goal

Integrate the validated `codex/kanban-pending-implementation` work into the
five product repositories and the root repository's `main` branch, preserving
independent work already present on each `main` branch.

## Work

- [x] Fetch and inspect all six repositories and their branch divergence.
- [x] Merge the feature branch into each product `main`, resolving conflicts.
- [x] Validate the combined root and product checkout on macOS.
- [x] Push all product `main` branches and record their final commits.
- [x] Commit and push the root submodule pointer update and integrate root `main`.
- [x] Leave the working root checkout on `main`.
- [ ] Verify the Windows CI gate for the integrated root `main` commit.

## Notes

- Rezonality's `live_project.cpp` merge retained model decoding from the feature
  branch and active shader-source tracking from `main`.
- SatView and ScoreView retained five deferred cards from their `main` branches
  that would otherwise have been lost during the merge.
- Product `main` commits: MegaCity `4bb74c8`, PCBView `49e7de6`, Rezonality
  `1f36e9d`, SatView `5a8f4cc`, ScoreView `42724e5`.
- Root `main` and the working feature branch both advanced through pointer commit
  `b08cee39`. MegaCity and PCBView required no pointer change because their
  feature tips became `main` by fast-forward.
- `python3 do.py validate debug` passed the build, smoke, and all five render
  scenarios, but one core terminal teardown assertion failed during the 47-entry
  CTest pass. That test passed three focused repeats, then
  `python3 do.py test debug --all` passed 47/47 entries; a subsequent
  `python3 do.py smoke debug --skip-build` passed on the same cache.
- Root `main` reached `6abc6e09` with all five product pointers published.
  [Hosted Build run](https://github.com/cmaughan/Draxul/actions/runs/36113740950)
  failed before compiling on both macOS and Windows: `actions/checkout` cannot
  read the private `cmaughan/draxul-pcbview` submodule with the workflow token.
  The Windows gate remains open until checkout receives repository access and
  the workflow is rerun.
