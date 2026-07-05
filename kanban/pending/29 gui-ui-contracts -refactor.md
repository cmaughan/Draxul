# Clarify GUI/UI contracts and UiPanel lifecycle

**Type:** refactor
**Priority:** 29
**Raised by:** Claude

## Goal

Make the valid `draxul-gui` versus `draxul-ui` split understandable and remove overlapping names/lifecycle APIs: cell-grid native overlays versus ImGui diagnostics.

## Implementation plan

- [ ] Add short READMEs/target comments defining ownership, dependencies, and when code belongs in each library.
- [ ] Rename the private palette `PanelLayout` to a domain-specific name; avoid broad public renames initially.
- [ ] Choose one canonical `UiPanel` frame-driving API (`render` or explicit begin/end) and migrate all callers.
- [ ] Replace undocumented raw `IImGuiHost*` lifetime with an explicit attach/detach token or reference contract.
- [ ] Separate layout/state from ImGui rendering where pure tests add value.
- [ ] Remove dead/overlapping entry points only after all callers and tests migrate.
- [ ] Update architecture docs when the contract is final.

## Tests and acceptance

- [ ] Existing diagnostics/panel layout/input-capture tests cover the canonical API.
- [ ] Attach/detach/shutdown order is tested with a fake backend.
- [ ] A contributor can determine the correct library from the docs and dependency direction.
- [ ] No visual behavior change; full tests/render snapshots/smoke pass.

## Dependencies and parallelism

Item 08 should land first. This can run separately from Chrome once shared overlay helpers are stable.

<model>GPT-5 Codex</model>
