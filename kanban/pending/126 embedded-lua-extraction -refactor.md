# WI 126 — Embedded Lua Extraction from Host Code

**Type:** Refactor  
**Severity:** Medium (testability, reviewability, duplication)  
**Source:** Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`libs/draxul-host/src/nvim_host.cpp` embeds multi-line Lua blocks as raw C++ string literals. These are:
- Invisible to syntax highlighting and Lua linters
- Untestable in isolation (must go through the full Neovim RPC stack to exercise)
- Duplicated (see **WI 115** for the `open_file_at_type:` duplication)
- Hard to review in diffs (C++ string escaping obscures intent)

From Gemini: "Large Lua blocks embedded as C++ string literals in `nvim_host.cpp` are untestable in isolation, invisible to syntax highlighting and linting, and duplicated."

---

## Fix Strategy

**Option A — Separate `.lua` source files compiled into the binary:**
1. Move each Lua block to `libs/draxul-host/src/lua/open_file.lua`, `focus_window.lua`, etc.
2. In CMakeLists, `xxd -i` or a CMake custom command generates `generated_lua.h` containing `const char* k_open_file_lua = "..."` from the `.lua` sources.
3. `nvim_host.cpp` `#include`s the generated header.

Advantage: `.lua` files get syntax highlighting and linting; diffs are clean.

**Option B — `constexpr` string constants in a dedicated header:**
Move Lua strings to `libs/draxul-host/src/nvim_lua_scripts.h` with named `constexpr char[]` constants. Simpler than Option A but no syntax highlighting.

**Option B is the minimum viable fix.** Option A is the better engineering outcome.

---

## Investigation Steps

- [ ] After **WI 115** lands: `grep -n 'R"(' libs/draxul-host/src/nvim_host.cpp` — list all raw string literals
- [ ] Categorise each: which are Lua? Any other embedded scripts?
- [ ] Count total Lua blocks to estimate scope

---

## Acceptance Criteria

- [ ] No multi-line Lua literals in `nvim_host.cpp`
- [ ] Each extracted script is named meaningfully
- [ ] If Option A: `.lua` files are under version control and CMake generates the header automatically
- [ ] Existing RPC and nvim process tests still pass
- [ ] CI green

---

## Interdependencies

- **Depends on WI 115** (dedup handler first so extraction starts from a clean single copy)
- **WI 127** (handler consolidation) may proceed in parallel after WI 115
