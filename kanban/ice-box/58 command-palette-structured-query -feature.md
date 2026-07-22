# Structured command-palette queries

**Type:** feature
**Priority:** 58
**Raised by:** GPT/Codex, Gemini

## User need

Filter large palettes by category and complete typed arguments such as host, tab, pane, session, or path without memorizing exact command syntax.

## Implementation plan

- [ ] Extend action/provider metadata with category, aliases, argument schema, description, and an injected completion provider; keep zero-argument actions source-compatible.
- [ ] Parse optional filters such as `hosts:`, `panes:`, `sessions:`, and `settings:` into a pure query model before fuzzy scoring.
- [ ] Add a two-stage palette state: select command, then edit/complete typed arguments with breadcrumb, validation, and Escape/back navigation.
- [ ] Implement bounded asynchronous filesystem completion and synchronous completions for provider kinds, tabs, panes, and sessions.
- [ ] Reuse UTF-8-safe editing/display helpers and never run a command until all required arguments validate and the user confirms.
- [ ] Let existing features such as split-with-host, pane move, and template selection register completion providers rather than adding palette-specific branches.
- [ ] Preserve current fuzzy matching for ordinary free-text queries and expose category/argument syntax in help.

## Tests and acceptance

- [ ] Test parser ambiguity, unknown prefixes, Unicode, empty values, quoting/escaped spaces, long queries, completion cancellation, stale async results, and missing providers.
- [ ] Existing action order/selection/dispatch tests remain compatible for unstructured queries.
- [ ] Every displayed completion maps to one typed value; invalid arguments cannot dispatch.
- [ ] Filesystem completion is bounded, cancellable, and does not block the main thread.

## Dependencies and parallelism

Benefits from pending 12 provider metadata and 29 GUI contracts. It becomes shared infrastructure for 50, 55, and 69; assign one palette owner before parallel features register completions.

<model>GPT-5 Codex</model>
