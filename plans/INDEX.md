# Plans index

A status snapshot of everything under `plans/`, so a reader can tell a shipped design
from a live one without opening all of them. See [README.md](README.md) for what `plans/`
is (design docs, research, and review output — **not** work items; those live in
[`kanban/`](../kanban/README.md)).

> **This is an inferred snapshot, not authoritative state.** Statuses were assigned on
> 2026-07-19 by skimming each doc against the tree at this commit (and against the canonical
> feature inventory in [`../docs/features.md`](../docs/features.md)). The plans themselves are
> unchanged. When a plan ships, tick it in the plan and move its kanban item — and, ideally,
> update the row here. Where the tree left real doubt, the row is marked **research** rather
> than guessed.

## Status legend

| Status | Meaning |
|--------|---------|
| **active** | Still directing current or future work — master plans, the manifesto/north-star, forward "go" plans, deferred future options, and multi-phase plans only partly shipped. |
| **implemented** | The design has shipped; kept for provenance. Safe to treat as reference, not a to-do. |
| **superseded** | Explicitly replaced by a newer plan (linked). |
| **research** | Evidence notes, audits, architecture analyses, and reference material that inform design but are not themselves an actionable build plan — plus genuinely status-uncertain docs. |

## Top-level plans

| Plan | Status | Notes |
|------|--------|-------|
| [server-client-terminal-runtime.md](server-client-terminal-runtime.md) | active | Comprehensive vertical-slice plan for one server-owned terminal/agent runtime, reconnectable GPU clients, deterministic two-client reflection, optional tray/menu-bar status, and a future SSH bridge. |
| [herdr-agent-harness-research.md](herdr-agent-harness-research.md) | active | Herdr concept research and phased local agent-harness direction; vocabulary, TabController, the in-memory multi-Space lifecycle, app actions, and Spaces rail are implemented. Multi-Space persistence is refined in the focused plan below. |
| [multi-space-session-persistence.md](multi-space-session-persistence.md) | active | Source-backed v2 plan: save and restore every loaded Space, retain TOML, use transactional partial restore, and derive the Agents list from pane-owned identity. |
| [app-shell-layout.md](app-shell-layout.md) | implemented | One authoritative root shell layout, a draggable persisted Spaces/sidebar splitter, and Chrome ownership cleanup without putting app chrome into per-tab pane trees. |
| [music-notation-research.md](music-notation-research.md) | research | State-of-the-art notes feeding ScoreView; self-labelled research. |
| [scoreview.md](scoreview.md) | active | ScoreView master plan; rendering foundation shipped, anchors future phases toward the manifesto. |
| [scoreview-manifesto.md](scoreview-manifesto.md) | active | North-star vision (the adaptive piano-learning runner). Destination, not a milestone. |
| [scoreview-conveyor.md](scoreview-conveyor.md) | implemented | Milestone 1 — flow/conveyor view. Shipped (see features "Conveyor (flow) mode"). |
| [scoreview-gate.md](scoreview-gate.md) | implemented | Milestone 2 — wait-gate + adaptive tempo. Shipped (kept for bots/tests). |
| [scoreview-ear.md](scoreview-ear.md) | implemented | Milestone 3 — acoustic listener (`NoteListener`). Shipped with unit coverage. |
| [scoreview-runner.md](scoreview-runner.md) | implemented | Milestone 4 — Roll mode ("guitar hero for piano"). Shipped as the default game. |
| [scoreview-stream.md](scoreview-stream.md) | implemented | Milestone 5 — rolling window + player memory (S0–S4). Shipped. |
| [scoreview-composer.md](scoreview-composer.md) | active | Science-backed composer go-plan; extends the shipped S0–S4 with further direction. |
| [scoreview-composition-model.md](scoreview-composition-model.md) | active | Decision note: semantic composition is the destination; string fabrication is the guarded interim. |
| [scoreview-learning-research.md](scoreview-learning-research.md) | research | Two-pass learning-science evidence review behind the composer. |
| [satview.md](satview.md) | active | SatView master plan; core (globe/day-night/tracks/live positions) shipped, anchors the module. |
| [satview-catalog-population-split.md](satview-catalog-population-split.md) | implemented | Active/inactive/rocket/debris/unknown split — shipped (population filtering). |
| [satview-ground-sky-projections.md](satview-ground-sky-projections.md) | implemented | Stereographic/perspective ground sky — shipped. |
| [satview-hdr-pipeline.md](satview-hdr-pipeline.md) | implemented | HDR/MSAA/ACES scene pipeline — shipped. |
| [satview-lunar-orbit-catalog.md](satview-lunar-orbit-catalog.md) | implemented | Moon-centered catalog + ephemeris fidelity tiers — core shipped (lunar ephemeris). |
| [satview-lunar-surface-artifacts.md](satview-lunar-surface-artifacts.md) | implemented | Lunar surface markers — shipped (70 LROC-confirmed objects). |
| [satview-observatory-horizon-constellation-boundaries.md](satview-observatory-horizon-constellation-boundaries.md) | implemented | Observatory silhouette + constellation boundaries/labels — shipped. |
| [satview-threaded-refresh.md](satview-threaded-refresh.md) | implemented | Off-loop refresh + `--continuous-refresh` — shipped (flag present in host code). |
| [satview-solar-system-data-expansion.md](satview-solar-system-data-expansion.md) | active | Phase 1 (Sun/planet/moon POVs) shipped; later phases explicitly future work. |
| [satview-bayer-star-labels.md](satview-bayer-star-labels.md) | active | Self-labelled **Deferred** — a scoped future overlay, not yet built. |
| [satview-earth-moon-rotating-frame.md](satview-earth-moon-rotating-frame.md) | active | Self-labelled **future visualization option**; inertial frame remains the default. |
| [satview-lunar-data-source-audit.md](satview-lunar-data-source-audit.md) | research | Completed data-source audit; drove two disposition corrections + ARTEMIS ingestion. |

## `design/`

| Plan | Status | Notes |
|------|--------|-------|
| [design/draxul_geometry.md](design/draxul_geometry.md) | implemented | `draxul-geometry` + `DraxulTree` mesh generator — shipped. |
| [design/shared-code-visualization-renderer-module-split-plan.md](design/shared-code-visualization-renderer-module-split-plan.md) | implemented | Shared neutral scene + separate city/biology builders — shipped (MegaCity + BioView). |
| [design/rendering_todo.md](design/rendering_todo.md) | active | Open rendering cleanups (e.g. move final present to a true sRGB target). |
| [design/semantic-code-visualization-separation-plan.md](design/semantic-code-visualization-separation-plan.md) | superseded | Its own refinement note redirects to `shared-code-visualization-renderer-module-split-plan.md`. |
| [design/renderers.md](design/renderers.md) | research | 3D-architecture analysis/reference; reflects the current two-tier renderer + `IFrameContext`. |
| [design/rendering_efficiency.md](design/rendering_efficiency.md) | research | Evergreen efficiency principles + external references (last updated 2026-03-24). |
| [design/draxul_megacity_isometric_plan.md](design/draxul_megacity_isometric_plan.md) | research | Early isometric-scene exploration; the semantic-city direction went elsewhere — status genuinely uncertain, not confirmed superseded. |
| design/megacity-current-architecture.svg | research | Rendered architecture diagram asset (reference). |

## `superpowers/` (agent implementation plans)

Dated, checkbox-driven implementation plans produced during agentic execution. Most describe
work that has since shipped; kept for provenance.

| Plan | Status | Notes |
|------|--------|-------|
| [superpowers/2026-06-22-markdown-kanban-modules-plan.md](superpowers/2026-06-22-markdown-kanban-modules-plan.md) | implemented | Markdown/Kanban moved to `modules/` — shipped. |
| [superpowers/2026-06-22-megacity-treesitter-only-module-boundaries-plan.md](superpowers/2026-06-22-megacity-treesitter-only-module-boundaries-plan.md) | implemented | Tree-sitter-only MegaCity source — shipped. |
| [superpowers/2026-06-23-megacity-mesh-upload-path-plan.md](superpowers/2026-06-23-megacity-mesh-upload-path-plan.md) | implemented | Reusable static mesh family cache — shipped. |
| [superpowers/2026-07-13-do-py-clean-design.md](superpowers/2026-07-13-do-py-clean-design.md) | implemented | `do.py clean` — shipped (`cmd_clean` present). |
| [superpowers/2026-07-13-do-py-clean-implementation.md](superpowers/2026-07-13-do-py-clean-implementation.md) | implemented | Implementation plan for the above — shipped. |
| [superpowers/2026-07-13-fast-unit-tests-design.md](superpowers/2026-07-13-fast-unit-tests-design.md) | implemented | Four CTest shards + test PCH — shipped. |
| [superpowers/2026-07-13-fast-unit-tests-implementation.md](superpowers/2026-07-13-fast-unit-tests-implementation.md) | implemented | Implementation plan for the above — shipped. |
| [superpowers/plans/2026-05-19-markdown-gpu-draw-list-renderer.md](superpowers/plans/2026-05-19-markdown-gpu-draw-list-renderer.md) | implemented | Native GPU draw-list markdown renderer — shipped. |
| [superpowers/plans/2026-05-19-markdown-pipe-tables.md](superpowers/plans/2026-05-19-markdown-pipe-tables.md) | implemented | Markdown pipe tables — shipped. |
| [superpowers/plans/2026-05-19-markdown-viewer-host.md](superpowers/plans/2026-05-19-markdown-viewer-host.md) | implemented | `--host markdown` viewer — shipped. |
| [superpowers/plans/2026-05-20-kanban-viewer-host.md](superpowers/plans/2026-05-20-kanban-viewer-host.md) | implemented | `--host kanban` viewer — shipped. |
| [superpowers/plans/2026-06-15-kanban-pending-batch.md](superpowers/plans/2026-06-15-kanban-pending-batch.md) | implemented | One-off batch that closed small pending cards; consumed. |
| [superpowers/plans/2026-06-17-skip-citydb-for-treesitter-semantic-city.md](superpowers/plans/2026-06-17-skip-citydb-for-treesitter-semantic-city.md) | implemented | Build the city straight from the Tree-sitter snapshot — shipped. |
| [superpowers/plans/2026-06-22-command-palette-save-session-as.md](superpowers/plans/2026-06-22-command-palette-save-session-as.md) | implemented | Palette "save session as" — shipped. |
| [superpowers/plans/2026-07-07-satview-planet-display-implementation.md](superpowers/plans/2026-07-07-satview-planet-display-implementation.md) | implemented | Saturn rings, moon bodies, Sun-view planet tracks — shipped. |
| [superpowers/specs/2026-07-07-satview-planet-display-design.md](superpowers/specs/2026-07-07-satview-planet-display-design.md) | implemented | Design spec for the planet-display work above — shipped. |
| [superpowers/plans/2026-06-16-graphify-semantic-city-source.md](superpowers/plans/2026-06-16-graphify-semantic-city-source.md) | superseded | The Graphify source was removed by the Tree-sitter-only direction. |
| [superpowers/plans/2026-07-08-satview-mars-orbit-ephemeris.md](superpowers/plans/2026-07-08-satview-mars-orbit-ephemeris.md) | active | Mars orbiters still render catalog-only; the ephemeris import is not yet shipped. |

## Subdirectories (categorical)

| Path | Status | Notes |
|------|--------|-------|
| [prompts/](prompts/) | active | Reusable prompt templates (consensus/review/architecture-diagram); wired into the workflow — e.g. CLAUDE.md's `come to consensus` runs `prompts/consensus_review.md`. |
| [reviews/](reviews/) | research | Point-in-time multi-model review outputs and consensus snapshots. Historical evidence — do not treat as current state. |
