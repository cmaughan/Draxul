Explore the repository directly. Use file discovery and file-reading tools to inspect the current source under `app/`, `libs/`, `modules/`, `shaders/`, `tests/`, and `scripts/`, plus the root build files and relevant documentation. Do not rely on a pre-generated combined source file.

Your sole focus is **user-facing features and quality-of-life improvements**. Identify valuable capabilities that are missing, incomplete, awkward to discover, or inconsistent across hosts and platforms. This is not a bug hunt or a refactoring review.

Use `docs/features.md` and the detail pages under `docs/features/` as the implemented-feature baseline. Inspect `kanban/pending/`, `kanban/ice-box/`, and `kanban/done/` before proposing anything, and do not duplicate an existing work item.

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

Rank recommendations by expected user value, then implementation cost and risk. End with a concise top-10 feature shortlist and a short list of the strongest existing feature qualities worth preserving.

Return the entire report as markdown so the calling script receives the full review.
