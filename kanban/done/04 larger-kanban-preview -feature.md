# Give Kanban previews more space and smaller text

Requested 2026-09-29: the pinned card preview should occupy two-thirds of
the original Kanban pane height and default to text two points smaller.

- [x] Reverse the board/preview split in local and shared topology.
- [x] Reduce companion preview text by two points, preserving standalone
  Markdown settings and the minimum supported font size.
- [x] Update documentation and validate sizing, font reload/reset, and startup.

Existing manually resized splits retain their geometry until the preview is
closed and reopened. Track validation evidence and remaining gates here.

## Implementation and validation

- `App::show_markdown_preview` now gives the owner one-third in both local
  and server-backed split requests. Existing splits are intentionally retained;
  close/reopen the preview to apply the new default geometry.
- Local previews now carry the same companion-owner descriptor as shared
  previews. Markdown uses that descriptor to subtract 2pt, clamped at 6pt.
  Config reload and font reset retain the offset. Preview zoom is local so it
  cannot persist a reduced global Markdown size and shrink again on reopening.
- Updated the existing pane geometry coverage and added host coverage for
  initialization, zoom/reset, source reload, config reload, minimum size, and
  unchanged standalone Markdown font settings.
- Release build succeeded in the existing cache (35.8s build lease; no separate
  configure/generate step reported). One core aggregate: 24/25 CTest entries
  passed in 33.54s, including Markdown and App/pane-manager tests. The unchanged
  Kanban refocus failure remains tracked in
  `kanban/pending/03 kanban-focus-preview-refresh-regression -bug.md`.
- Same-cache Release startup smoke passed. Markdown render export passed and
  was visually inspected. An isolated visible Kanban render run toggled `p`
  and exported `/tmp/draxul-kanban-preview-check.png`: board occupies the top
  third, the smaller-text preview occupies the bottom two-thirds. No live
  server or user session was restarted. Render capture took about 7 seconds.
- The diagnostics-panel CTest initially failed at 19.7081% drift versus 18%
  tolerance (1.21s); this snapshot includes live timing counters. A recheck
  after the aggregate finished is recorded below. No reference was blessed.
- No focused unit rerun or remote Windows CI was run. Shared C++ implementation
  is platform-neutral; live visual validation was on macOS/Metal.
- Panel recheck also failed (19.799%, 1.19s). This independent snapshot
  investigation is transferred to
  `kanban/pending/05 diagnostics-panel-snapshot-drift -test.md`; do not treat
  the full validation inventory as green. The requested preview behavior was
  verified directly and its host/layout coverage passed.
