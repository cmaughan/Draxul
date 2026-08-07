Explore the repository source files directly — use Glob and Read (or equivalent file-reading tools) to scan all source files under `app/`, `libs/`, `shaders/`, `tests/`, and `scripts/`. Read the actual files as they exist on disk; do not rely on any pre-generated combined file.

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

For each bug found, report:
- **File and line number**
- **Severity**: CRITICAL (crash/UB/data corruption), HIGH (incorrect behavior), MEDIUM (edge-case failure)
- **What goes wrong**: concrete scenario that triggers the bug
- **Suggested fix**: one-liner or short code snippet

Do NOT report:
- Code smells, style issues, or refactoring opportunities
- Missing features or enhancements
- Items already tracked in `kanban/pending/`, `kanban/ice-box/`, or `kanban/done/`
- Theoretical issues you cannot construct a trigger scenario for

docs/features.md has a list of implemented features for context.

Rank your findings by severity (CRITICAL first). The entire output will be returned as markdown so that a script calling this gets the full report.
