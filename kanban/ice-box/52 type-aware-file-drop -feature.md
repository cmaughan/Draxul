# Type-aware file-drop routing

**Type:** feature
**Priority:** 52
**Raised by:** GPT/Codex, Claude

## User need

Dropping a file should open it in an appropriate Draxul host instead of blindly sending `open_file:` to whichever pane happens to be focused.

## Implementation plan

- [ ] Introduce a pure `DropRouter` below `App` that classifies normalized paths and
      queries providers without opening untrusted content.
- [ ] Extend the delivered provider metadata with supported extensions/drop kinds and a
      default open action, including discovered native plugins.
- [ ] Route to the provider selected by metadata rather than hard-coding product IDs.
- [ ] For a focused shell, offer/define a safe “insert path” behavior using platform/shell-aware quoting and bracketed paste; never execute the path automatically.
- [ ] Define modifier behavior (for example, hold a modifier to force focused-host delivery) and a chooser for ambiguous/unknown types.
- [ ] Use `TabController`/PaneManager APIs to split or reuse panes according to one documented policy; do not add host-specific branches to `InputDispatcher`.
- [ ] Preserve existing `open_file:` encoding for direct host delivery and handle multiple dropped paths in stable order with a reasonable count limit.

## Tests and acceptance

- [ ] Table-test extensions, case, directories, Unicode, spaces, quotes, shell metacharacters, Windows drive/UNC paths, missing paths, and multiple drops.
- [ ] Test absent optional plugins, Markdown-disabled builds, and chooser fallback.
- [ ] Prove shell insertion round-trips as one literal path for Bash, Zsh, PowerShell, and Windows command quoting policy.
- [ ] Host routing is deterministic, never executes a dropped path, and does not lose the existing focused-host override.

## Dependencies and parallelism

Depends on the remaining projection work in
`kanban/ice-box/22 app-tab-session-controllers -refactor.md`. Untrusted `.mxl`
validation is product-owned in
`plugins/scoreview/kanban/ice-box/18 hostile-mxl-inputs -test.md`. Reuse the provider
picker delivered by `kanban/done/69 split-with-host-launcher -feature.md`.

<model>GPT-5 Codex</model>
