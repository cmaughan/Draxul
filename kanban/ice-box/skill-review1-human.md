# Skill Recommendations for Draxul Development

## High-Value for This Project

### 1. Hooks (via `/update-config`)
- **Pre-commit build check**: Auto-run `cmake --build build --target draxul draxul-tests` before commits land. CLAUDE.md already warns about this but a hook enforces it.
- **Post-edit shader validation**: Trigger `glslc` validation when `.vert`/`.frag` files change — catches SPIR-V compilation errors immediately.
- **Auto-format restage**: The pre-commit hook runs clang-format but you still have to restage manually. A hook can automate the retry.

### 2. Plans (`/plan` mode)
- Essential for multi-file renderer changes that touch the abstraction hierarchy (`IBaseRenderer` → `I3DRenderer` → `IGridRenderer`). Getting alignment before editing across 5+ files prevents wasted work.
- Good for cross-platform changes where Windows Vulkan and macOS Metal paths both need updating.

### 3. Worktree-isolated agents
- Run experimental refactors or prototype features in isolation without polluting your working tree. Useful when exploring architectural changes to the host/renderer hierarchy.

### 4. Background agents for review
- When you finish a feature branch, launch parallel review agents to catch issues across renderer, RPC, and font pipeline code simultaneously.

## Workflow Suggestions

### 5. Memory system
- Save common debugging workflows (log flags, test commands)
- Platform-specific gotchas hit repeatedly
- Preferences for PR structure and commit style

### 6. Render test blessing workflow
- If frequently running `py do.py blessbasic` etc., a custom hook could auto-detect which scenarios changed and suggest the right bless command.

### 7. `/simplify` after feature work
- The codebase has a clean architecture. Running simplify after adding features helps catch unnecessary abstractions before they calcify.

## What Would Help Claude Most

- **A CLAUDE.md entry about preferred workflow** (plans first, or dive in? Big PRs or small ones?) — will be saved to memory once communicated.
- **Weak spots** — e.g., "I'm less familiar with the Metal side" — so Claude can be more thorough reviewing those areas.
