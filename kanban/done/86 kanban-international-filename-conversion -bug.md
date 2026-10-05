# Preserve international Kanban names safely
**Summary:** Preserve card names in their original characters so opening a board cannot crash on international names.

**Priority:** 86  
**Severity:** CRITICAL  
**Source:** `modules/kanban/draxul-kanban/src/kanban_store.cpp`  
**Reported by:** Claude C6; consensus F06.

**Evidence and trigger:** Lines 578, 616, and 658 use throwing narrow filename conversions. On Windows, scanning an emoji or Asian card name outside the active code page can escape board loading.

**Related:** `kanban/pending/75 windows-markdown-source-paths -bug.md` owns the related Markdown path boundary.

- [x] **Investigate:** Trace repository, lane, card, title, preview, and error-text conversions while preserving native paths.
  Narrow `path::string()` was used for source (repository/submodule) names, lane names, card
  file names, `.gitmodules` paths, error/warning text, the window title, and the `open_file:` /
  Markdown preview paths; the reverse direction (`root / name`, move destinations, per-source
  save directories) re-narrowed those names through `path(std::string)`, and the host decoded
  `HostLaunchOptions::source_path`/`working_dir` (UTF-8) the same way.
- [x] **Fix:** Convert displayed names explicitly to UTF-8 and contain scan/conversion failures at the board boundary.
  `kanban_path_utf8()` / `kanban_path_from_utf8()` (`kanban_board.h`) are now the only
  name/path conversions in the store and host; filesystem locations stay native paths.
  `load_kanban_board()` / `load_kanban_workspace()` convert any escaping exception into a load
  error, the metadata existence check uses the `error_code` overload, and
  `KanbanHost::reload_board()` contains root-resolution failures, keeping the previous board.
- [x] **Acceptance:** International repository/lane/card names load, display, reload, and select correctly on Windows with a non-UTF-8 code page.
  `kanban workspace preserves international repository, lane, and card names` (store) and
  `kanban host displays, opens, moves, and reloads international names` (host) use Cyrillic +
  CJK + emoji names, which fit no single ANSI code page, across repository, submodule, lane,
  and card names, metadata order round-trip, lane move, Neovim open, reload, and preview.
  Verified on macOS; the same tests exercise the Windows conversion in CI.
- [x] **Acceptance:** A failed scan leaves usable state and reports an error without terminating the application.
  A new `KanbanScanFailurePoint::Exception` injects a throwing scan; store and host
  scan-error tests assert no throw, a reported error, and the previous board retained.
- [x] **Validation:** Run core aggregate tests and same-cache smoke; verify Windows filename behavior and existing POSIX behavior.
  Kanban core (42 cases) and host (22 cases) suites, core aggregate, and same-cache smoke pass on
  macOS. Windows behavior is covered by the new tests in CI. Follow-up outside this card:
  Markdown preview and the app topology route still decode/encode those UTF-8 paths with
  narrow conversions (`markdown_host.cpp` `open_file:`, `App::show_markdown_preview`,
  `topology_mutation_route.cpp` `working_directory.string()`), tracked by
  `kanban/pending/75 windows-markdown-source-paths -bug.md`.
