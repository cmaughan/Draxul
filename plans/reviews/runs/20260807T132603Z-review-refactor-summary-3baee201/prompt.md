Read the latest refactor-focused reviews in `plans/reviews/` (files matching `review-refactor-latest.*.md`) and replace `plans/reviews/review-refactor-consensus.md` with one refactoring consensus.

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

Create one implementation-ready markdown work item for each accepted proposal under `kanban/pending/`. Use an unused two-digit priority prefix and end each filename with `-refactor.md`; do not overwrite or duplicate an existing card. Each card must have scoped checkbox sections for boundary verification, implementation/migration, unit tests, cross-platform validation, agent documentation/tooling where relevant, and acceptance criteria.

Append your `<model>` identifier to the consensus file and clearly state recommended sequencing and interdependencies. Return a brief execution summary as the command result, but put the full consensus in the required file.
