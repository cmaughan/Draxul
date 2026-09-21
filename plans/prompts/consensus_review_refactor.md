Read exactly the refactor-focused review reports listed in `INPUT_REVIEWS/index.md` and return one complete consensus as your final Markdown response. The trusted runner publishes the consensus and any enabled Kanban cards; do not write files or substitute mutable latest-file globs.

Map the relevant architecture and compare the reports' actual coverage before reconciling findings. Use up to four read-only subagents for independent verification of coherent groups of findings when useful, with no nested delegation; use the same model and high effort for Codex/Claude. Wait for all workers, re-read their evidence before accepting it, and check cross-component implications. If delegation is unavailable, verify sequentially. Reconcile against current source, documentation and root/product tracker lanes. Distinguish confirmed findings, rejected claims, already-tracked work, and unresolved leads. State verification gaps and do not turn unresolved leads into tasks. Report every distinct accepted finding without a numeric quota or an arbitrary output cap.

This is **refactor planning only**. Do not pull findings from feature or bug review files.

1. Deduplicate overlapping structural proposals and credit the agents that raised each one.
2. Verify every proposal against current source, CMake targets, include/link relationships, tests, root and nested `AGENTS.md` files, scripts, docs, and all Kanban lanes.
3. Drop stale, already tracked, purely stylistic, speculative, or rewrite-scale suggestions, with a short reason.
4. Reconcile disagreements about module boundaries, interface ownership, static-library granularity, sequencing, and cross-platform risk.
5. Prefer incremental boundaries that improve test isolation and agent ownership while keeping the tree buildable at every step.

For each accepted refactor, include:

- Concrete current-tree evidence: files, symbols, targets, and relevant dependencies.
- Priority: P0, P1, or P2, with a brief leverage/risk rationale.
- The agreed target boundary and responsibility.
- Intended public interface, callers, dependencies, and private implementation.
- An incremental migration sequence.
- Focused tests and narrow build/test commands the boundary should enable.
- Windows/macOS and Vulkan/Metal parity considerations.
- Which agent or agents proposed it.
- Dependencies on other accepted refactors.
- A clean ownership suggestion for one agent or safely independent sub-agents.

Include a concise final target/module map showing proposed static libraries and dependency direction. Add a separate repository-guidance section covering root/subdirectory `AGENTS.md` improvements and any agent-oriented tooling or validation entry points.

Return one complete implementation-ready markdown work item for the runner to create for each accepted proposal under `kanban/pending/`. Use an unused two-digit priority prefix and end each filename with `-refactor.md`; do not overwrite or duplicate an existing card. Each card must have scoped checkbox sections for boundary verification, implementation/migration, unit tests, cross-platform validation, agent documentation/tooling where relevant, and acceptance criteria.

Append your `<model>` identifier to the consensus file and clearly state recommended sequencing and interdependencies. Return the full consensus, including complete proposed cards under exact `### kanban/pending/<filename>.md` headings when task creation is enabled.
