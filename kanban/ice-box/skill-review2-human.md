# Custom Skills to Build for Draxul

## Build & Test Skills

### `/smoke`
Run the full smoke test pipeline (`cmake --build build --target draxul draxul-tests && py do.py smoke`) and interpret failures. Knows about the two-platform build, shader compilation, and render snapshot expectations. Saves you from remembering the exact sequence.

### `/bless <scenario>`
Wrapper around `py do.py bless*` that first shows you the visual diff (old vs new reference), asks for confirmation, then blesses and stages the updated references. Prevents accidental blessings.

### `/render-check`
After touching renderer code, builds the target, runs render snapshots, and reports which scenarios changed with a summary of what shifted (e.g., "glyph positions moved 1px left in unicode scenario").

## Cross-Platform Skills

### `/cross-platform-audit`
Given a file you just changed in `src/vulkan/`, finds the Metal counterpart (or vice versa) and reviews whether the same change is needed there. Flags divergences in the two backends. This is easy to forget and hard to catch in review.

### `/shader-pair`
When editing a `.vert` or `.frag` shader, loads both the GLSL and Metal versions side-by-side, checks for semantic drift, and validates the GLSL compiles with `glslc`.

## Architecture Skills

### `/trace-dataflow <event>`
Traces a specific Neovim redraw event (e.g., `grid_line`, `hl_attr_define`) from msgpack-RPC decode through `UiEventHandler` → `Grid` → `App::update_grid_to_renderer()` → GPU buffer write. Outputs the full call chain with file:line references. Useful when debugging rendering glitches.

### `/add-render-pass`
Scaffolds a new `IRenderPass` subclass: creates the header/source, registers it with the renderer, and wires up the Metal/Vulkan-specific bits. Follows the pattern established by `CubeRenderPass`.

### `/new-library`
Scaffolds a new `libs/draxul-<name>/` library following the project's conventions: `include/draxul/`, `src/`, `CMakeLists.txt` with the right FetchContent patterns and dependency graph position.

## Review & Quality Skills

### `/review-pr`
Already exists conceptually in the consensus workflow, but a dedicated skill could: pull the diff, spawn parallel agents for renderer/RPC/font review, and auto-produce the consensus format described in CLAUDE.md.

### `/check-hierarchy`
Validates the renderer and host inheritance hierarchies haven't been violated: no upward dependencies, no backend headers leaking into `app/`, no duplicated headers between `src/` and `include/draxul/`. Codifies the "Known Pitfalls" section as an automated check.

## Day-to-Day

### `/nvim-rpc-debug <method>`
Generates a replay fixture (using `tests/support/replay_fixture.h`) for a specific RPC method, pre-filled with realistic msgpack structure. Cuts the boilerplate when reproducing UI parsing bugs.

### `/work-item <title>`
Creates a new work item in `plans/work-items/` with the established format, auto-numbered, and linked in any index files.

## Priority

Highest-ROI skills to build first: **`/cross-platform-audit`**, **`/trace-dataflow`**, and **`/smoke`** — they address the most error-prone and repetitive parts of the workflow.
