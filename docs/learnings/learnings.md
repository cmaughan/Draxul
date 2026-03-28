# Learnings

Things discovered while working on this project that are worth remembering for future sessions or other projects.

---

## Tooling

### A running learnings commentary is worth keeping

One thing that has helped in this repo is updating `docs/learnings.md` as the work happens, not treating it as a post-hoc writeup.

Why it helps:
- useful discoveries get captured while the context is still fresh
- small but important fixes and workflow lessons do not get forgotten once the bigger feature is done
- the repo gradually builds a practical memory of "what bit us before" and "what worked well"
- future sessions can pick up the reasoning behind tools, tests, and validation paths without having to rediscover it

Lesson:
- treat the learnings file as a running engineering commentary, not just a final retrospective

---

### Start the debugger immediately when a renderer change causes a startup crash

A useful reminder from the Metal multi-frame-in-flight work: once the app started trapping during startup, the right move was to proactively switch to the debugger straight away instead of continuing to guess from logs or code inspection.

What helped:
- the agent chose on its own to stop speculating and launch the failing startup path under `lldb`
- run the app under `lldb` with the same startup path that was failing
- get the exact trap site before making more renderer changes
- use that to distinguish a teardown/synchronization bug from an actual draw-path crash

In this case, the debugger immediately showed the crash was in `libdispatch` semaphore disposal, which pointed straight at the new Metal frame-semaphore shutdown logic.

Lesson:
- if a rendering or startup change begins crashing, proactively start the debugger early and get the first real stack/trap location before iterating further

---

### Claude can generate and manipulate code dependency graphs

Claude successfully:
- Generated a `scripts/gen_deps.py` script that calls `cmake --graphviz` to produce a dot file of CMake target dependencies
- Wrote Python to post-process the dot file: strip the CMake legend subgraph (brace-depth tracking), filter noise targets (ZERO_CHECK, ALL_BUILD, etc.), and render to SVG via `dot`
- Extended it with `--exclude` (remove a node and all its edges entirely) and `--prune` (keep a named node as a leaf but remove everything it depends on — useful for collapsing third-party library subtrees like SDL3-static or freetype without losing the node itself)

The prune logic works by parsing the dot node/edge lines, building an adjacency list, BFS-ing from the prune root to find all descendants, then dropping those nodes and any edges that reference them.

**Useful for:** visualising module boundaries, spotting unexpected dependency edges, onboarding new contributors.

```sh
python scripts/gen_deps.py --prune SDL3-static --prune freetype --prune harfbuzz
```

---

### clang-uml for class-level dependency diagrams

For deeper than CMake target graphs, `clang-uml` generates UML class diagrams directly from C++ source using `compile_commands.json` (already emitted by the CMake config via `CMAKE_EXPORT_COMPILE_COMMANDS ON`).

Setup:
- `.clang-uml` YAML config in the repo root defines the `draxul_classes` diagram
- `scripts/gen_uml.py` runs `clang-uml` then `plantuml` to render to SVG
- Install: `brew install clang-uml plantuml` (Mac) / `winget install bkryza.clang-uml` + `choco install plantuml` (Windows)
- `compile_commands.json` requires a **Ninja** build (`cmake --preset clang-tools` → `build-tools/`); the default VS generator does not emit it

```sh
python scripts/gen_uml.py                        # all diagrams → docs/uml/
python scripts/gen_uml.py --diagram draxul_classes
python scripts/gen_uml.py --puml-only            # emit .puml without rendering
```

Key config fields in `.clang-uml`:
- `compilation_database_dir`: points at `./build-tools` (Ninja build, has `compile_commands.json`)
- `glob`: must match **`.cpp` translation units**, not headers — clang-uml follows includes from there
- `using_namespace`: strips `draxul::` prefix from all type names in the diagram
- `generate_method_arguments: none`: keeps the diagram readable (no parameter lists)
- `exclude: namespaces: [std, mpack, ...]`: essential — without this the diagram is swamped with STL and third-party types
- **SVG not PNG**: PlantUML's PNG output is pixel-limited and clips large diagrams; always use `-tsvg`

#### clang-uml 0.6.2 known crashes (Windows)
Two source files trigger `STATUS_ILLEGAL_INSTRUCTION` (exit 3221225725) in clang-uml 0.6.2 on Windows:
- `libs/draxul-grid/src/grid.cpp`
- `libs/draxul-nvim/src/ui_events.cpp`

Workaround: exclude them from `glob`. Their classes still appear in the diagram via headers included by other translation units. Running diagrams in parallel (default) also triggers a crash — run each with `-n <name>` sequentially.

#### Package diagrams don't work for single-namespace codebases
`type: package` requires sub-namespaces to group. Since all draxul code is in one `draxul` namespace, the package diagram is always empty. Use the CMake graphviz graph (`docs/deps/deps.svg`) for library-level dependency visualisation instead.

---

### Visual regression tests should capture renderer output, not the desktop

For deterministic screenshot-style testing in Draxul, capture pixels directly from the renderer output instead of taking a desktop screenshot.

What worked well:
- Read back the actual presented frame from the swapchain/drawable
- Keep scenarios deterministic: fixed window size, fixed bundled font, clean Neovim startup, blink disabled, scripted startup commands
- Store platform-specific references because Vulkan and Metal can differ slightly in raster output

Why this matters:
- Desktop screenshots pick up compositor noise, z-order/focus issues, DPI scaling, and other window-manager artifacts
- Swapchain/drawable readback gives a stable pixel buffer that can be diffed mechanically and blessed when expected changes occur

Current repo pattern:
- scenario files under `tests/render/`
- references under `tests/render/reference/`
- actual/diff/report artifacts under `tests/render/out/`

---

### Hero screenshots need a separate path from deterministic render tests

The README/marketing screenshot should not use the exact same startup model as the deterministic regression scenarios.

What we learned:
- Deterministic render tests should stay clean and fixed: bundled font, `-u NONE --noplugin`, fixed commands, blink off, stable compare/bless flow.
- The README hero screenshot looks much better when it uses the user's real Neovim config, theme, statusline, and plugin UI.
- The current split works well:
  - deterministic scenarios live under `tests/render/*.toml`
  - the hero screenshot uses `tests/render/readme-hero.toml`
  - `scripts/update_screenshot.py` defaults to the hero scenario

Important operational detail:
- `update_screenshot.py` does **not** launch a normal visible desktop window. It runs Draxul in render-test/export mode and grabs pixels from the renderer backbuffer. That is why "the app is not popping up" during screenshot generation is expected behavior, not a startup failure.

Another practical upside:
- being able to generate a fresh screenshot directly from the app and drop it straight into the README is extremely useful during UI work
- it keeps the documentation current without manual screenshot editing
- it gives the user and the agent a shared visual artifact to reason about when polishing presentation or chasing regressions

---

### Multi-line array parsing in render scenarios was a real bug

The render scenario loader originally only handled single-line arrays like:

```toml
commands = ["set number", "edit file.txt"]
```

That broke the hero screenshot scenario because `readme-hero.toml` used a multi-line `commands = [ ... ]` block. The failure mode was confusing:
- the screenshot updater appeared to run
- the output PNG did not change
- Draxul's render-test export path was actually failing before capture because it loaded zero startup commands

Fix:
- teach `load_render_test_scenario()` in `app/render_test.cpp` to accumulate multi-line array literals
- add a regression test for multi-line `commands` parsing

Lesson:
- config/test-scenario formats that look TOML-like need to support the syntax we actually write in the repo, or they become silent tooling traps

---

### Plugin-driven screenshot actions should call APIs directly, not replay mappings

Trying to open Mini Files in the hero screenshot by simulating the `-` key was unreliable.

What we found:
- `-` was a Lazy-managed normal-mode mapping with an expr callback
- feeding the key too early did nothing
- even when the mapping existed, replaying the mapped key path was less deterministic than just calling the plugin directly

What worked:
- defer the action briefly inside Neovim
- `pcall(require, 'mini.files')` to force the plugin to load
- call `MiniFiles.open(vim.api.nvim_buf_get_name(0), false)` directly

Lesson:
- for scripted screenshot/setup flows, prefer explicit plugin APIs over simulated keypresses whenever possible

---

### Python downsampling was fun, but the native high-resolution image won

We tried a supersampled hero screenshot path:
- capture at `2560x1600`
- keep the font size the same
- downsample in Python by averaging `2x2` RGBA blocks into a `1280x800` PNG

That worked technically and was straightforward to implement in `scripts/update_screenshot.py`, but the user preferred the native full-resolution result instead.

Current outcome:
- hero screenshots are captured and written directly at `2560x1600`
- no downsample step is used now

Still useful to remember:
- pure-Python RGBA downsampling is easy to add for doc assets if we ever want a smaller supersampled output path later

---

### Small visual regressions are much easier to fix once the bless loop exists

The new render snapshot and screenshot path paid for itself again when the debug overlay landed.

What happened:
- the first overlay implementation introduced a small packing bug in the renderer-side extra-cell region
- cursor and overlay cells ended up fighting over the tail slots
- the regression was minor, but immediately visible once the frame capture path and bless/diff workflow were in place

Why this was useful:
- the problem showed up as a concrete visual mismatch instead of a vague "something looks a bit off"
- the actual image and diff made it obvious that the bug was in overlay/cursor packing, not glyph rasterization or Neovim redraws
- the fix was quick because the feedback loop was already there

Lesson:
- once a UI project has reliable capture, diff, and bless mechanics, even small regressions become cheap to spot and cheap to repair

---

### A root shortcut script helps once the scripted workflows start to sprawl

As smoke tests, render snapshots, blessing flows, and screenshot generation accumulated, the command surface became hard to remember.

What helped:
- add a root-level `do.py`
- give it a `--help` screen with short, single-word commands
- bake in the common scenarios (`smoke`, `basic`, `cmdline`, `unicode`, `blessall`, `shot`, `test`, etc.)

Why this is worth keeping:
- it lowers the friction for using the safety nets we already built
- it reduces "what was the exact command again?" overhead
- it makes the higher-value scripted paths more likely to be used consistently

Lesson:
- once a project grows a few scripted validation flows, invest in one friendly entry point instead of expecting people to remember every raw command

---

### Human-facing structure tools are worth the effort

Tools that help a person see the shape of the codebase are not just documentation polish; they are a practical way to control complexity.

Examples from this repo:
- CMake dependency graphs
- class diagrams from `clang-uml`
- a single friendly command entry point like `do.py`
- deterministic render snapshots that make UI state visible instead of abstract

Why this matters:
- they reduce the cost of orienting in a growing codebase
- they make module boundaries and cross-cutting dependencies easier to spot
- they help both humans and agents reason about where a change belongs
- they lower the chance of accidental architectural drift because the current structure is visible

Lesson:
- if a tool helps a human understand the structure of the system being built, it is usually worth having once the codebase reaches moderate complexity

---

### GitHub Project Board Sync

The work-item markdown files in `plans/work-items/` and `plans/work-items-icebox/` can be synced to the GitHub project board (project #1, "Draxul") using:

```
python do.py syncboard
```

---

## Renderer Efficiency

### Distinguish semantic world rebuilds from per-frame scene extraction

In Megacity there are three very different kinds of work, and it is important not to conflate them:

- semantic/world rebuild: reconcile DB data, build layout, repopulate ECS
- per-frame scene extraction: build the CPU-side scene snapshot from camera state and ECS entities
- GPU render submission: consume the stored snapshot and issue draw commands

The explicit `Rebuild World` button should only guard the first category. It should not be treated as a guard for all scene or renderer work.

Lesson:
- when reasoning about renderer cost, separate "rebuild the world" from "rebuild the frame packet"

### Rebuilding SceneObjects does not mean rebuilding static meshes

Megacity `SceneObject`s are lightweight CPU draw records, not mesh assets. Rebuilding them currently means:

- walk ECS entities
- choose a `MeshId`
- compute a world transform
- copy color and sign UV metadata
- append to the snapshot vector

It does **not** mean:

- regenerate the cube mesh
- regenerate the sign meshes
- reupload static geometry every frame

Those meshes are cached by the Metal/Vulkan Megacity render passes and reused.

Lesson:
- "rebuilding SceneObjects" is mostly CPU extraction and matrix work, not static mesh generation

### `set_scene()` is cheap; `build_scene_snapshot()` is where the CPU work is

`IsometricScenePass::set_scene()` only stores the latest `SceneSnapshot`. The expensive CPU-side part is `build_scene_snapshot()`, which currently:

- updates camera matrices and light parameters
- derives the visible floor-grid spec from the camera footprint
- walks all renderable ECS entities
- rebuilds the `SceneObject` list
- recomputes scene bounds and point-light placement

The render pass `record()` step then consumes that stored snapshot.

Lesson:
- if Megacity feels heavy on camera motion or UI edits, look first at snapshot construction and UI-side preview recomputation, not `set_scene()` itself

### The floor grid uses the right dynamic-geometry model

The floor grid is not rebuilt from scratch every frame unconditionally.

Current behavior:

- the visible `FloorGridSpec` is recomputed when the scene snapshot is rebuilt
- the render pass caches the floor-grid mesh and only regenerates it when that spec changes
- the resulting grid geometry is then streamed through transient buffers for rendering

That is a good model for dynamic scene geometry going forward:

- dynamic geometry can be derived from the current frame state
- mesh generation can still be cached on stable specs
- transient upload is the correct path for draw-time dynamic geometry

Lesson:
- keep dynamic floor/grid-like geometry on the transient path; optimize by reducing unnecessary snapshot churn, not by forcing it into a static-world path

Or directly:

```
python sync_project_board.py
```

**How it works:**
- Items in `plans/work-items/` are created/maintained as **Backlog** on the board.
- Items in `plans/work-items-icebox/` are created/maintained as **IceBox**.
- Items already in **Ready / In progress / In review / Done** are left untouched — the script never resets active work.
- Matching is by title (extracted from the `# H1` heading of each file).
- The script is idempotent; safe to run at any time.

**Setup (first time only):** The `gh` CLI needs the `project` OAuth scope:

```
gh auth refresh -h github.com -s project
```

`sync_project_board.py` at the repo root uses `gh api graphql` mutations: `addProjectV2DraftIssue` to create new items and `updateProjectV2ItemFieldValue` to set the Status field.

---

### SonarCloud: Duplicate Header Detection

SonarCloud flagged `libs/draxul-host/src/scrollback_buffer.h` as a duplicate of `libs/draxul-host/include/draxul/scrollback_buffer.h`. The `src/` copy was a leftover from before the module-boundary refactor (work item 15) and had drifted out of sync with the public header.

The fix: delete the `src/` copy and update the `.cpp` include from `"scrollback_buffer.h"` to `<draxul/scrollback_buffer.h>`.

**Rule**: each header belongs in exactly one location. Public API → `include/draxul/` (angle-bracket include). Internal implementation detail → `src/` (quote include). Never maintain both. If SonarCloud reports a duplication, the `src/` copy is almost always the stale one.

---

### pre-commit for Automatic clang-format Enforcement

The repo uses [pre-commit](https://pre-commit.com) to run `clang-format` automatically on every `git commit`, catching formatting issues before they reach CI or review.

**Configuration** (`.pre-commit-config.yaml` at the repo root):

```yaml
repos:
  - repo: local
    hooks:
      - id: clang-format
        name: clang-format
        language: system
        entry: clang-format
        args: [-i]
        files: \.(cpp|h|mm)$
```

This runs the system `clang-format` (must be on `PATH`) in-place on all staged `.cpp`, `.h`, and `.mm` files before the commit is recorded. If any file is reformatted, the commit is aborted and the reformatted files are left staged-and-modified — just `git add` them and commit again.

**Setup (first time / new machine):**

```bash
brew install pre-commit          # macOS
pre-commit install               # installs the hook into .git/hooks/pre-commit
```

After `pre-commit install` the hook is active for all future commits in that clone. CI does not rely on the hook — it is purely a local convenience.

**Why this matters for agents:** Sub-agents that write code and commit must ensure `clang-format` is satisfied before committing, otherwise the pre-commit hook aborts the commit. The safe pattern: always run `clang-format -i` on touched files as the last step before `git commit`.

---

### Parallel Agent Integration — Code Move + Modify Conflict

When running multiple agents in parallel via `isolation: "worktree"`, each agent gets its own copy of the repo. A subtle but powerful integration scenario:

**The situation:** Two agents ran in parallel on a refactor batch. Agent A moved a block of code to a different module. Agent B independently modified that same code at its original location (pre-move). Neither agent knew what the other was doing.

**What the main agent did:** After reviewing all 7 agent outputs, it applied Agent A's move first, then — rather than rejecting Agent B's change as conflicting — recognised that B's modification needed to land in the *new* location and transplanted it there automatically.

**Why this works well:** The integration step is where the main agent earns its keep. With a broad view of all diffs, it can reason about intent rather than just applying patches mechanically. The worktree isolation means each sub-agent produces a clean, self-consistent diff that's easy to reason about independently.

**Takeaway:** Don't be afraid to assign overlapping or potentially conflicting work to parallel agents. The main agent can integrate intelligently as long as the individual outputs are coherent. The worst case is a manual merge step; the best case is fully automatic integration even across structural changes.

---

### Debug logging is cheap to add and remove — don't leave it at INFO

With AI-assisted development, adding or removing diagnostic log statements is trivial. There is no reason to keep verbose debug spew at `INFO` level "just in case."

What happened:
- split pane work added `INFO`-level logs for `apply_grid_size`, `set_viewport`, `MetalGridHandle` operations, and font loading
- these were useful during development but cluttered the default output once the feature was stable
- removing them was a single pass across a few files

Rule:
- use `DRAXUL_LOG_DEBUG` for diagnostic/tracing messages (grid sizes, viewport changes, font loading, renderer state)
- reserve `DRAXUL_LOG_INFO` and above for messages that are actionable at runtime (errors, warnings about missing resources, startup milestones)
- when in doubt, use DEBUG — it's seconds of work to promote a message to INFO later if it turns out to be important

---

## Fonts, Emoji, and Fallbacks

### Windows color emoji needs an end-to-end color glyph path

Monochrome emoji was not just a font-selection problem. Full color on Windows required the whole pipeline to support color glyphs.

What had to be true end to end:
- FreeType color glyph loading had to preserve `BGRA` bitmap data
- the glyph cache had to keep RGBA pixels instead of collapsing everything to alpha
- atlas uploads had to support RGBA, not just monochrome glyph masks
- shaders had to treat color glyphs differently and avoid tinting them with the cell foreground color

Font selection also mattered:
- on Windows, `seguiemj.ttf` should be preferred ahead of `seguisym.ttf` for emoji-like clusters
- if the fallback picker resolves to a monochrome symbol font first, you still get black-and-white emoji even if the renderer can handle color glyphs

Lesson:
- color emoji is a pipeline feature, not a single switch

---

### CJK tofu on Windows was mostly a fallback-font coverage problem

The "Wide" line rendering as tofu was fixed by broadening the default Windows fallback list, not by changing shaping logic.

Useful fallback candidates:
- `YuGoth*`
- `meiryo`
- `msgothic`
- `msyh`
- `simsun`

Lesson:
- if wide/CJK text is tofu while Latin and emoji work, the first thing to check is fallback coverage, not HarfBuzz

---

### Bundled fonts and installed fonts can diverge in important ways

We hit this earlier with newer Nerd Font icons: the repo-bundled font lacked glyphs that were present in the installed system version.

Practical pattern that worked:
- prefer a current installed JetBrains Nerd Font if present
- keep a known-good bundled fallback in the repo
- update the bundled copy when the system-installed version proves materially better

Lesson:
- a stale bundled font can make the app look broken even when the user's machine actually has the right glyphs

---

### Claude can generate architecture diagrams as SVG on demand

Asking Claude to "draw an SVG diagram of this application showing how things are inherited/linked" produces a usable class hierarchy and data flow diagram directly in the repo.

What worked:
- Claude explored the full codebase (renderer, host, window, font, grid, app layers), mapped all inheritance and composition relationships, and generated a single self-contained SVG
- the output uses color-coded boxes (interfaces, abstract bases, concrete classes, orchestrators, support types), hollow arrows for inheritance, dashed arrows for composition
- includes a data flow section at the bottom showing both the nvim and terminal paths

Why this is useful:
- the diagram lives in `docs/architecture.svg` and can be opened in any browser
- it updates in minutes whenever the architecture changes — just re-run the prompt
- the stored prompt is in `plans/prompts/architecture_diagram.md` for repeatable generation

Lesson:
- SVG architecture diagrams are cheap to regenerate — treat them as living documents, not one-off artifacts

---

## City Grid Rasterization

### Three bugs in the 2D occupancy grid, and how to debug rasterization visually

After placing city blocks in the MegaCity semantic layout, a 2D occupancy grid is built on a background thread. Each cell is marked as building (1), sidewalk (2), road (3), or empty (0). An ImGui "City Map" panel draws this grid as a colored overview. Three bugs made the grid look wrong; all were found by comparing the 2D grid panel against the 3D city view side by side.

**Screenshot**: See `docs/learnings/images/grid_plan_view.png` — the 2D grid (left) vs the 3D city (right) after bugs 1 and 2 were fixed but bug 3 was still present. The large central building (MegaCityHost) is missing from the grid, and sidewalks are fragmented.

#### Bug 1: Doubled road/sidewalk extents

`RoadSegmentPlacement::extent` stores **full** widths (not half-extents). The renderer confirms this — it passes `extent_x` directly to `glm::scale()` which scales a unit cube `[-0.5, +0.5]`. But `fill_rect` was using `center - extent` to `center + extent`, effectively doubling every road and sidewalk segment. Fix: use `center - extent * 0.5f`.

**How I figured it out**: Traced how `RoadMetrics::extent_x` is used in `build_scene_snapshot()` — the scale applies to a unit cube, so `extent_x` is the full width, not half.

#### Bug 2: Off-by-one at snapped cell boundaries

All geometry is snapped to `placement_step` (0.5), so rect edges land exactly on cell boundaries. Using `floor()` for both min and max edges includes one extra cell at the max edge. Example: building covering `[−0.5, 1.5]` with origin `−1.0`, step `0.5` → `c1 = floor(5.0) = 5`, but cell 5 starts at world x=1.5 — the building only touches the boundary. Fix: apply a tiny epsilon inset (`step * 0.01`) on max edges so `floor` doesn't include the boundary cell.

**How I figured it out**: Worked through concrete arithmetic with snapped values.

#### Bug 3: Draw order — per-building vs per-layer passes

Buildings were processed one at a time (roads → sidewalks → footprint per building). When building B is processed after building A, B's road pass overwrites A's already-drawn footprint. The large MegaCityHost building was drawn first (highest connectivity), then neighboring buildings' road passes overwrote it.

Fix: three separate global passes — all roads, then all sidewalks, then all building footprints — so higher-priority layers always win regardless of building order.

**How I figured it out**: The screenshot was the key clue. The pattern of "big building missing, small ones present" pointed directly to a draw-order dependency where later buildings' lower-priority layers (roads) clobbered earlier buildings' higher-priority layers (footprints).

**Key lesson**: When rasterizing layered geometry with overlapping extents, always separate passes by layer priority rather than by entity. Per-entity layering only works if entities don't overlap, but city lots are deliberately placed in edge contact with shared road corridors.

---

## Procedural Geometry Prompts

### Procedural building generator prompt

Store the original prompt that drove the procedural building-shell work:

> lets add a procedural building to the geometry static library. We already know what a building is; it's a set of 'slices', each of which has a given height, based on the semantic information. I want to build a building based of that semantic information, instead of the stacked cubes we hvae now. Much like the tree, we will start with a ring at the base - typically 4 vertices to make a rectangluar shape. We will step up to the next fractional height by building '3' more rings; Yes - each heigh section will effectively be 3 quads. But that's basically it - we walk to the roof of the building, adding rings of quads. Effectively 3 rings per level: the semantic map should contain the array of level information. Obviously we calculate the correct tangent and normal information. There are no 'inner' floors; the building is now an outer ring of default 4 vertices. It will look the same as now, but without the cube stacking/inner quads. Since each 'level' of the building is now represented by 3 quad strips, we may push that inner quad in or out - lets have a factor for that, but not change it yet. The basic 4 ring floor size will divide evenly too - so if I said 5 I would get a pentagonal building, etc. You can think of this is a 'tube with 'n' sides', divided into the usual alternating floor colors; with each floor having that extra middle strip. Obviously this now means that each building has unique geoemtry (if it doesn't already). Lets build and try it
