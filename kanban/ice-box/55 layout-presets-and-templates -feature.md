# Built-in layout presets and named-template UX

**Type:** feature
**Priority:** 55
**Raised by:** GPT/Codex, Gemini

## User need

Create balanced rows, columns, grids, and main-plus-stack layouts quickly,
then save or export a useful current topology as a named reusable template
through the GUI.

## Delivered boundary

Commit `cc66d5d` delivered declarative layout JSON validation and atomic apply:
files can describe Spaces, tabs, panes, aliases, split directions, and ratios;
validation is non-mutating; apply allocates the complete Space transactionally
and rolls back failed allocation. This card reuses that format and operation.
It does not add a second layout parser, validator, apply transaction, or
rollback mechanism.

## Implementation plan

- [ ] Add pure builders for balanced rows, columns, grids, and
      main-plus-stack layouts that respect minimum pane dimensions and produce
      deterministic pane order and ratios.
- [ ] Add a UI transaction for rearranging the current tab's existing panes
      into a built-in preset without restarting their hosts.
- [ ] Add export of the current tab or selected Space to the delivered
      declarative layout JSON format, excluding live process and device state.
- [ ] Persist named templates in the config directory with atomic file
      replacement and a small index containing display name and source path.
- [ ] Validate exported and stored templates through the delivered layout
      validator before publishing them.
- [ ] Add palette/GUI flows for previewing and applying a built-in preset,
      saving/exporting the current layout, creating from a named template, and
      renaming or deleting templates.
- [ ] Show provider/path validation failures before mutation and use the
      delivered atomic apply path when a template instantiates new hosts.
- [ ] Preserve unknown future declarative-layout fields when renaming or
      reindexing a stored template where practical.

## Tests and acceptance

- [ ] Property-test leaf count, unique IDs, ratios, minimum sizes, and
      deterministic topology for every built-in preset over varied pane counts
      and dimensions.
- [ ] Rearranging existing panes preserves pane IDs, host/runtime identity,
      focus, and session round-trip state.
- [ ] Export the current layout, validate it with the existing validator,
      apply it through the existing declarative API, and verify an equivalent
      topology with distinct new host identities.
- [ ] Round-trip named templates containing mixed hosts and Unicode names;
      cover rename, delete, missing provider/path, corrupt index, and atomic
      storage failure.
- [ ] Palette and GUI cancellation leave the current topology and template
      store unchanged.

## Dependencies and parallelism

The declarative layout format, validation/apply transaction, `TabController`,
and session descriptor boundaries are available. Share export and named-store
helpers with duplicate tab (51) rather than introducing another serialized
layout type. Preset math can be developed independently once the rearrange
transaction contract is fixed.

<model>GPT-5 Codex</model>
