Review the repository through the supplied `repomix-output.xml` in one coordinating review run. It contains source paths, line numbers, directory structure, documentation, tests, tooling, and initialized product submodules. Every reviewer and subagent must use this same packed file as the sole source input. Read and search within it using bounded tool output; continue after truncation. Do not create runner batches, segment receipts, or coverage JSON.

## Review procedure and completion criteria

1. **Map the system first.** Read the embedded `CLAUDE.md`, `docs/module-map.md`, `docs/features.md`, relevant CMake targets, and applicable root/product agent guidance. Identify major subsystems, public interfaces, ownership, lifecycle, and important cross-component flows before selecting review areas.
2. **Delegate a bounded review team.** Explicitly use up to four read-only subagents for coherent, non-overlapping subsystem investigations. Balance the scope across workers; do not assign arbitrary character ranges. Give each worker the review focus below, the architecture context, its assigned areas and boundary checks, and the same packed input. Use the same model and high reasoning effort for Codex/Claude; do not substitute a cheaper model. Prohibit nested delegation. Workers return evidence and inspected/unreviewed areas to the coordinator. If delegation is unavailable, perform the same investigations sequentially and disclose that limitation.
3. **Cover every major area.** The coordinator and workers together must examine application orchestration and configuration; client/server/control and session lifecycle; terminal/Neovim/PTY/input behavior; rendering, fonts and platform implementations; built-in modules; each initialized product plugin; and relevant SDK, build, tests, scripts and documentation. Inspect root and product Kanban lanes to exclude already tracked work. For each area, trace representative behavior through its implementation, callers, dependencies and tests. A directory listing or a keyword match alone is not substantive review.
4. **Verify and connect the results.** Wait for every worker. The coordinator must re-read cited evidence and surrounding control/data flow before accepting a finding, check counterexamples and existing protections, reconcile overlap, and perform a final cross-component pass over ownership, initialization/shutdown, asynchronous state, protocol/configuration contracts, plugin boundaries and Windows/macOS parity. Follow the focus-specific rules below; do not turn a feature or refactor review into a bug report.
5. **Finish on scope, not finding count.** Do not stop because several good findings were found, and do not impose a target number. Finish when every major area has been substantively examined and the verification/boundary pass is complete, or when a real time/context/tool limit prevents it. If limited, return useful verified results and explicitly mark the review partial. Never describe unverified worker claims as confirmed findings.

Report every distinct, substantiated finding, keeping each entry concise without capping the number. End with a short coverage table: area, representative paths or flows examined, whether the coordinator or a worker inspected it, and remaining gaps. Identify delegation failures and partially reviewed areas. Keep unresolved leads separate from accepted findings, with the evidence still needed; do not recommend Kanban cards for those leads. Coverage is an honest account of investigation, not proof that all defects or opportunities were found.

Your sole focus is **user-facing features and quality-of-life improvements**. Identify valuable capabilities that are missing, incomplete, awkward to discover, or inconsistent across hosts and platforms. This is not a bug hunt or a refactoring review.

Use `docs/features.md` and the detail pages under `docs/features/` as the implemented-feature baseline. Inspect root and product `kanban/pending/`, `kanban/ice-box/`, and `kanban/done/` lanes before proposing anything, and do not duplicate an existing work item.

Evaluate opportunities in areas such as:

1. Terminal and Neovim workflows, navigation, panes, sessions, clipboard, input, and discoverability.
2. Markdown, Kanban, MegaCity, SatView, ScoreView, and other product modules.
3. Cross-platform parity between Windows/Vulkan and macOS/Metal.
4. Accessibility, diagnostics, configuration, onboarding, and failure recovery.
5. Small workflow improvements with high daily value as well as larger differentiated capabilities.

For each recommendation, include:

- **Priority**: HIGH, MEDIUM, or LOW.
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
