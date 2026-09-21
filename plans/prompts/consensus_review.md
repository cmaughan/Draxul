Read exactly the feature-focused review reports listed in `INPUT_REVIEWS/index.md` and return one complete consensus as your final Markdown response. The trusted runner publishes the consensus and any enabled Kanban cards; do not write files or substitute mutable latest-file globs.

Map the relevant architecture and compare the reports' actual coverage before reconciling findings. Use up to four read-only subagents for independent verification of coherent groups of findings when useful, with no nested delegation; use the same model and high effort for Codex/Claude. Wait for all workers, re-read their evidence before accepting it, and check cross-component implications. If delegation is unavailable, verify sequentially. Reconcile against current source, documentation and root/product tracker lanes. Distinguish confirmed findings, rejected claims, already-tracked work, and unresolved leads. State verification gaps and do not turn unresolved leads into tasks. Report every distinct accepted finding without a numeric quota or an arbitrary output cap.

This is **feature planning only**. Do not pull findings from bug or refactor review files.

1. Deduplicate overlapping feature proposals and credit the agents that raised each one.
2. Verify each proposal against the current source, `docs/features.md`, detailed feature docs, and every Kanban lane. Drop implemented, pending, completed, or intentionally iced ideas, noting the reason briefly.
3. Reconcile disagreements about user value, scope, sequencing, or platform feasibility and give a clear ruling.
4. Rank confirmed proposals by user value, strategic fit, implementation cost, and cross-platform risk.

For each confirmed feature, include:

- The user problem and proposed behavior.
- Current-tree evidence that the feature is not already implemented or tracked.
- Which agent or agents proposed it.
- Likely implementation areas and Windows/macOS considerations.
- Dependencies on other confirmed work.
- A concise acceptance signal.

Return one complete implementation-ready markdown work item for the runner to create for each accepted proposal under `kanban/pending/`. Use an unused two-digit priority prefix and end each filename with `-feature.md`; do not overwrite or duplicate an existing card. Each card must contain scoped checkbox sections for investigation, implementation, cross-platform behavior, tests, documentation, and acceptance criteria. Mention safe sub-agent ownership only when the work has genuinely separable areas.

Append your `<model>` identifier to the consensus file and flag all interdependencies and recommended sequencing there. Return the full consensus, including complete proposed cards under exact `### kanban/pending/<filename>.md` headings when task creation is enabled.
