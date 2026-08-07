Explore the repository source files directly. Use file discovery and file-reading tools to inspect the current source under `app/`, `libs/`, `modules/`, `shaders/`, `tests/`, and `scripts/`, plus root build files, `AGENTS.md`, and any subdirectory `AGENTS.md` files. Read the actual files as they exist on disk; do not rely on a pre-generated combined source file.

Your sole focus is **refactoring for code hygiene, modularity, testability, and safe parallel agent work**. Ignore feature ideas and correctness bugs unless a concrete structural problem is the reason code cannot be isolated or tested; report runtime defects through the bug review instead.

Specifically examine:

1. **Clean code and cohesion**: oversized translation units, mixed responsibilities, hidden coupling, unclear ownership, excessive state, repeated orchestration, and difficult control flow.
2. **Clean module boundaries**: dependency direction, public versus private headers, backend leakage, cycles, app-layer logic that belongs in libraries, and product modules that are not independently understandable.
3. **Shared functionality behind narrow interfaces**: duplicated platform, renderer, host, configuration, parsing, resource-lifetime, or test-support logic that has a stable common contract. Do not recommend generic abstractions merely to remove a few similar lines.
4. **Small static libraries and test seams**: cohesive code that should become a simple static-library target with a minimal public API, explicit dependencies, and focused unit tests. Keep platform-specific implementation details private and preserve both Windows and macOS paths.
5. **Agent-friendly work isolation**: hotspots where unrelated work collides, modules whose ownership cannot be scoped cleanly, broad rebuild/test requirements, generated artifacts, or missing narrow validation commands.
6. **Repository agent guidance and tooling**: whether root and subdirectory `AGENTS.md` files accurately describe boundaries, platform obligations, build/test commands, generated files, ownership, and local pitfalls; whether large product areas need a concise local guide; and whether scripts expose reliable module-level build/test/check entry points.
7. **Testing architecture**: production seams that force integration testing, test helpers coupled to unrelated modules, monolithic test targets, and opportunities for isolated fixtures or target-level tests.

Inspect CMake targets and actual include/link relationships before recommending a library split. Prefer proposals that reduce dependency fan-out, compile scope, merge conflicts, and the amount of context an agent needs for a bounded task.

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
- Items already tracked in `kanban/pending/`, `kanban/ice-box/`, or `kanban/done/`.
- Proposed library splits that have not been checked against current CMake and include/link relationships.

Rank findings by leverage and sequencing, not file size alone. End with a compact proposed module/target map, the best isolated work packages for parallel agents, and the strongest existing structural qualities worth preserving.

Return the entire report as markdown so the calling script receives the full review.
