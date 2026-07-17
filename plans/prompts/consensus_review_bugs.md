Read the latest bug-focused reviews in `plans/reviews/` (files matching `review-bugs-latest.*.md`) and produce a single `review-bugs-consensus.md` in `plans/reviews/`.

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

Then extract bug-fix work items from the confirmed bugs. File each as a markdown file in `kanban/pending/` starting from `00 <descriptive-name> -bug.md`, with highest-severity bugs getting the lowest numbers. Each work item should contain:
- The bug description and trigger scenario
- Investigation steps (checkboxes)
- Fix strategy (checkboxes)
- Acceptance criteria (checkboxes)

Do NOT create work items for issues already in `kanban/pending/`, `kanban/ice-box/`, or `kanban/done/`.

Append your `<model>` identifier to the consensus file so authorship is traceable.
Flag any interdependencies between bug fixes in the consensus document.
