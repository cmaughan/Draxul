Review the repository through the supplied `repomix-output.xml` in one coordinating review run. It contains source paths, line numbers, directory structure, documentation, tests, tooling, and initialized product submodules. Every reviewer and subagent must use this same packed file as the sole source input. Read and search within it using bounded tool output; continue after truncation. Do not create runner batches, segment receipts, or coverage JSON.

## Review procedure and completion criteria

1. **Map the system first.** Read the embedded `CLAUDE.md`, `docs/module-map.md`, `docs/features.md`, relevant CMake targets, and applicable root/product agent guidance. Identify major subsystems, public interfaces, ownership, lifecycle, and important cross-component flows before selecting review areas.
2. **Delegate a bounded review team.** Explicitly use up to four read-only subagents for coherent, non-overlapping subsystem investigations. Balance the scope across workers; do not assign arbitrary character ranges. Give each worker the review focus below, the architecture context, its assigned areas and boundary checks, and the same packed input. Use the same model and high reasoning effort for Codex/Claude; do not substitute a cheaper model. Prohibit nested delegation. Workers return evidence and inspected/unreviewed areas to the coordinator. If delegation is unavailable, perform the same investigations sequentially and disclose that limitation.
3. **Cover every major area.** The coordinator and workers together must examine application orchestration and configuration; client/server/control and session lifecycle; terminal/Neovim/PTY/input behavior; rendering, fonts and platform implementations; built-in modules; each initialized product plugin; and relevant SDK, build, tests, scripts and documentation. Inspect root and product Kanban lanes to exclude already tracked work. For each area, trace representative behavior through its implementation, callers, dependencies and tests. A directory listing or a keyword match alone is not substantive review.
4. **Verify and connect the results.** Wait for every worker. The coordinator must re-read cited evidence and surrounding control/data flow before accepting a finding, check counterexamples and existing protections, reconcile overlap, and perform a final cross-component pass over ownership, initialization/shutdown, asynchronous state, protocol/configuration contracts, plugin boundaries and Windows/macOS parity. Follow the focus-specific rules below; do not turn a feature or refactor review into a bug report.
5. **Finish on scope, not finding count.** Do not stop because several good findings were found, and do not impose a target number. Finish when every major area has been substantively examined and the verification/boundary pass is complete, or when a real time/context/tool limit prevents it. If limited, return useful verified results and explicitly mark the review partial. Never describe unverified worker claims as confirmed findings.

Report every distinct, substantiated finding, keeping each entry concise without capping the number. End with a short coverage table: area, representative paths or flows examined, whether the coordinator or a worker inspected it, and remaining gaps. Identify delegation failures and partially reviewed areas. Keep unresolved leads separate from accepted findings, with the evidence still needed; do not recommend Kanban cards for those leads. Coverage is an honest account of investigation, not proof that all defects or opportunities were found.

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
