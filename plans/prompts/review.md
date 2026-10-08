Review the repository through the supplied `repomix-output.xml` in one coordinating review run. It contains source paths, line numbers, directory structure, documentation, tests, tooling, and initialized product submodules. Every reviewer and subagent must use this same packed file as the sole source input. Read and search within it using bounded tool output; continue after truncation. Do not create runner batches, segment receipts, or coverage JSON.

## Plain-English summaries

Start every accepted finding or recommendation with `**Summary:**` and one short sentence explaining what will change and why it is needed, for someone who has not read the code. Use everyday language; avoid class names, file paths, acronyms, and unexplained technical terms. Describe the current problem and intended benefit without claiming unsupported results or measured speedups. Put technical evidence and implementation details below the summary.

Every proposed, created, or revised Kanban card, including cards merged into existing work and cards in product submodules, must put this summary immediately below its title heading and before priority, source, or other metadata. Follow the summary with exactly one `**Priority:** Pn — short reason` line on the CLAUDE.md scale (P0 critical, P1 high, P2 medium, P3 low); never add `**Severity:**`, `**Priority/evidence:**`, or numeric priority lines to a card. Preserve existing evidence and checklist progress when revising a card.

## Review procedure and completion criteria

1. **Inventory the scope before investigating.** Read the embedded `CLAUDE.md`, `docs/module-map.md`, `docs/features.md`, relevant CMake targets, and applicable root/product guidance. Build a Markdown checklist from the actual source inventory, not just these documents: application orchestration/configuration; client/server/control/session lifecycle; terminal/Neovim/PTY/input; rendering/fonts/window/platform paths; built-in modules; SDK/plugin support; every initialized product and descendant module; and relevant build, scripts, tests and documentation. Account for first-party implementation directories and record explicit reasons for exclusions such as generated or third-party code. Split large areas into bounded responsibilities with named entry points, contracts, source paths, backend/platform variants and an owner. Do not hide several products or all of a large renderer inside one catch-all assignment. Update the checklist when investigation reveals another responsibility.
2. **Delegate bounded assignments, not a fixed first wave.** Use read-only subagents within the available concurrency limit, capped at four simultaneous workers (and fewer if the host counts the coordinator in that limit). This limits concurrency, not the total number of assignments. Give each worker the review focus, its checklist entries, relevant boundaries, evidence requirements, and the same immutable packed input. Use the selected model and high effort where supported; disclose any setting that cannot be enforced. Do not substitute a cheaper model or allow nested delegation. Reuse or replace workers as assignments finish; keep remaining responsibilities queued. If delegation is unavailable, investigate sequentially and disclose it. Each worker returns inspected paths/contracts, supported findings, counterexamples checked, and precise unfinished work, including areas with no substantiated finding.
3. **Earn each completed checklist entry.** Inspect the implementation, callers, dependencies and relevant tests for that responsibility. Trace its entry points and state transitions through initialization, normal use, boundary/malformed inputs, error recovery, cancellation, shutdown and ownership wherever applicable. Read each relevant Windows/macOS and Vulkan/Metal implementation separately; source review of the other platform does not require running it. Use the focus-specific investigation requirements below, not a generic bug hunt for every review type. Follow consequential branches and helper calls rather than stopping at one representative function. Listings, keyword matches, skimmed code and lines-read counts do not establish review coverage. Inspect test assertions and fixtures for the contracts being reviewed, not merely the presence of test names. Record why any check is inapplicable.
4. **Verify findings independently of coverage.** Wait for each assigned result. The coordinator re-reads cited evidence and surrounding control/data flow, checks existing protections and counterexamples, and reconciles overlap before accepting a claim. Inspect actual cards in all root and descendant `pending`, `ice-box` and `done` lanes. Exclude already tracked findings without skipping the surrounding implementation. No finding quota applies; "examined, no substantiated issue found" is a valid outcome. Keep unresolved leads separate with the missing evidence; never turn them into speculative cards.
5. **Close gaps before drafting the final report.** Audit worker results against the original and expanded checklist. Skipped, skimmed or partially reviewed responsibilities remain open work, not a disclaimer that discharges the assignment. Reassign them or examine them yourself, including newly exposed callers and platform paths. For example, "all product Metal backends omitted" requires follow-up assignments, not a completed rendering entry. Continue gap-closing passes until all in-scope obligations are examined or an actual limit prevents further work.
6. **Perform a separate cross-component pass.** After subsystem investigations, trace shared contracts across producers and consumers: initialization/shutdown, asynchronous publication, ownership, protocol/configuration limits, plugin boundaries and platform parity. Apply the review's own focus to these traces. Reopen affected checklist entries when a boundary exposes unexamined code, and verify any additional findings.
7. **Stop on evidence, not report size.** A static review is complete only when every in-scope checklist entry and the gap-closing/cross-component passes are complete. Do not stop because a useful list exists or the first workers finished. Respect actual time, context, tool and permission limits; do not bypass them or restart providers. If one prevents completion, return a partial report identifying the observed limit, exact unfinished paths/contracts and next investigation steps. Repository size alone is not evidence that a limit was reached. If work was left unfinished without an observed limit, say so; do not invent a blocker or label it complete.

## Completion and evidence report

Begin with **Static review coverage: complete / partial** and **Runtime validation: not performed** for this read-only packed-source workflow. These describe investigation, not an assurance that all defects or opportunities have been found. Do not run builds, tests, benchmarks or project binaries; distinguish inspected test source and archived measurements from execution during this review.

Include the final Markdown checklist/table with one row per bounded responsibility: owner, concrete source paths and contracts examined, platform/backend checks, evidence or no-finding outcome, and status (`examined`, `partial`, `not examined`, or `out of scope` with justification). Preserve unfinished detail instead of collapsing it into "some branches" or "sampled." An in-scope gap makes overall static coverage partial even when finding verification succeeded. End with the remaining investigation queue and observed limits, or explicitly state that no checklist obligations remain. Report every distinct supported finding without a numeric quota or arbitrary output cap.

## Focus-specific investigation

For each responsibility, trace the user journeys it supports from discovery and configuration through operation, feedback, recovery and cross-platform behavior. Verify capabilities against their actual implementation and feature documentation. Examine infrastructure for constraints on those journeys, not as a source of unrelated cleanup or defect proposals.

Your sole focus is **user-facing features and quality-of-life improvements**. Identify valuable capabilities that are missing, incomplete, awkward to discover, or inconsistent across hosts and platforms. This is not a bug hunt or a refactoring review.

Use `docs/features.md` and the detail pages under `docs/features/` as the implemented-feature baseline. Inspect root and product `kanban/pending/`, `kanban/ice-box/`, and `kanban/done/` lanes before proposing anything, and do not duplicate an existing work item.

Evaluate opportunities in areas such as:

1. Terminal and Neovim workflows, navigation, panes, sessions, clipboard, input, and discoverability.
2. Markdown, Kanban, MegaCity, SatView, ScoreView, and other product modules.
3. Cross-platform parity between Windows/Vulkan and macOS/Metal.
4. Accessibility, diagnostics, configuration, onboarding, and failure recovery.
5. Small workflow improvements with high daily value as well as larger differentiated capabilities.

For each recommendation, include:

- **Priority**: P0, P1, P2, or P3 (see the CLAUDE.md priority scale).
- **User problem**: the concrete workflow or friction being addressed.
- **Proposed behavior**: what the user would experience.
- **Current evidence**: relevant files, documentation, or existing behavior that show the gap is still present.
- **Likely implementation areas**: the modules or interfaces that would be involved, without turning the entry into a refactor proposal.
- **Cross-platform considerations**: how Windows and macOS remain consistent.
- **Acceptance signal**: a concise way to know the feature is useful and complete.

Do NOT report:

- Correctness bugs or runtime defects.
- Code smells, module splits, dependency cleanup, or other refactoring work.
- Pure test-infrastructure improvements without a user-facing capability.
- Ideas already represented in any Kanban lane.
- Speculative ideas with no connection to Draxul's actual product direction.

Rank recommendations by expected user value, then implementation cost and risk. After the full uncapped list, include a shortlist of up to 10 features and the strongest existing feature qualities worth preserving. Validate each gap against actual implemented behavior and documentation; absence of a keyword is not evidence that a capability is missing.

Return the entire report as markdown so the calling script receives the full review.
