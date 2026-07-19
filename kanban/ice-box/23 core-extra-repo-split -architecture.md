# Split Draxul into a public terminal core and a private extras tree

**Type:** architecture
**Priority:** 23
**Raised by:** Claude (Fable 5) with the owner, repo-structure discussion 2026-07-18

## Goal

Public GitHub = the Draxul terminal (app, core libs, markdown + kanban
hosts). A second, private repo = the extras (`modules/{score,satview,
megacity}` with their tests, fixtures, plans notes, and kanban cards).
Locally the two combine into ONE build via a sibling-checkout overlay, so
daily "work on everything as a whole" is unchanged. Decision rationale —
including why not submodules, why not subtree, and the recorded
filtered-mirror fallback — lives in plans/repo-split-core-extras.md.

## Owner decisions (flagged, not assumed)

- [ ] Confirm the public set: markdown + kanban hosts stay in the terminal
      product? nanovg-demo host public or extra?
- [ ] Public history hygiene: filter the extras out of the public repo's
      history (clean; only cheap while the repo is still private) vs
      delete-going-forward with history retained.
- [ ] One extras repo (recommended) vs per-module repos.

## Phase 0 — make the tree splittable (in-repo, no git surgery, valuable standalone)

- [ ] One registration seam: replace the three `#ifdef` blocks in
      `app/main.cpp` (~:526) with a single
      `register_extra_host_providers(registry)` guarded by
      `DRAXUL_HAVE_EXTRA_HOSTS`, provided by the extra tree and absent
      otherwise. Unknown-host UX is already handled by
      `validate_host_provider_availability` (coordinate with pending
      `12 host-provider-availability -bug.md`).
- [ ] `DRAXUL_EXTRA_DIR` cache variable (default: `../draxul-extra` when
      present) + conditional `add_subdirectory`; presets `mac-debug`
      (public-only) and `mac-debug-all` (combined); CI matrix proves BOTH
      configurations green (build + unit suites + smoke; render scenarios in
      the combined leg).
- [ ] Module tests move home: `tests/scoreview_*`, `tests/satview_*`,
      megacity tests + their fixtures (Grieg, Swan Lake, the minimal SVG)
      into `modules/<x>/tests/` with per-tree CTest registration; the public
      tree exports a `draxul-test-support` interface target (test_pch,
      temp_dir, the ScoreHost fixture). This IS pending
      `35 modular-test-targets -refactor.md` — execute it as that card.
- [ ] Extras' third-party fetches (verovio, rtmidi, tinysoundfont, kissfft,
      the YDP soundfont) relocate from `cmake/FetchDependencies.cmake` into
      their module trees, so the public repo never fetches them and the
      extra tree is self-contained.
- [ ] Paper trail travels: extras' kanban cards, plans/ notes, and their
      docs/features.md sections migrate into the module directories.
- [ ] Boundary inventory for SatView/MegaCity mirroring ScoreView's (which
      is ready — five libraries, seam-only consumption of libs/): fold into
      pending `26 satview-library-boundaries` / `27 megacity-host-renderer-
      decomposition`.

## Phase 1 — the split (an afternoon once Phase 0 is green)

- [ ] Archive the untouched monorepo privately before anything else.
- [ ] `git filter-repo` a clone down to the extras paths (list historical
      locations so renames keep their history) → push as `draxul-extra`.
- [ ] Produce the public repo per the history decision above; install the
      pre-commit hook in both repos.
- [ ] Local sibling layout verified: combined configure/build/tests/smoke
      green from `~/dev/draxul` + `~/dev/draxul-extra`.
- [ ] Private-side pinning only: `draxul-extra` CI records (or submodules)
      the public-core SHA it was tested against. Never the reverse
      direction.

## Phase 2 — quality of life

- [ ] A small `both` helper script (status / branch / commit across the
      pair with matching messages). Resist heavier meta-repo tooling (west,
      git-repo) until real pain demands it.
- [ ] Extras CI: check out core at the pinned SHA + self, build combined,
      run the full suite; nightly canary against core `main` to surface
      seam drift early.
- [ ] Document the inter-repo contract: `libs/` public headers (IHost,
      renderer interfaces, NanoVG pass, host registry). Changes there
      require the paired-commit + canary discipline.

## Tests and acceptance

- [ ] Public-only build contains zero references to the extras: none of
      their FetchContent, no registration symbols, `--host score` fails
      with the availability error; unit suites + smoke green.
- [ ] Combined build is behaviorally identical to today's monorepo build:
      same hosts registered, all suites green, render scenarios pass.
- [ ] `draxul-extra` builds against a pinned public-core checkout on a
      clean machine, following a one-page setup doc.
- [ ] History end-state matches the owner's hygiene decision; the extras
      repo retains full file history.

## Dependencies and parallelism

Phase 0 absorbs or coordinates with pending 35 (modular test targets), 12
(host-provider availability), 26 (satview boundaries), and 27 (megacity
decomposition). Phase 1 runs only after Phase 0's two-configuration CI
matrix is green and the three owner decisions are made. Ongoing feature
work — composer #2 prep (22), the C-series tuning — continues unaffected:
Phase 0 touches build seams and file locations, never module internals.

<model>Claude Fable 5</model>
