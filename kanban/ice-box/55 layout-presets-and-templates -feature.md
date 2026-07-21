# Layout presets and named templates

**Type:** feature
**Priority:** 55
**Raised by:** GPT/Codex, Gemini

## User need

Create balanced rows, columns, grids, and main-plus-stack layouts quickly, then save a useful topology as a named reusable template.

## Implementation plan

- [ ] Define a versioned `LayoutTemplate` using the session split-tree/host launch descriptor types, with no live process state.
- [ ] Add pure builders for balanced rows, columns, grids, and main-plus-stack that respect minimum pane dimensions and deterministic leaf order.
- [ ] Add `WorkspaceController::apply_layout_template()` as a transaction with preview/confirmation when existing panes would be closed or relaunched.
- [ ] Support two scopes: rearrange existing panes into a preset without restart, and instantiate a saved template with new hosts.
- [ ] Persist named templates in the config directory through atomic storage; validate provider availability and paths before mutation.
- [ ] Add palette commands for applying presets, saving the current workspace, renaming/deleting templates, and creating a workspace from a template.
- [ ] Version and migrate descriptors conservatively; unknown future fields should survive round trips where practical.

## Tests and acceptance

- [ ] Property-test leaf count, unique IDs, ratios, minimum sizes, and deterministic topology over varying pane counts/dimensions.
- [ ] Round-trip named templates with mixed hosts and Unicode names.
- [ ] Inject unavailable provider, invalid source, storage failure, and partial host initialization; source workspace remains intact.
- [ ] Existing panes retain identity in rearrange mode; instantiate mode creates distinct hosts.

## Dependencies and parallelism

Depends on the `WorkspaceController` refactor (`ice-box/22 app-workspace-session-controllers -refactor.md`) and its host/session-state prerequisites — all currently deferred to the ice box, so this card is blocked until they are scheduled. Share descriptor/storage types with duplicate workspace (51). Preset math can be developed independently after the controller transaction contract is fixed.

<model>GPT-5 Codex</model>
