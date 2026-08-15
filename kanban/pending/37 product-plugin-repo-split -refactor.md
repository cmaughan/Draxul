# Product plugin repo split

Move SatView, MegaCity, and ScoreView into their own GitHub repositories,
mounted back under `plugins/` as git submodules, so the Draxul repo focuses on
the terminal/agentic/host core and each product repo documents its own
facilities. Full plan, verified coupling inventory, and owner decision points in
[`plans/product-plugin-submodule-split.md`](../../plans/product-plugin-submodule-split.md).
Builds on the completed boundary work in
`kanban/pending/36 external-product-plugin-separation -refactor.md` and
supersedes the direction of
`kanban/ice-box/23 core-extra-repo-split -architecture.md`.

- [ ] Owner decisions: repo names/visibility, history model, kanban/plans
      relocation, `plugins/support/imgui` ownership.
- [ ] Phase 0: pre-split cleanup — move product test fixtures out of core
      `tests/support/`, drop `draxul-host`/`draxul-renderer` links from
      megacity/satview test CMake and extend the dependency guard to
      `TEST_TARGETS`, de-hardcode cross-tree fixture paths, add missing
      third-party declares, delete dead `vk_cube_pass`/`megacity_cube`/
      `assets/megacity`/`megacity_screenshot.sh`, fix `do.py` scoreview
      coverage target name, disambiguate ScoreView shadow headers.
- [ ] Phase 0 gate: card 36's 7E macOS runtime acceptance closed; card 36 to
      done.
- [ ] Phase 1: create and seed `draxul-satview`, `draxul-megacity`,
      `draxul-scoreview` repos with product docs, READMEs, and import-commit
      provenance.
- [ ] Phase 2: replace `plugins/{satview,megacity,scoreview}` with submodules;
      CI `submodules: recursive` plus a fail-if-enabled-but-absent guard;
      refresh README/CLAUDE.md/module-map/features docs; verify initialized,
      uninitialized, and disabled configure states.
- [ ] Phase 3: per-product CI building against pinned Draxul; re-run the 7E
      acceptance matrix from the submodule layout on Windows and macOS.
