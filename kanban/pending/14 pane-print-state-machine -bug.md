# Cross-platform pane-print state machine

**Type:** bug
**Priority:** 14
**Raised by:** GPT/Codex; Claude identified pane printing as a maintenance drag

## Problem

`print_pane` is registered as a general action, but Windows captures a full frame before the lower layer reports that printing is unsupported. `App` owns an unbounded capture flag, focus can change while capture is pending, temporary macOS PDF names can collide and are not removed, and there is no host/backend integration test.

## Implementation plan

- [ ] Extract a small `PanePrintController` or `draxul-pane-capture` component that owns request identity, the captured pane descriptor/host hint, deadline, capture polling, cancellation, backend invocation, and completion cleanup.
- [ ] Define an injected `IPrintBackend` with macOS and Windows implementations; keep CoreGraphics/PDFKit and Win32 print APIs private to their platform source files.
- [ ] On Windows, use the native print dialog and printer device context to print the cropped RGBA/BGRA pane image with aspect-preserving placement; do not create a throwaway full-frame capture for an unsupported action.
- [ ] On macOS, use collision-resistant temporary files or an in-memory/native job where practical, and remove every temporary PDF on success, cancellation, and failure.
- [ ] Give capture requests monotonic IDs and an explicit deadline; ignore late frames from cancelled/expired requests.
- [ ] Snapshot the focused pane rectangle and print hint when the action begins; a later focus change must not redirect the job.
- [ ] Reject a second print request while one is active with a clear toast, or cancel/replace it through one documented policy.
- [ ] Keep raster pane printing as the current contract; record vector ScoreView output as a future enhancement rather than widening this fix.
- [ ] Move platform-independent crop/whitening/page-fit logic below `App` and leave `App` responsible only for dispatching the action and pumping the controller.

## Tests

- [ ] Use a fake capture source, clock, filesystem/temp owner, and print backend to cover success, timeout, cancellation, repeated action, focus change, stale frame, invalid rectangle/hint, and backend failure.
- [ ] Assert temporary resources are removed on every terminal state.
- [ ] Add platform adapter tests for page fit and native error mapping without opening an interactive dialog in normal unit tests.
- [ ] Run a manual print-to-PDF validation on macOS and Windows after automated tests pass.

## Acceptance criteria

- [ ] The print action is functional and truthfully available on both supported platforms.
- [ ] No print request can request frames forever, print the wrong pane, leak a file, or consume a stale capture.
- [ ] `App` no longer owns the print state-machine fields directly.
- [ ] Build `draxul`/tests, run focused tests, render smoke, and application smoke on both backends.

## Dependencies and parallelism

Coordinate with pending 22 (`App` controllers) and 31 (runtime-support ownership), but this reliability fix should not wait for a full App split. It establishes the capture seam used by feature 56 (PNG export). One integration owner should control `app.cpp`; platform adapters can be separate tasks after the contract lands.

<model>GPT-5 Codex</model>
