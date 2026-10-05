# Preserve source-path characters through launch

**Summary:** Read file names in their original characters so Windows can open Markdown files with non-English names.

**Priority:** 81  
**Severity:** HIGH  
**Source:** `libs/draxul-app-cli/src/cli_args.cpp`

**Evidence and trigger:** B18; UTF-8 arguments undergo narrow Windows path conversion in CLI parsing and Markdown source resolution.

- [x] **Investigate:** Trace source and working-directory decoding and serialization through CLI and host launch.
- [x] **Fix:** Use explicit UTF-8 boundaries while retaining native filesystem paths.
- [x] **Acceptance:** Absolute and relative non-English source paths open on Windows with a non-UTF-8 code page; ordinary paths remain valid.
- [x] **Validation:** Run core aggregate tests and same-cache smoke; verify the Windows path scenario.

## Resolution (2026-10-05)

- **Cause:** Windows `main` converts argv to UTF-8, but `cli_args.cpp` built
  `std::filesystem::path` from those strings and later code serialized paths with
  `path::string()`. Both use the active code page on Windows, so non-English
  source paths were garbled or threw. Topology mutations, the Markdown preview
  route, Markdown `open_file:`/`reload`, and pane environment values had the same
  narrow boundary. Kanban (card 86) already emits UTF-8 and depended on this.
- **Fix:** `path_from_utf8()` / `path_to_utf8()` in
  `libs/draxul-types/include/draxul/filesystem_path_text.h` mark every UTF-8
  boundary: CLI path options, `main.cpp` launch/topology values,
  `topology_mutation_route.cpp`, `App::show_markdown_preview`,
  `PaneManager` runtime/executable environment, and `MarkdownHost` source and
  working-directory resolution, error text, status, title, and reload.
  Native `std::filesystem::path` values are kept for file access.
- **Tests:** `cli: path arguments preserve non-English UTF-8 names` and
  `markdown host opens non-English source paths from UTF-8 launch text`
  (absolute, relative to a non-English working directory, `open_file:`, reload).
  On macOS the native path encoding is already UTF-8, so these guard the Windows
  code-page path when CI runs them there.
- **Validation (macOS Debug):** focused `[unicode]` cases 2/2; core aggregate
  56/56 (`draxul-do-py-tests` after initializing the Megacity submodule in the
  worktree); same-cache smoke passed; Release `--smoke-test` startup run.
