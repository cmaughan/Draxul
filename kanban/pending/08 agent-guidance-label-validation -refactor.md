# Repair agent guidance and add label-level validation

**Type:** refactor
**Priority:** P1 / sequence 08
**Raised by:** GPT/Codex and Claude
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 9

## Goal

Make canonical/root/nested guidance describe the final live architecture and add
a stable cross-platform `python do.py test --label <label>` workflow that builds
the smallest owning test target before invoking CTest.

## Boundary verification

- [ ] Inventory stale symbols, source paths, target names, commands, and shard counts
  in `CLAUDE.md`, root/nested `AGENTS.md`, README, `do.py`, and test wrappers.
- [ ] Use `tests/CMakeLists.txt` and live `ctest -N` as target/label authority.
- [ ] Verify final target names from pending host/Nvim/product boundary cards.
- [ ] Confirm SatView/Score rules that genuinely need nested scope rather than root duplication.
- [ ] Distinguish this work from ice-boxed AI review-script deduplication.

## Implementation and migration

- [ ] Correct deleted `I3DHost`/`IGridHost`/`attach_3d_renderer` architecture claims.
- [ ] Replace invalid `draxul-tests.exe`, `ctest -R draxul-tests`, and deleted test-file guidance.
- [ ] Remove hard-coded shard-count prose from `do.py` help/README where it can drift.
- [ ] Implement `python do.py test --label <label>` for known CTest labels and owning build targets.
- [ ] Handle optional-disabled labels with a clear, deterministic result.
- [ ] Keep Windows multi-config and macOS single-config command construction aligned.
- [ ] Add `modules/satview/AGENTS.md` and `modules/score/AGENTS.md`; add thin root pointers.
- [ ] Update `modules/megacity/AGENTS.md` to current focused targets/files.

## Unit tests

- [ ] Add Python tests for label parsing, unknown labels, target mapping, and command construction.
- [ ] Add tests for optional-disabled labels and configuration selection.
- [ ] Verify `python do.py test --label core`, `app`, `kanban`, `megacity`,
  `satview`, `scoreview`, and `scoreview-host` select expected targets/labels.
- [ ] Run `python -m unittest tests.do_py_tests`.
- [ ] Run at least one real focused label command on the current platform.

## Cross-platform validation

- [ ] Windows: verify `--build-config`/`-C` and owning target selection under MSVC.
- [ ] macOS: verify single-config build and CTest label selection.
- [ ] Ensure nested guides require optional ON/OFF checks and paired Vulkan/Metal inspection.
- [ ] Record unavailable platform execution instead of claiming unrun coverage.

## Agent documentation and tooling

- [ ] Keep shared rules canonical in `CLAUDE.md`; root `AGENTS.md` remains a thin Codex pointer.
- [ ] SatView guide: target direction, immutable worker handoff, offline fixture,
  assets, ON/OFF builds, and Vulkan/Metal scene parity.
- [ ] Score guide: notation/learn/input/audio/model/host direction, GPU-free learn,
  async engrave, generated assets, focused labels, and macOS TCC ordering.
- [ ] Megacity guide: current focused target, CTest label, and current test files.
- [ ] Add a lightweight validation for named current files/targets where practical.

## Acceptance criteria

- [ ] No canonical/current guide tells agents to use deleted interfaces, files, or binaries.
- [ ] `python do.py test --label <label>` is stable and tested on both command-construction paths.
- [ ] Root and nested guides have clear scope with no copied shared rule blocks.
- [ ] Help/README describe focused tests without a drift-prone shard total.
- [ ] Python tests, focused command, full docs/hygiene checks, and smoke pass.

## Dependencies and ownership

Land after `kanban/pending/02 host-layer-static-libraries -refactor.md` through
`kanban/pending/07 scoreview-analysis-overlay-boundary -refactor.md` so guidance
names final targets. One guidance/tooling owner controls canonical text and
`do.py`; SatView and Score guide drafts can be independent only after target
names stabilize.
