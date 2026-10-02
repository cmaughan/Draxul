Read exactly the refactor-focused review reports listed in `INPUT_REVIEWS/index.md` and return one complete consensus as your final Markdown response. The trusted runner publishes the consensus and any enabled Kanban cards; do not write files or substitute mutable latest-file globs.

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

Return one complete implementation-ready markdown work item for the runner to create for each accepted proposal. Use root `kanban/pending/` for core/shared work and `plugins/<product>/kanban/pending/` for work wholly owned by an initialized product submodule. Include a `**Source:**` line with a backtick-quoted owning source path. Use an unused two-digit priority in the owning lane and end each filename with `-refactor.md`; do not overwrite or duplicate an existing card. Each card must have scoped checkbox sections for boundary verification, implementation/migration, unit tests, cross-platform validation, agent documentation/tooling where relevant, and acceptance criteria.

Append your `<model>` identifier to the consensus file and clearly state recommended sequencing and interdependencies. Return the full consensus, including complete proposed cards under exact `### kanban/pending/<filename>.md` or `### plugins/<product>/kanban/pending/<filename>.md` headings when task creation is enabled.
