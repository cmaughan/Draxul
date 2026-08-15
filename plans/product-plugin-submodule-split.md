# Product plugin repo split — SatView, MegaCity, ScoreView as submodules

**Date:** 2026-08-15 · **Card:** kanban `37 product-plugin-repo-split -refactor.md`

## Goal

Move the three product plugins into their own GitHub repositories and mount them
back into Draxul as git submodules under `plugins/`:

```
github.com/cmaughan/Draxul            the terminal / agentic / host core
github.com/cmaughan/draxul-satview    → submodule at plugins/satview
github.com/cmaughan/draxul-megacity   → submodule at plugins/megacity
github.com/cmaughan/draxul-scoreview  → submodule at plugins/scoreview
```

The Draxul repo and README refocus on what the core actually is — terminal and
agent runtimes, shell/Neovim hosts, topology, plugin lifecycle, Kanban/Markdown —
while each product repo gets its own README describing its specific facilities
(satellite visualization, code-city analysis, piano learning).

## Relationship to prior decisions

- `plans/external-product-plugin-separation.md` (kanban card 36) did the hard
  part. Slice 7 made every product mount an overridable directory
  (`DRAXUL_<P>_PLUGIN_DIR`, root `CMakeLists.txt:37-42`), made an enabled but
  absent checkout a supported configure state (`CMakeLists.txt:179-192`), and
  proved via the generated CMake graph that no core target reaches a product
  target. This plan is the git-logistics half that Slice 7 deliberately
  prepared for.
- `plans/repo-split-core-extras.md` (2026-07-18, ice-box card
  `23 core-extra-repo-split -architecture.md`) rejected submodules — but its
  premises predate the plugin cutover and no longer hold. Then, products were
  C++ `modules/` sharing IHost/renderer interfaces, so cross-cutting changes
  were constant and the "runtime plugins" alternative was dismissed as C++ ABI
  pain. Since then that alternative was *built*: products cross only the
  versioned C ABI plus an allowlisted support set, enforced at configure time,
  so the constant pointer-bump tax that motivated the rejection is gone by
  construction. Two of that note's points survive into this plan: a public
  repo must not reference a private submodule (visibility decision below), and
  submodule pointer hygiene needs explicit workflow rules (see "Day-to-day
  workflow"). The ice-box card stays as a record; this plan supersedes its
  direction.

## What is already true (verified 2026-08-15, post-merge main)

- Root CMake never names a product target. Discovery is explicit mount +
  inversion of control: root `add_subdirectory()`s each existing
  `DRAXUL_<P>_PLUGIN_DIR`; the plugin calls `draxul_register_bundled_plugin()`
  (`cmake/DraxulPlugins.cmake:41`), and root consumes staging/tests through the
  registered properties only.
- An enabled-but-missing mount degrades to a STATUS message, exactly what an
  uninitialized submodule looks like.
- Dependency direction is enforced: products may link only their own targets,
  third-party code, `Draxul::PluginSDK`, and `Draxul::PluginSupport::*`
  (allowlist at root `CMakeLists.txt:212-228`; checks in
  `cmake/DraxulPlugins.cmake` and `cmake/CheckDependencyBoundaries.cmake`).
- All three plugin trees contain their own sources, shaders, assets, tests, and
  dependency fetching, with zero `../../` path escapes. ScoreView additionally
  builds standalone against the installed SDK and has a working copied-tree
  extraction smoke (`plugins/scoreview/tests/external_product_plugin_smoke.py`).
- Sizes: satview 46 MB, megacity 37 MB, scoreview 2 MB. Largest single file
  11 MB. Plain git is fine; no LFS needed.

## Owner decisions (flagged, not assumed)

- [ ] **Repo names and visibility.** Suggested: `draxul-satview`,
      `draxul-megacity`, `draxul-scoreview`, all same visibility as Draxul
      itself. A public repo must not reference private submodules — the
      pointer 404s for everyone else and CI can't fetch it. If any product
      should stay private, that product must instead stay an optional local
      mount (the `DRAXUL_<P>_PLUGIN_DIR` override already supports this) and
      not become a committed submodule.
- [ ] **History.** Recommended: fresh-start product repos whose initial commit
      records the Draxul SHA they were exported from. Only 5–9 commits touch
      each plugin at its current path — the deep product history lives under
      old `modules/` paths and stays fully greppable in Draxul. A
      `git filter-repo` extraction with rename-chasing across the migration is
      possible but buys little for the ceremony.
- [ ] **Where product kanban cards and plans live.** Recommended: migration
      records (cards 36/37, separation plans, reviews) stay in Draxul;
      product roadmap items (e.g. `05 megacity-model-layout-routing-library`,
      `06 satview-scene-composer-host-split`,
      `07/18 scoreview-*`, `plans/scoreview-manifesto.md`, `plans/scoreview-*.md`)
      move to their product repos so each repo's README/kanban tells that
      product's story.
- [ ] **`plugins/support/imgui/` ownership.** All three products hard-depend on
      it. Recommended: it stays in Draxul as part of the support surface (it is
      generic, product-free code and the allowlist already names it
      `Draxul::PluginSupport::ImGui`). Product-repo CI consumes it from the
      Draxul checkout it builds against; ScoreView's standalone path already
      copies it explicitly.

## Phase 0 — pre-split cleanup (inside Draxul, before any repo exists)

Everything here is ordinary in-repo work and makes the split mechanical. Items
1–3 are the real couplings; the rest is hygiene found while surveying.

1. **Move product test fixtures out of core `tests/support/`.**
   `tests/support/satview_host_fixture.h` includes `draxul/satview/*` headers
   and is the declared `friend` of a plugin-internal class — it belongs in
   `plugins/satview/tests/`. Same move for
   `tests/support/megacity_scene_test_support.h` (includes
   `draxul/megacity_host.h`). After the move, core `tests/` must not include
   any product header.
2. **Stop plugin test CMake linking broad core.**
   `plugins/megacity/cmake/Tests.cmake:11-12` and
   `plugins/satview/cmake/Tests.cmake:12-13` link `draxul-host` and
   `draxul-renderer`, bypassing the allowlist (the boundary check only inspects
   `PRODUCT_TARGETS`). Port those tests to support targets as ScoreView's test
   CMake already does, and extend `draxul_check_registered_plugin_dependencies`
   to cover `TEST_TARGETS` so the seam cannot silently reopen.
3. **Remove cross-tree fixture paths.** `tests/render/scoreview-plugin.toml:4`
   and `tests/topology_cli_integration.ps1:256` hardcode
   `plugins/scoreview/tests/fixtures/musicxml/…`. Let the registration API
   expose (or stage) plugin-owned fixtures so core test drivers reference the
   staged location, not the source tree.
4. **Make megacity/satview declare their shared third-party deps.** Both consume
   root-fetched `stb`, `imgui`, `SDL3`, `nlohmann_json` variables/targets
   without declaring them (e.g. `plugins/satview/CMakeLists.txt:39,70,88`).
   Mirror ScoreView's defensive re-declare pattern so a product tree configures
   against a Draxul checkout without ordering luck.
5. **Delete dead product residue in core:**
   `libs/draxul-renderer/src/vulkan/vk_cube_pass.{h,cpp}` (zero callers; loads
   `megacity_cube.*` shaders at runtime), `plugins/megacity/shaders/megacity_cube.*`,
   the dead root `assets/megacity/` directory, and
   `scripts/megacity_screenshot.sh` (already broken — `--host megacity` no
   longer parses). Fix the stale `CubeRenderPass` mention in `CLAUDE.md`.
6. **Fix `do.py:1201`:** coverage export names `draxul-test-scoreview-host`,
   which no longer exists (`draxul-test-scoreview-runtime` is real); today the
   `.is_file()` filter silently drops ScoreView runtime coverage.
7. **Disambiguate ScoreView's shadow headers.** ScoreView ships its own copies
   of `draxul/host.h`, `imgui_host.h`, etc. under
   `product/draxul-score-runtime-support/include/draxul/` while some of its
   sources also include core `<draxul/host.h>` / `<draxul/nanovg_pass.h>`
   (`score_runtime.h:3-4`); include-path order currently decides which wins.
   Pick one spelling per header before the tree moves repos.
8. **Close out card 36's last tick** (7E macOS runtime acceptance) so the split
   starts from a fully accepted baseline.

## Phase 1 — create and seed the product repos

Per product (satview shown; the other two are identical in shape):

1. `gh repo create cmaughan/draxul-satview` (visibility per the decision above).
2. Seed the repo root with the current contents of `plugins/satview/` — the
   plugin's `CMakeLists.txt` becomes the repo root CMakeLists, which is exactly
   what the mount contract expects (`${DRAXUL_SATVIEW_PLUGIN_DIR}/CMakeLists.txt`).
3. Move product docs in with it: `docs/features/satview.md` and
   `docs/satview/data-sources.md` (whose ~30 relative
   `../../plugins/satview/...` links become correct in-repo paths after the
   move). ScoreView takes `docs/features/scoreview.md`. MegaCity has no doc
   file yet — its README covers it.
4. Write the product README: what it is, screenshots, its facilities, how it
   mounts into Draxul (submodule + `DRAXUL_ENABLE_<P>`), its configuration
   surface, third-party/data attribution (satview's data-sources doc),
   and — for ScoreView — the standalone SDK build.
5. Move the product's roadmap cards/plans from Draxul's `kanban/` and `plans/`
   (per the owner decision above).
6. Initial commit: `Import SatView from cmaughan/Draxul@<sha>` so archaeology
   has a stable pointer back into the monorepo history.

## Phase 2 — cut Draxul over to submodules

One commit (or one per product if bisectability is preferred):

1. `git rm -r plugins/satview plugins/megacity plugins/scoreview`, then
   `git submodule add <url> plugins/<name>` for each — same paths, so the
   mount defaults, docs, and muscle memory all keep working.
   `plugins/spinning-triangle` and `plugins/support/` remain ordinary Draxul
   directories (the triangle is the in-repo reference plugin and ABI test
   vehicle).
2. Verify the two supported local states: submodules initialized (full build)
   and not initialized (core-only build with three STATUS skips) — plus
   `-DDRAXUL_ENABLE_<P>=OFF` still excluding an initialized checkout.
3. **CI:** add `submodules: recursive` to both `actions/checkout` steps in
   `.github/workflows/build.yml` (`:21`, `:75`). Then add a guard step that
   fails the job if any `DRAXUL_ENABLE_<P>` is ON while `DRAXUL_HAVE_<P>` came
   out OFF — the graceful local skip is exactly wrong for CI, where it would
   silently drop every product test while staying green. Emitting the
   `DRAXUL_HAVE_*` values to a configure log the workflow greps is enough.
4. **Docs refresh in Draxul:** README (refocus on terminal/agentic/host core;
   product sections become short links to the product repos), `CLAUDE.md`
   (`plugins/` section → submodule wording, clone/update commands),
   `docs/module-map.md` (mount table gains repo URLs), `docs/features.md`
   (product deep-dives shrink to pointers; the option/mount table at `:830`
   stays), `FEATURES.md:9-10` links → product repos,
   `.agents/skills/draxul/SKILL.md` untouched (plugin IDs and launch JSON stay
   valid). Note in CLAUDE.md that `git clone --recurse-submodules` (or
   `git submodule update --init` after clone) is the expected setup.
5. Move card 37 through `kanban/` as slices land; card 36 moves to done once
   7E closes.

## Phase 3 — product-repo CI and acceptance

1. Each product repo gets a workflow that proves it builds and passes its test
   shard against Draxul main: check out Draxul at a pinned ref, check out
   itself, configure with only its own product enabled and
   `-DDRAXUL_<P>_PLUGIN_DIR=<its checkout>`, build, run its focused tests.
   This is the reverse-pin direction the 2026-07 note already blessed: the
   product pins core, never the other way beyond the submodule pointer.
2. ScoreView keeps its extraction smoke as the extra portability check;
   extending that pattern to satview/megacity stays optional (card 36 already
   demoted it from a requirement).
3. Re-run the Slice 7E acceptance matrix from the submodule layout on Windows
   and macOS: no products / each alone / all together, link-graph audit,
   CLI pane creation for all four product views, render snapshots.

## Day-to-day workflow after the split

- Fresh clone: `git clone --recurse-submodules`; after pulling Draxul:
  `git submodule update --init --recursive` (a `do.py` helper or git alias can
  wrap this).
- A product-only change: commit + push in the product repo, then one
  pointer-bump commit in Draxul when Draxul should adopt it. Draxul CI always
  builds the pinned SHA, so main never turns red because a product repo moved.
- A seam change (SDK / support allowlist): land the Draxul side first (the
  product repos' CI pins Draxul, so nothing breaks), then update products and
  bump pointers. The configure-time boundary checks make accidental seam
  widening a hard error rather than a review judgement.
- `git status` showing a dirty submodule pointer means "product checkout moved
  but Draxul hasn't adopted it" — commit it deliberately or
  `git submodule update` to snap back; never commit a pointer bump as a
  drive-by in an unrelated change.

## Risks

- **Green-but-empty CI** is the one severe failure mode (Phase 2 step 3 guard
  exists precisely for it).
- Fixture-path and test-support coupling (Phase 0 items 1–3) would otherwise
  turn into cross-repo breakage discovered only at CI time.
- Submodule friction is real but now bounded: the boundary work means pointer
  bumps happen at product-release cadence, not per-change.
- History in product repos starts shallow; the import-commit SHA pointer and
  the untouched monorepo history are the mitigation.
