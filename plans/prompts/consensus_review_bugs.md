Read exactly the bug-focused review reports listed in `INPUT_REVIEWS/index.md` and return one complete consensus as your final Markdown response. The trusted runner publishes the consensus and any enabled Kanban cards; do not write files or substitute mutable latest-file globs.

Map the relevant architecture and compare the reports' actual coverage before reconciling findings. Use up to four read-only subagents for independent verification of coherent groups of findings when useful, with no nested delegation; use the same model and high effort for Codex/Claude. Wait for all workers, re-read their evidence before accepting it, and check cross-component implications. If delegation is unavailable, verify sequentially. Reconcile against current source, documentation and root/product tracker lanes. Distinguish confirmed findings, rejected claims, already-tracked work, and unresolved leads. State verification gaps and do not turn unresolved leads into tasks. Report every distinct accepted finding without a numeric quota or an arbitrary output cap.

This is a **bug triage**, not a general review. Your job:

1. **Deduplicate**: Multiple agents often find the same bug with different descriptions. Merge them into one entry, crediting which agents spotted it.
2. **Verify**: For each reported bug, read the actual source file and line cited. Confirm the bug is real and still present. Drop false positives with a brief note explaining why.
3. **Severity-rank**: Order confirmed bugs strictly by severity — CRITICAL (crash/UB/data corruption), HIGH (incorrect behavior), MEDIUM (edge-case failure).
4. **Triage conflicts**: Where agents disagree on severity or whether something is a bug, state both positions and your ruling with reasoning.

For each confirmed bug, the consensus entry should include:
- File path and line number (verified against current source)
- Severity rating
- One-sentence description of the defect
- Concrete trigger scenario
- Suggested fix
- Which agent(s) reported it

Then extract bug-fix work items from the confirmed bugs. Put core and shared-boundary cards under exact `### kanban/pending/<filename>.md` headings. Put defects wholly owned by an initialized product submodule under exact `### plugins/<product>/kanban/pending/<filename>.md` headings, using `megacity`, `satview`, `scoreview`, `pcbview`, or `rezonality`. Include a `**Source:**` line with a backtick-quoted owning source path in each card. Use an available two-digit priority in each owning lane, ranking higher-severity bugs first within that lane. Each work item should contain:
- The bug description and trigger scenario
- Investigation steps (checkboxes)
- Fix strategy (checkboxes)
- Acceptance criteria (checkboxes)

Do NOT create work items for issues already in any root or product `kanban/pending/`, `kanban/ice-box/`, or `kanban/done/` lane.

Append your `<model>` identifier to the consensus file so authorship is traceable.
Flag any interdependencies between bug fixes in the consensus document.
