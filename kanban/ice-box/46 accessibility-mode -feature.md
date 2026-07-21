# Accessibility mode

**Type:** feature
**Priority:** 46
**Raised by:** GPT/Codex

## User need

Offer independent UI/chrome scaling, reduced motion, stronger focus indication, high-contrast/color-blind-safe themes, and keyboard-accessible native controls without changing terminal cell semantics unexpectedly.

## Implementation plan

- [ ] Audit all native Draxul UI surfaces and animations; distinguish terminal font size from chrome/ImGui/overlay scale.
- [ ] Add typed accessibility config for UI scale, reduced motion, high contrast, enhanced focus, and palette preset.
- [ ] Route scale through Chrome layout, native overlays, ImGui font/style, hit testing, and minimum target sizes.
- [ ] Disable/reduce toast/chord/camera transitions under reduced motion while preserving state feedback.
- [ ] Add keyboard focus/order/activation for palette, toasts, session picker, and diagnostics controls.
- [ ] Use luminance/contrast checks for configurable colors and ship at least one tested high-contrast/color-blind-safe preset.
- [ ] Investigate platform accessibility labels/APIs and document what SDL/ImGui can and cannot expose.

## Tests and acceptance

- [ ] Pure layout tests cover multiple UI scales/DPI combinations and hit targets.
- [ ] Contrast tests validate built-in presets; input tests cover keyboard-only operation.
- [ ] Reduced-motion mode has no time-dependent position transitions.
- [ ] Both platform render paths and docs are updated; render tests/smoke pass.

## Dependencies and parallelism

Best after items 23 and 29 clarify UI ownership and item 21 provides schema-driven config.

<model>GPT-5 Codex</model>
