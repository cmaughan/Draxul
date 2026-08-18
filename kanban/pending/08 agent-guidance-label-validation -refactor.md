# Finish agent-guidance accuracy and focused validation

**Type:** refactor
**Priority:** P2 / sequence 08
**Raised by:** GPT/Codex and Claude
**Consensus:** `plans/reviews/review-refactor-consensus.md`, Accepted 9

## Delivered checkpoint

Commit `b88906f0` aligned `CLAUDE.md`, README, `do.py`, and wrapper guidance on the
Debug/Ninja development cache, core-by-default tests, product opt-in scopes, bounded
parallelism, and same-cache smoke. Product-specific guidance now belongs to each
plugin repository rather than deleted `modules/*` paths.

## Remaining work

- [x] Correct the stale renderer hierarchy in `docs/module-map.md`; `I3DRenderer`
      no longer exists. Fixed during the 2026-08-17 tracker audit.
- [ ] Regenerate or remove generated API pages that still publish the old module map
      and deleted `modules/{megacity,satview,score}` paths.
- [ ] Audit current root and nested guidance for named files/targets that no longer
      exist, using CMake and `tests/CMakeLists.txt` as authority.
- [ ] Decide the remaining exact-label/Catch selection interface together with
      `kanban/pending/35 streamline-local-build-validation-workflow -refactor.md`;
      do not add a second competing test runner here.
- [ ] Add lightweight validation for current file/target references where it can avoid
      this class of drift without parsing prose heuristically.

## Acceptance criteria

- [ ] Canonical and generated current documentation names only live interfaces, paths,
      targets, and validation commands.
- [ ] Root guidance remains the shared source; product guidance stays in its owning
      plugin repository.
- [ ] Focused validation has one documented interface and tested Windows/macOS command
      construction.
