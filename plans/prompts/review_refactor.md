Review the repository through the supplied `repomix-output.xml` in one coordinating review run. It contains source paths, line numbers, directory structure, documentation, tests, tooling, and initialized product submodules. Every reviewer and subagent must use this same packed file as the sole source input. Read and search within it using bounded tool output; continue after truncation. Do not create runner batches, segment receipts, or coverage JSON.

## Review procedure and completion criteria

1. **Map the system first.** Read the embedded `CLAUDE.md`, `docs/module-map.md`, `docs/features.md`, relevant CMake targets, and applicable root/product agent guidance. Identify major subsystems, public interfaces, ownership, lifecycle, and important cross-component flows before selecting review areas.
2. **Delegate a bounded review team.** Explicitly use up to four read-only subagents for coherent, non-overlapping subsystem investigations. Balance the scope across workers; do not assign arbitrary character ranges. Give each worker the review focus below, the architecture context, its assigned areas and boundary checks, and the same packed input. Use the same model and high reasoning effort for Codex/Claude; do not substitute a cheaper model. Prohibit nested delegation. Workers return evidence and inspected/unreviewed areas to the coordinator. If delegation is unavailable, perform the same investigations sequentially and disclose that limitation.
3. **Cover every major area.** The coordinator and workers together must examine application orchestration and configuration; client/server/control and session lifecycle; terminal/Neovim/PTY/input behavior; rendering, fonts and platform implementations; built-in modules; each initialized product plugin; and relevant SDK, build, tests, scripts and documentation. Inspect root and product Kanban lanes to exclude already tracked work. For each area, trace representative behavior through its implementation, callers, dependencies and tests. A directory listing or a keyword match alone is not substantive review.
4. **Verify and connect the results.** Wait for every worker. The coordinator must re-read cited evidence and surrounding control/data flow before accepting a finding, check counterexamples and existing protections, reconcile overlap, and perform a final cross-component pass over ownership, initialization/shutdown, asynchronous state, protocol/configuration contracts, plugin boundaries and Windows/macOS parity. Follow the focus-specific rules below; do not turn a feature or refactor review into a bug report.
5. **Finish on scope, not finding count.** Do not stop because several good findings were found, and do not impose a target number. Finish when every major area has been substantively examined and the verification/boundary pass is complete, or when a real time/context/tool limit prevents it. If limited, return useful verified results and explicitly mark the review partial. Never describe unverified worker claims as confirmed findings.

Report every distinct, substantiated finding, keeping each entry concise without capping the number. End with a short coverage table: area, representative paths or flows examined, whether the coordinator or a worker inspected it, and remaining gaps. Identify delegation failures and partially reviewed areas. Keep unresolved leads separate from accepted findings, with the evidence still needed; do not recommend Kanban cards for those leads. Coverage is an honest account of investigation, not proof that all defects or opportunities were found.

Your sole focus is **refactoring for code hygiene, modularity, testability, and safe parallel agent work**. Ignore feature ideas and correctness bugs unless a concrete structural problem is the reason code cannot be isolated or tested; report runtime defects through the bug review instead.

Specifically examine:

1. **Clean code and cohesion**: oversized translation units, mixed responsibilities, hidden coupling, unclear ownership, excessive state, repeated orchestration, and difficult control flow.
2. **Clean module boundaries**: dependency direction, public versus private headers, backend leakage, cycles, app-layer logic that belongs in libraries, and product modules that are not independently understandable.
3. **Shared functionality behind narrow interfaces**: duplicated platform, renderer, host, configuration, parsing, resource-lifetime, or test-support logic that has a stable common contract. Do not recommend generic abstractions merely to remove a few similar lines.
4. **Small static libraries and test seams**: cohesive code that should become a simple static-library target with a minimal public API, explicit dependencies, and focused unit tests. Keep platform-specific implementation details private and preserve both Windows and macOS paths.
5. **Agent-friendly work isolation**: hotspots where unrelated work collides, modules whose ownership cannot be scoped cleanly, broad rebuild/test requirements, generated artifacts, or missing narrow validation commands.
6. **Repository agent guidance and tooling**: whether root and subdirectory `AGENTS.md` files accurately describe boundaries, platform obligations, build/test commands, generated files, ownership, and local pitfalls; whether large product areas need a concise local guide; and whether scripts expose reliable module-level build/test/check entry points.
7. **Testing architecture**: production seams that force integration testing, test helpers coupled to unrelated modules, monolithic test targets, and opportunities for isolated fixtures or target-level tests.

For each proposed structural change, trace the current responsibility across callers, implementations, tests and build targets. Inspect CMake targets and actual include/link relationships before recommending a library split. Prefer proposals that reduce dependency fan-out, compile scope, merge conflicts, and the amount of context an agent needs for a bounded task.

For each finding, report:

- **Location**: concrete files, targets, and line numbers or symbol names.
- **Priority**: P0 (blocks safe change), P1 (high-leverage hygiene), or P2 (useful cleanup).
- **Current structural problem**: evidence from the current tree, including the responsibilities or dependencies that are entangled.
- **Proposed boundary**: the smallest coherent module, static library, interface, or tooling/documentation change.
- **Dependency shape**: intended callers, dependencies, public API, and implementation-private pieces.
- **Migration path**: incremental steps that keep the tree buildable and avoid a broad rewrite.
- **Testing improvement**: the focused tests or target-level validation enabled by the change.
- **Agent-work benefit**: how the change creates clearer ownership or reduces collision and context scope.
- **Risks and prerequisites**: especially Windows/macOS or Vulkan/Metal parity concerns.

Do NOT report:

- New user-facing features.
- Pure style or naming preferences without a measurable ownership, dependency, testing, or maintenance benefit.
- Sweeping rewrites without an incremental boundary and migration path.
- Items already tracked in any root or product `kanban/pending/`, `kanban/ice-box/`, or `kanban/done/` lane.
- Proposed library splits that have not been checked against current CMake and include/link relationships.

Rank findings by leverage and sequencing, not file size alone. End with a compact proposed module/target map, the best isolated work packages for parallel agents, and the strongest existing structural qualities worth preserving.

Return the entire report as markdown so the calling script receives the full review.
