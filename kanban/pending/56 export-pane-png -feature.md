# Export focused pane as PNG

**Type:** feature
**Priority:** 56
**Raised by:** GPT/Codex

## User need

Save the focused pane as a portable PNG without invoking printing or capturing unrelated Chrome/workspaces.

## Implementation plan

- [ ] Build on the request/deadline/crop controller from item 14; capture the pane rectangle and identity at action time.
- [ ] Add a small image-encoding boundary in an appropriate lower library and use a pinned PNG encoder or existing SDL capability; do not add encoding to `App`.
- [ ] Normalize captured channel order/row stride and preserve alpha or composite against the pane background through an explicit option.
- [ ] Open a native save dialog with a suggested sanitized title/timestamp, append `.png` when absent, and replace files only after confirmation.
- [ ] Encode/write on a worker if the image is large, using atomic temporary replacement and a bounded completion callback.
- [ ] Expose `export_pane_png` in actions/palette/keybindings and toast the final path or error without logging image data.

## Tests and acceptance

- [ ] Test crop coordinates at 1x/2x DPI, row stride/channel conversion, alpha policy, filename sanitation, cancellation, encoder/write failure, focus change, and stale captures.
- [ ] Decode the produced PNG in a test and compare exact dimensions/pixels for a tiny fixture.
- [ ] Export works on Windows/Vulkan and macOS/Metal and contains only the originally focused pane.
- [ ] No temp file remains on failure and the UI remains responsive during large encoding.

## Dependencies and parallelism

Depends on pane-print state-machine item 14 but not on native printing backends. A codec/test subtask can be delegated after the capture result contract is stable.

<model>GPT-5 Codex</model>
