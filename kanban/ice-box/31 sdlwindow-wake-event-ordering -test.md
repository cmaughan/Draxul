# SDL window wake and event ordering

**Type:** test
**Priority:** 31

`SdlWindow::wake()` is a no-op without a real SDL window, so a fake-window test cannot
exercise lost-wake behavior.

- [ ] Use SDL's dummy/headless driver with a real test window, or extract a queue seam that
      production `wake()` and `wait_events()` both use.
- [ ] Prove wake-before-wait and wake-during-wait complete within a bounded deadline.
- [ ] Queue resize, display-scale, file-drop, and the registered file-dialog result event;
      verify ordered delivery without loss.
- [ ] Run focused window tests on Windows and macOS command paths.
