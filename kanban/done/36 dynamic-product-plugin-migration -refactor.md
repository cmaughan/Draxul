# Dynamic product plugin migration

Execute the vertical slices in
[`plans/dynamic-plugin-satview-scoreview-migration.md`](../../plans/dynamic-plugin-satview-scoreview-migration.md)
to move SatView and ScoreView onto the trusted native plugin architecture.

- [x] Slice 1: replace plugin ABI v1 with current-only ABI v2 and prove lifecycle,
      scheduling, metadata, actions, and teardown through the spinning triangle.
- [x] Slice 2: prove durable client-local path/storage services through the
      spinning triangle.
- [x] Slice 3: extract `SatViewRuntime` and run the existing static host through it.
- [x] Slice 4: ship the SatView scene as a real Vulkan/Metal plugin.
- [x] Slice 5: add SatView controls/state and remove the static SatView host route.
- [x] Slice 6: extract `ScoreRuntime` and the backend-neutral score canvas seam.
- [x] Slice 7: ship ScoreView reading modes through the dynamic plugin route.
- [x] Slice 8: move ScoreView transport, workers, and player persistence across.
- [x] Slice 9: add deterministic audio, microphone, and MIDI device ownership.
- [x] Slice 10: remove the static ScoreView route and complete packaging/docs cleanup.

Completed on Windows on 2026-08-12. SatView and ScoreView now have plugin-only
production routes: their legacy host kinds, provider registrations, executable
links, static adapters, and executable-relative product assets are gone. Clean
plugin staging includes each module's native dependencies and assets; the main
executable imports neither product nor Verovio. Missing modules and local source
failures preserve the pane with a wrapped actionable placeholder. The topology
CLI integration, all 24 CTest entries, the three dynamic-product Vulkan render
comparisons, clean-package discovery/removal checks, debug smoke, and repository
hygiene pass. Metal/macOS remains enforced by CI because it is not runnable from
this Windows checkout.

Acceptance is defined per slice in the plan. Prefer isolated-server, attached-UI,
render-snapshot, and smoke coverage over narrow helper unit tests.
