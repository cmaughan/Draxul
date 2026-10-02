Read exactly the bug-focused review reports listed in `INPUT_REVIEWS/index.md` and return one complete consensus as your final Markdown response. The trusted runner publishes the consensus and any enabled Kanban cards; do not write files or substitute mutable latest-file globs.

## Verification and coverage obligations

1. **Inventory claims and coverage separately.** Read exactly the selected reports and map their bounded responsibilities to current source, documentation and actual root/descendant tracker lanes. Preserve each provider's examined, partial and unexamined areas, source snapshot differences and evidence limitations. Missing or vague coverage evidence is not proof that an area was reviewed.
2. **Verify through bounded assignments.** Use read-only subagents for coherent groups of claims when useful, within available concurrency and at most four simultaneous workers; the limit is not a cap on total assignments. Reuse or replace workers until the verification queue is resolved, with no nested delegation. Use the selected model and high effort where supported, disclosing unenforceable settings. If delegation is unavailable, verify sequentially. Give workers exact claims, current-source boundaries and this review's evidence rules. Wait for all results and re-read supporting evidence before acceptance.
3. **Close verification gaps.** Give every supplied claim a supported disposition: accepted, rejected with reason, already tracked with exact card path, or unresolved with precise missing evidence. Reassign incomplete verification rather than silently dropping claims or accepting them on authority. Check callers, protections, tests and platform branches; follow cross-component implications. Respect actual execution limits and disclose any interrupted verification.
4. **Audit the combined investigation coverage.** A finding verified in a subsystem does not establish full coverage of that subsystem. Credit complementary provider coverage only where concrete paths and contracts support it. Explicitly list responsibilities neither provider examined adequately. Do not erase gaps because both reports completed, agreed, or found many issues. This synthesis is not automatically a fresh whole-repository review; if an original investigation gap was not actually closed here, preserve it as an outstanding review assignment.
5. **Report separate outcomes.** Begin with **Finding verification: complete / partial**, **Combined static review coverage: complete / partial**, and **Runtime validation: not performed** for this read-only workflow. Verification can be complete when every claim has an explicit disposition even though some leads remain unresolved; it does not mean every claim is accepted. Combined coverage is complete only when the in-scope investigation obligations have supporting evidence and no outstanding gaps. Keep provider execution success separate from both outcomes.
6. **Preserve an actionable handoff.** Include a coverage table mapping responsibilities to provider evidence, any additional source investigation here, and remaining gaps. For partial work, name exact paths/contracts, missing evidence and next bounded assignments; distinguish observed time/context/tool limits from work simply not completed. No generic "large repository" excuse, percentage estimate or invented blocker. Do not run project binaries, builds, tests or benchmarks, and do not present existing measurements as newly executed validation. Never create speculative bug/feature/refactor/performance cards from unresolved leads or coverage gaps.

Report every distinct supported finding without a numeric quota or arbitrary output cap. Keep the focus-specific evidence, ranking and task-creation rules below.

## Plain-English summaries

Start every accepted finding or recommendation with `**Summary:**` and one short sentence explaining what will change and why it is needed, for someone who has not read the code. Use everyday language; avoid class names, file paths, acronyms, and unexplained technical terms. Describe the current problem and intended benefit without claiming unsupported results or measured speedups. Put technical evidence and implementation details below the summary.

Every proposed, created, or revised Kanban card, including cards merged into existing work and cards in product submodules, must put this summary immediately below its title heading and before priority, source, or other metadata. Preserve existing evidence and checklist progress when revising a card.

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

Then extract bug-fix work items from the confirmed bugs. Put core and shared-boundary cards under exact `### kanban/pending/<filename>.md` headings. Put defects wholly owned by an initialized product submodule under exact `### plugins/<product>/kanban/pending/<filename>.md` headings, deriving `<product>` from the actual initialized product inventory rather than a fixed product-name list. Include a `**Source:**` line with a backtick-quoted owning source path in each card. Use an available two-digit priority in each owning lane, ranking higher-severity bugs first within that lane. Each work item should contain:
- The bug description and trigger scenario
- Investigation steps (checkboxes)
- Fix strategy (checkboxes)
- Acceptance criteria (checkboxes)

Do NOT create work items for issues already in any root or product `kanban/pending/`, `kanban/ice-box/`, or `kanban/done/` lane.

Append your `<model>` identifier to the consensus file so authorship is traceable.
Flag any interdependencies between bug fixes in the consensus document.
