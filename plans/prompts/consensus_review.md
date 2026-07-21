Read the latest feature-focused reviews in `plans/reviews/` (files matching `review-latest.*.md`) and replace `plans/reviews/review-consensus.md` with one feature consensus.

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

Create one implementation-ready markdown work item for each accepted proposal under `kanban/pending/`. Use an unused two-digit priority prefix and end each filename with `-feature.md`; do not overwrite or duplicate an existing card. Each card must contain scoped checkbox sections for investigation, implementation, cross-platform behavior, tests, documentation, and acceptance criteria. Mention safe sub-agent ownership only when the work has genuinely separable areas.

Append your `<model>` identifier to the consensus file and flag all interdependencies and recommended sequencing there. Return a brief execution summary as the command result, but put the full consensus in the required file.
