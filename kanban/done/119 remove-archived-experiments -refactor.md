# Remove archived experiments from the active product inventory

**Summary:** Preserve PCBView in its standalone repository while removing its Draxul submodule, build/test/CI/package integration and current references. Remove the archived BioView mode from MegaCity while retaining the City product and its shared infrastructure.

**Priority:** P1 — user-requested product scope cleanup

- [x] Preserve the PCBView checkout and the prior MegaCity implementation outside the active workspace.
- [x] Remove PCBView integration, test/render fixtures and product inventory references, including descendant CI workflows.
- [x] Adopt the MegaCity product cleanup and retain only City behavior.
- [x] Remove obsolete experiment documentation and tracker references; audit all initialized product repositories.
- [x] Run the core + MegaCity aggregate and same-cache smoke, check MegaCity-disabled configuration, and record platform limitations.

Requested 2026-10-09. The GitHub experiment repository must remain intact. Historical source remains recoverable in Git; active code and documentation should no longer advertise the experiments.

Preserved checkout: `/Users/cmaughan/dev/archived-experiments/draxul-pcbview` (origin remains the existing GitHub repository). MegaCity history bundle: `/Users/cmaughan/dev/archived-experiments/draxul-megacity-before-bio-removal-20261009.bundle`. Original product revision: `06c804c11cdda5de6e2c5d986fdbe33c1e85da1b`.

Implementation: the remaining City host owns one preferences file and UI layout; mode-dependent helpers, analysis controls, builders, exclusive ellipsoid/sphere/organic geometry and tests were removed. The shared semantic scanner, City layout/routing, Vulkan/Metal render passes and City preference/reload integration coverage remain.

CI: root checkout now initializes all five remaining public submodules recursively. MegaCity, SatView and ScoreView standalone CI no longer sets the retired product option and explicitly disables unmounted Flashcards. Windows native execution is unavailable on this Mac; backend sources and shared compile boundaries were inspected.

## Validation, 2026-10-09

Used the existing `build/` Release / Unix Makefiles cache throughout, retaining its configuration rather than switching build trees.

- MegaCity disabled: configure passed (11.51s), app build passed (41.27s). Then re-enabled MegaCity (configure 12.71s).
- `python3 do.py test release --megacity`: **60/60 CTest entries passed**, tests 27.39s, build + tests 52.39s (roughly 25s build/preflight). No focused filters were run before this aggregate.
- `python3 do.py smoke release --skip-build`: **passed**, isolated same-cache runtime and explicit owned-server cleanup.
- City golden comparison failed at 82.6172% both after cleanup (16.11s) and on a temporarily restored/rebuilt original MegaCity revision (16.36s). The actual BMPs were **byte-identical**, proving unchanged City output for this fixture. The inherited golden mismatch is transferred to `plugins/megacity/kanban/pending/18 city-render-reference-mismatch -test.md`; no reference was blessed.
- Restored the cleanup patch and rebuilt the app. Final `python3 do.py run release --console -- --smoke-test --server-runtime-dir <isolated-runtime>` **passed**, build/startup 5.34s. Its owned server acknowledged shutdown.
- Refreshed the dependency SVG; XML parses. Audited the root and all five initialized product repositories: no remaining experiment names in tracked source/docs/build/test inventories, apart from the explicit archive records.
- Windows native execution and remote CI were not run here. Both backend source paths remain aligned; CI covers the remaining platform execution.

Additional diagnostic/build costs (log spans; not combined with the aggregate):
- `off-configure`: 11.51s recorded log span (approximate).
- `off-build`: 41.27s recorded log span (approximate).
- `on-configure`: 12.71s recorded log span (approximate).
- `smoke`: 0.94s recorded log span (approximate).
- `baseline-build`: 25.05s recorded log span (approximate).
- `baseline-render`: 16.39s recorded log span (approximate).
- `final-build`: 15.97s recorded log span (approximate).
- `city-render`: 16.12s recorded log span (approximate).
- `deps`: 13.00s recorded log span (approximate).

Published owning-repository revisions: MegaCity `95ac2a6c44a4654c6b660ab0924c2a806830cfad`, SatView `c478b0a8045845d8b039423835d4f2bc876d2450`, ScoreView `c5c543a4054fa6fe317e9cfc12d509568a0bd4ec`. Product changes are adopted deliberately with this root cleanup. The initial removal left the archived PCBView repository unchanged.

Follow-up, 2026-10-09: at the user's request, removed all five PCBView pending cards and cleared their board ordering entries in the preserved standalone repository. Published tracker-only revision `88500d4`; experiment source and prior card history remain intact. Audited the root, all five initialized product repositories and the standalone PCBView board: no pending or deferred cards belong to the retired experiments. The City render-reference follow-up remains active because it concerns the retained product. Board audit and staged whitespace checks passed; no builds or runtime tests were needed for these tracker-only deletions.
