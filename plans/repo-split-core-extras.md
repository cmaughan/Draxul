# Core / extras repo split — decision note

**Date:** 2026-07-18 · **Card:** kanban `23 core-extra-repo-split -architecture.md`
**Question:** the public GitHub should become the Draxul terminal; a second
repo should hold the extras (ScoreView, SatView, MegaCity), combined locally
so day-to-day work still feels like one big project. Submodules? Subtrees?

## Decision

**Sibling-checkout overlay.** Two completely normal repos, side by side on
disk, joined by one CMake variable:

```
~/dev/draxul/          public: app, libs, markdown, kanban, shaders, core tests
~/dev/draxul-extra/    private: modules/{score,satview,megacity} + their
                       tests, fixtures, plans, kanban cards
```

The public root CMake gains `DRAXUL_EXTRA_DIR` (defaulting to
`../draxul-extra` when it exists) and conditionally `add_subdirectory()`s it;
the per-module `#ifdef` registration blocks in `app/main.cpp` collapse into
one `register_extra_host_providers()` hook the extra tree provides (no-op
when absent). One build directory spans both trees, so clangd, ctest, smoke,
and cross-cutting work feel exactly like today. The only tax: a change
spanning both trees is two commits instead of one.

The starting point is favorable: the extras are already behind
`DRAXUL_ENABLE_*` gates, their heavy dependencies (Verovio etc.) fetch
conditionally, and `main.cpp` is documented as the sole executable
touchpoint. This is a build-seam and git-logistics change, not an
architecture change.

## Options rejected

- **Git submodules (extras as a submodule of the public repo).** The public
  repo cannot reference a private repo — the pointer 404s for everyone else.
  And the daily workflow here is constantly cross-cutting; submodules tax
  every such change with commit + commit + pointer-bump, plus dirty-pointer
  noise, bisect pain, and branch coordination. The one acceptable submodule
  direction is the reverse — the *private* extras repo may pin the public
  core as a submodule for its CI — because that pointer never leaks and the
  friction lands only on the private side. Even there, a recorded SHA in CI
  is enough.
- **Git subtree.** Subtree extracts/merges a *prefix directory*. The
  terminal is not a prefix — it is the root minus `modules/{score,satview,
  megacity}`. Multi-prefix subtree splits (and pushing history back out
  through them) are exactly the git ceremony that goes wrong quietly.
- **Runtime plugins (dlopen hosts).** Solves distribution separation, not
  repo separation, and buys C++ ABI pain (ImGui contexts, renderer
  interfaces across DSOs). The compile-time registry + gates already give
  the pluggability needed. Out of scope.

## Recorded fallback: the filtered mirror

Keep one private monorepo as the source of truth (today's life, unchanged)
and have CI generate the public repo with `git filter-repo` path
allowlisting. Zero local friction forever; the cost is a public repo that is
effectively a read-only artifact (external PRs need hand-porting). **The
Phase 0 preparation below serves both models identically**, so the overlay
vs mirror choice stays reversible until the split itself: if the two-commit
tax turns out to grate, switch to the mirror without redoing anything.

## Consequences

- The inter-repo contract becomes the `libs/` public headers (IHost, the
  renderer interfaces, the NanoVG pass, the host registry). Changes there
  ripple to the other repo — paired commits plus a nightly extras-vs-core
  canary keep the seam honest. ScoreView already consumes only that seam
  (the kanban 20/21/22 decomposition made it the model citizen); SatView and
  MegaCity get the same boundary inventory via their existing cards (26/27).
- One extras repo, not one per module: per-module repos multiply
  coordination at solo scale for no benefit. The module directories inside
  `draxul-extra` remain as independent as today, and any one of them can be
  split out later with the same filter-repo move.
- The public history-hygiene choice (filter the extras out of the public
  repo's past vs delete-going-forward) is the one irreversible decision, and
  it is only cheap while the repo is still private. It is flagged on the
  card as the owner's call.
