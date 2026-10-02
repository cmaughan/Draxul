Review the repository through the supplied `repomix-output.xml` in one coordinating review run. It contains source paths, line numbers, directory structure, documentation, tests, tooling, and initialized product submodules. Every reviewer and subagent must use this same packed file as the sole source input. Read and search within it using bounded tool output; continue after truncation. Do not create runner batches, segment receipts, or coverage JSON.

## Plain-English summaries

Start every accepted finding or recommendation with `**Summary:**` and one short sentence explaining what will change and why it is needed, for someone who has not read the code. Use everyday language; avoid class names, file paths, acronyms, and unexplained technical terms. Describe the current problem and intended benefit without claiming unsupported results or measured speedups. Put technical evidence and implementation details below the summary.

Every proposed, created, or revised Kanban card, including cards merged into existing work and cards in product submodules, must put this summary immediately below its title heading and before priority, source, or other metadata. Preserve existing evidence and checklist progress when revising a card.

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

For each responsibility, apply the bug categories below to reachable flows, including failure paths and numeric/domain boundaries. Trace the trigger through callers, handlers, locks and ownership to its consequence; do not equate a suspicious operation with a demonstrated defect.

Your sole focus is **finding bugs, defects, and correctness issues**. Ignore style, naming, architecture, and feature ideas — only report things that are **wrong or will break at runtime**.

Specifically hunt for:

1. **Memory safety**: use-after-free, dangling pointers/references, double-free, buffer overruns, uninitialized reads, iterator invalidation during mutation, stack-use-after-scope in lambdas/callbacks.
2. **Concurrency**: data races (shared mutable state without locks), lock-order inversions, missing atomic operations, signal-unsafe calls, thread-unsafe static locals.
3. **Undefined behavior**: signed integer overflow in arithmetic, null dereference paths, out-of-bounds indexing, strict aliasing violations, use of moved-from objects.
4. **Logic errors**: off-by-one in loops/ranges, wrong comparison operators, inverted conditions, dead code paths that mask real failures, silent truncation of values.
5. **Resource leaks**: file handles, GPU resources, threads not joined, allocations on error paths that skip cleanup.
6. **Error handling gaps**: unchecked return values that can fail (file I/O, allocations, system calls), catch blocks that swallow and lose context, exceptions thrown across C boundaries.
7. **Platform-specific**: Windows/macOS divergence where one path is correct and the other is not, assumptions about endianness, path separators, or process model.
8. **Numeric precision**: float equality comparisons that should use epsilon, integer overflow in size calculations, narrowing conversions that lose data.

For each accepted bug, verify a reachable trigger and trace its consequence through relevant callers, exception handlers, locks, ownership and platform branches. Reject candidates contradicted by existing guards or tests.

For each bug found, report:
- **File and line number**
- **Severity**: CRITICAL (crash/UB/data corruption), HIGH (incorrect behavior), MEDIUM (edge-case failure)
- **What goes wrong**: concrete scenario that triggers the bug
- **Suggested fix**: one-liner or short code snippet

Do NOT report:
- Code smells, style issues, or refactoring opportunities
- Missing features or enhancements
- Items already tracked in any root or product `kanban/pending/`, `kanban/ice-box/`, or `kanban/done/` lane
- Theoretical issues you cannot construct a trigger scenario for

docs/features.md has a list of implemented features for context.

Rank your findings by severity (CRITICAL first). The entire output will be returned as markdown so that a script calling this gets the full report.
