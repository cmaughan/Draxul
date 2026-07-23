# App shell layout and root sidebar splitter

**Status:** implemented
**Date:** 2026-07-23
**Related:** [Herdr agent-harness research](herdr-agent-harness-research.md)

## Implementation result

Implemented in July 2026. `App` now computes and publishes one `AppShellLayout` for
chrome paint, chrome hit-testing, and every Space's pane root. The Space divider is
draggable with cell snapping, its preferred width is persisted as
`space_sidebar_columns`, and Space navigation rows use full cell height.

## Goal

Give Draxul one explicit application-level layout model for its persistent chrome and
content regions, then make the Spaces/sidebar boundary a real draggable splitter.

The resulting hierarchy is:

```text
AppShellLayout
|- work area
|  |- Spaces sidebar
|  |- root divider
|  `- content
|     |- tab bar
|     `- active tab's PaneManager / SplitTree
`- diagnostics panel

Full-window overlays (command palette and toasts) remain outside layout flow.
```

This does **not** move application chrome into a tab's pane tree. Each `Tab` continues
to own one independent `PaneManager` and `SplitTree`; the shell layout supplies the
root rectangle within which those pane trees compute their leaves.

## Current problem

Draxul currently has no authoritative application layout object:

- `RenderNode` controls draw/pump order, not geometry.
- `ChromeHost` derives the tab-bar height and Spaces sidebar width.
- `App` repeatedly subtracts those values before recomputing pane viewports.
- `DiagnosticsPanelHost` separately exposes the remaining terminal height.
- `InputDispatcher` asks `ChromeHost` for chrome dimensions and owns separate pane
  divider-drag logic.
- `ChromeHost` draws a one-pixel sidebar boundary, but that line is not hit-testable or
  draggable.

This duplicates layout arithmetic across initialization, resize, font changes, panel
toggle, tab and Space lifecycle, and session restore. It also makes layout ownership
look inverted: `ChromeHost` appears to manage layout while `App` actually assigns all
host viewports.

The Space-label overflow has the same root cause. NanoVG draws a rounded background
inset within a full grid row, then a separate full-height grid handle draws the text
without per-row clipping.

## Decisions

### One shell layout, separate pane trees

`App` owns the current `AppShellLayout`. `ChromeHost`, `InputDispatcher`, and
all `TabController` instances consume it.

The per-tab `SplitTree` remains the owner of:

- pane leaf and divider IDs;
- nested horizontal/vertical pane structure;
- pane focus;
- pane split ratios;
- pane-layout snapshots;
- cell-snapped pane-divider drag.

The shell layout owns only:

- work-area and diagnostics rectangles;
- Spaces sidebar visibility and rectangle;
- the root sidebar-divider rectangle;
- the content rectangle;
- the tab-bar rectangle;
- the pane-root rectangle.

### Do not reuse `SplitTree` directly

Using a two-leaf `SplitTree` at the application root would import pane-specific
semantics that do not apply to chrome: focus, leaf identity, close/swap/zoom commands,
host lifecycle, and pane persistence.

The first implementation should use a small purpose-built shell layout. If Draxul
later gains independently resizable right inspectors, bottom consoles, or dockable
regions, the neutral geometry/ratio portion of `SplitTree` can then be extracted into
a reusable layout primitive.

### Sidebar size is a UI preference

Persist the preferred width as a column count, not pixels, so it remains stable across
font-size and DPI changes:

```toml
space_sidebar_columns = 20
```

The value belongs to `AppConfig`, not the session snapshot. It applies across sessions
and does not require the multi-Space persistence-v2 format.

Initial constraints:

- default: 20 columns;
- minimum sidebar: 12 columns;
- maximum sidebar: 48 columns;
- minimum content: 20 columns;
- drag snaps to the current grid cell width;
- one Space: sidebar and root divider are absent, but the preference is retained;
- zoomed pane: preserve current behaviour by hiding app chrome and letting the pane
  occupy the full work area;
- diagnostics: full-width bottom region, outside the work area;
- command palette and toasts: full-window overlays, unaffected by the split.

### Sidebar entries are navigation rows

Use full-cell-height selection rows instead of text inside vertically inset rounded
buttons. The active row can retain a background/accent treatment, but its paint bounds
must contain the full text cell.

This removes the current glyph overflow without creating one grid handle or render
pass per Space.

## Proposed API

Add a pure layout module:

```cpp
// app/app_shell_layout.h

struct AppShellRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct AppShellLayoutInput
{
    int window_width = 0;
    int window_height = 0;
    int terminal_height = 0;       // space above the diagnostics panel
    int cell_width = 0;
    int cell_height = 0;
    int preferred_sidebar_columns = 20;
    bool show_sidebar = false;
    bool show_tab_bar = true;
    bool zoomed = false;
};

struct AppShellLayout
{
    AppShellRect window;
    AppShellRect work_area;
    AppShellRect sidebar;
    AppShellRect sidebar_divider;
    AppShellRect content;
    AppShellRect tab_bar;
    AppShellRect pane_root;
    AppShellRect diagnostics;
    int effective_sidebar_columns = 0;
    bool sidebar_visible = false;
    bool chrome_visible = true;
};

AppShellLayout compute_app_shell_layout(const AppShellLayoutInput& input);
```

The pure function clamps all dimensions and conserves pixels:

```text
work_area.w
  = sidebar.w + sidebar_divider.w + content.w

content.h
  = tab_bar.h + pane_root.h

window.h
  = work_area.h + diagnostics.h
```

`App` stores:

```cpp
AppShellLayout shell_layout_;
bool dragging_shell_divider_ = false;
```

The exact drag state may live in `InputDispatcher`, but the preferred column count and
recomputed layout remain owned by `App`.

## Phased implementation

### Phase 1: pure geometry and regression boundary

Add `app/app_shell_layout.h/.cpp` and `tests/app_shell_layout_tests.cpp`.

Pin the current and intended cases:

1. One Space: no sidebar or divider; content starts at x=0.
2. Multiple Spaces: sidebar, divider, and content exactly conserve width.
3. Narrow window: sidebar shrinks before content crosses its minimum.
4. Font/DPI change: the same preferred column count produces the new pixel width.
5. Diagnostics visible: work area ends where the diagnostics rectangle begins.
6. Zero/tiny dimensions: all rectangles remain non-negative.
7. Zoom: chrome rectangles disappear and pane root fills the work area.

No host, renderer, window, or `SpaceController` dependency belongs in this module.

**Exit condition:** shell geometry is deterministic and device-free, with no runtime
behaviour changed.

### Phase 2: make `App` the single geometry owner

Add one `App::refresh_app_shell_layout()` transaction that:

1. reads the current window, diagnostics, cell metrics, Space count, and zoom state;
2. computes `shell_layout_`;
3. recomputes every Space/tab `PaneManager` from `shell_layout_.pane_root`;
4. gives `ChromeHost` the new shell layout;
5. updates input hit-test geometry;
6. requests a frame.

Replace scattered combinations of:

```cpp
chrome_host_->tab_bar_height();
chrome_host_->space_sidebar_width();
diagnostics_host_->layout().terminal_height;
recompute_all_viewports(...);
```

with that transaction at these state boundaries:

- initialization and restored-session initialization;
- window resize;
- font/DPI change;
- diagnostics-panel toggle;
- new/closed/switched Space;
- new/closed/switched tab where zoom/chrome state may change;
- session load and rollback;
- zoom enter/exit.

`TabController::recompute_all_viewports()` remains the fan-out into per-tab pane trees,
but receives the already-final pane-root rectangle.

Do not make `ChromeHost` recompute shell geometry from its viewport. This avoids stale
width during resize and establishes one source of truth.

**Exit condition:** all pane and chrome geometry originates in `shell_layout_`, while
the rendered result remains equivalent except for the explicit divider width.

### Phase 3: make the root divider interactive

Add root-divider hit-testing before pane-divider hit-testing:

- left-button press over `shell_layout_.sidebar_divider` captures the root divider;
- mouse motion converts physical x to a preferred sidebar column count;
- the count is clamped, snapped, and passed to `refresh_app_shell_layout()`;
- button release ends capture and restores the normal cursor;
- while captured, events do not reach the active pane;
- hover uses the horizontal-resize cursor;
- dragging remains active if the pointer leaves the divider rectangle.

Keep root and pane divider state distinct. A root-divider drag must never produce a
`DividerId`, and pane-divider commands must never resize the sidebar.

Persist the resulting `space_sidebar_columns` through the existing config-save path.
Reloading config applies the new preference and recomputes the shell immediately.

**Exit condition:** the Spaces rail is mouse-resizable, cell-snapped, constrained, and
restored on the next launch.

### Phase 4: make `ChromeHost` a shell renderer, not a layout owner

Update Chrome input/layout to consume the shell rectangles:

- sidebar background and Space rows use `shell_layout_.sidebar`;
- root divider uses `shell_layout_.sidebar_divider`;
- tab bar uses `shell_layout_.tab_bar`;
- pane dividers, focus borders, and status pills continue to come from the active
  tab's `SplitTree`;
- sidebar hit regions derive from the same row geometry used for paint;
- the sidebar text grid viewport matches the sidebar rectangle exactly.

Replace inset rounded Space buttons with full-height navigation rows. Add explicit
horizontal padding and truncate labels from the actual available row columns.

Remove:

- `ChromeHost::space_sidebar_width()`;
- `ChromeHost::tab_bar_height()` as an application layout authority;
- sidebar subtraction inside `App::recompute_all_viewports()`;
- input-router width queries that can instead use the shell layout;
- the misleading `ChromeHost is the central layout manager` comment.

`ChromeHost` remains responsible for painting and chrome-specific hit regions, not
allocating application rectangles.

**Exit condition:** Chrome paint, chrome hit-testing, and pane roots all agree with the
same shell-layout snapshot, and Space labels remain inside their rows.

### Phase 5: validation and documentation

Add or update:

- pure shell-layout tests;
- root-divider input-routing and capture tests;
- app smoke coverage for Space-count transitions and layout recomputation;
- config parse/serialize/round-trip tests for `space_sidebar_columns`;
- Chrome layout tests for supplied shell rectangles and row truncation;
- a deterministic multi-Space Chrome render/snapshot scenario to cover the sidebar
  visually;
- `docs/features.md` configuration and interaction documentation;
- `docs/module-map.md` if the new layout module changes the documented app graph.

Validation gates:

```powershell
cmake --build build --target draxul draxul-tests --config Debug
ctest --test-dir build -C Debug --output-on-failure
py do.py smoke
```

Run the render snapshot suite because the work changes chrome geometry. Windows and
macOS use the same shell, NanoVG, and grid paths, but both platform CI jobs must pass.

## Invariants

- A `SplitTree` still belongs to exactly one `Tab`.
- Space switching never terminates or reconstructs inactive pane trees.
- The root splitter has no pane `LeafId` or `DividerId`.
- App-shell preference is not stored in session topology.
- One-Space startup preserves the existing full-width pane geometry.
- Zoom preserves its existing full-work-area behaviour.
- Overlays remain full-window and do not influence pane geometry.
- All rectangles are physical-pixel geometry; logical mouse coordinates are converted
  once before hit-testing.
- A layout refresh updates every Space so an inactive Space is correct immediately
  when activated.

## Risks and controls

| Risk | Control |
|---|---|
| Geometry drifts between paint, hit-test, and host viewports | Store and share one `AppShellLayout`. |
| Resize uses a stale `ChromeHost` viewport | Compute from explicit current window dimensions in `App`. |
| Root drag conflicts with pane drag | Separate capture state and prioritize root-divider hit-testing. |
| Font change silently changes saved pixel width | Persist columns and derive pixels from current cell width. |
| Narrow windows starve the pane area | Clamp sidebar against a minimum content width. |
| Zoom leaves invisible chrome space reserved | Make zoom an explicit shell-layout input and test it. |
| Multi-Space layout changes break inactive tabs | Recompute all Spaces from the same pane-root rectangle. |
| The refactor expands `App` further | Keep geometry pure and move computations into `app_shell_layout.*`; `App` only coordinates state changes. |

## Non-goals

- No generic docking system.
- No sidebar placement on the right or bottom.
- No nested application-level panels beyond the current diagnostics region.
- No change to Space, tab, or pane lifecycle.
- No session persistence-v2 work.
- No agent list or agent-status UI.
- No change to `RenderNode` draw/pump semantics.

## Completion criteria

- Draxul has one explicit, inspectable application shell layout.
- The Spaces sidebar is separated from content by a real draggable splitter.
- Space labels do not paint outside their navigation rows.
- `ChromeHost` consumes layout instead of owning application dimensions.
- All per-tab pane trees continue to split, focus, restore, zoom, and drag exactly as
  before inside the supplied pane-root rectangle.
- Single-Space render references remain unchanged unless an intentional root-divider
  pixel allocation requires a reviewed update.
