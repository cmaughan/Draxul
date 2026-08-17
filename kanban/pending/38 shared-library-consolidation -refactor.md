# Shared-library consolidation

Retire the duplication found in
[`plans/duplication-audit-2026-08-15.md`](../../plans/duplication-audit-2026-08-15.md)
by giving each duplicated concern one home. Seven slices, each buildable and
smoke-clean with an observable gate; full plan in
[`plans/shared-library-consolidation.md`](../../plans/shared-library-consolidation.md).
Fourteen confirmed divergence bugs are fixed in the slice that makes each one
structurally unrepeatable.

- [x] Slice 1: `draxul-types` absorbs string/modifier/log/process/path
      helpers, `draxul-font` gains the shared glyph blit, and types ship with
      the SDK install component. (Fixes audit bugs #4, #6, #14.)
- [x] Slice 2: `libs/draxul-imgui-core` — one scancode table + `IImGuiHost`;
      `PluginImGuiContext` + `ImGuiInputBridge` in `plugins/support/imgui`;
      all three product runtimes adopt. (Fixes #1, #2, #10.)
- [x] Slice 3: promote `draxul-nanovg` to `Draxul::PluginSupport::NanoVG`;
      ScoreView deletes its vendored NanoVG stack and shadow `draxul/`
      headers. (Fixes #3.)
- [x] Slice 4: `Draxul::PluginSupport::Adapter` plugin shell; SatView and
      spinning-triangle collapse to single dual-backend adapter TUs; delete
      dead `HostServices::ui_style()`. (Fixes #9. Note: the audit's
      "zero callers" claim was stale — the plugin fixture used
      `HostServices::ui_style()`; it now exercises the raw C service, and
      `UiStyleClient` is the one C++ convenience. ScoreView and
      spinning-triangle adopt the header-only shell core, which ships with
      the SDK install component; their raw path/storage blocks stay local
      because standalone extractions link only the installed SDK.)
- [ ] Slice 5: extend `PluginSupport::VulkanResources` (safe MSAA probe,
      `HdrScenePipeline`), shared ACES/atmosphere shader includes with
      contract parity tests, Vulkan adopts `grid_contract.h`, new
      `PluginSupport::CameraInput`. (Fixes #7, #8, #11.)
- [x] Slice 6: `draxul-protocol` owns topology→layout conversion, traversal
      helpers, and split-ratio constants; `ServerControlChannel` +
      `RevisionPolledClient` in `draxul-client`. (Fixes #5.)
- [x] Slice 7: `TerminalSurfaceHostBase` shared by local/remote terminal
      hosts; test-support consolidation (`test_support.h` helpers, shared
      `sdk_smoke.py`, ScoreView fixture dedupe, `synthetic_piano.h` moves to
      the scoreview repo). (Fixes #12, #13.) SatView's `wait_for_idle` pairs
      were left in place — that repo was outside this slice's file-ownership
      boundary; migrate them onto `tests::wait_until` when SatView is next
      touched. Repairing the ScoreView extraction smoke's macOS staging
      surfaced a pre-existing product defect: the standalone-built plugin
      aborts in its ImGui overlay (`SetCurrentFont: font && font->IsLoaded()`)
      during the render probe. That fix belongs in the scoreview product
      runtime (Slice 2/3 territory), not the test layer.
