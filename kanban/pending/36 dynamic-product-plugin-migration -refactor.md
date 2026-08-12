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
- [ ] Slice 8: move ScoreView transport, workers, and player persistence across.
- [ ] Slice 9: add deterministic audio, microphone, and MIDI device ownership.
- [ ] Slice 10: remove the static ScoreView route and complete packaging/docs cleanup.

Acceptance is defined per slice in the plan. Prefer isolated-server, attached-UI,
render-snapshot, and smoke coverage over narrow helper unit tests.
