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

Boundary-focused CTest labels include `app-shell` for pure shell layout/state tests
and `host-api` for neutral host registry/API tests; both targets also participate in
the owning core aggregate.

## Remaining work

- [x] Correct the stale renderer hierarchy in `docs/module-map.md`; `I3DRenderer`
      no longer exists. Fixed during the 2026-08-17 tracker audit.
- [x] Regenerate or remove generated API pages that still publish the old module map
      and deleted `modules/{megacity,satview,score}` paths.
- [x] Audit current root and nested guidance for named files/targets that no longer
      exist, using CMake and `tests/CMakeLists.txt` as authority.
- [x] Track the ownership and focused-test mappings accepted in
      `plans/reviews/runs/20260921T112827Z-review-refactor-summary-f86ba609/summary.md`
      as those boundaries land; distinguish proposed targets from current ones.
- [x] Replace the hardcoded `build-ninja-release` development example in
      `plugins/megacity/product/AGENTS.md` with the canonical selected Debug cache.
      Keep root `AGENTS.md` a pointer to `CLAUDE.md` and update product guidance in
      its owning repository.
- [x] Decide the remaining exact-label/Catch selection interface together with
      `kanban/done/35 streamline-local-build-validation-workflow -refactor.md`;
      do not add a second competing test runner here. `do.py test --label <label>`
      now composes an anchored CTest label filter with the selected core/product
      scope and fails when the intersection contains no tests.
- [x] Add lightweight validation for current file/target references where it can avoid
      this class of drift without parsing prose heuristically.

## Acceptance criteria

- [x] Canonical and generated current documentation names only live interfaces, paths,
      targets, and validation commands.
- [x] Root guidance remains the shared source; product guidance stays in its owning
      plugin repository.
- [x] Focused validation has one documented interface and tested Windows/macOS command
      construction.

## Final validation

- Regenerated the linked class-diagram source and SVG with current interfaces; the
  generated artifacts no longer contain `I3DRenderer` or retired product paths.
- The generator now discovers active macOS SDK/compiler resource paths instead of
  pinning a stale Homebrew compiler version, and a named-diagram run renders only
  that diagram.
- `python3 -m unittest tests.do_py_tests` passed 74 tests (one platform-only skip),
  including command construction and generated/current guidance checks.
