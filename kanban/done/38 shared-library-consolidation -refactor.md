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
- [x] Slice 5: extend `PluginSupport::VulkanResources` (safe MSAA probe,
      `HdrScenePipeline`), shared ACES/atmosphere shader includes with
      contract parity tests, Vulkan adopts `grid_contract.h`, new
      `PluginSupport::CameraInput`. (Fixes #7, #8, #11. Notes: the atmosphere
      scattering math was deliberately NOT extracted — the three copies have
      different step counts, ray-start policies and output tinting, so a shared
      include would need enough parameters to stop being shared math; recorded
      as a follow-up rather than forced. MegaCity keeps its in-frame staged
      texture upload; only SatView's load-time path adopts
      `upload_image_immediate`, because a blocking queue wait mid-frame would be
      a behaviour change, not a dedupe. Adopting the shared scene pass also
      fixed a latent MegaCity bug: its pass declared a resolve attachment
      unconditionally, which is invalid whenever the MSAA probe falls back
      to 1x.)
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
      surfaced a standalone-plugin crash (`SetCurrentFont: font &&
      font->IsLoaded()`); Slice 3 root-caused and fixed it — the standalone
      macOS module was missing the single-exported-symbol link contract, so
      dyld coalesced ImGui's weak inline symbols with the host and the plugin
      read the host's ImGui context. The extraction smoke now passes
      end-to-end including a real Grieg render.

**Completed 2026-08-17.** All seven slices landed on `main`, each validated
with a full build, `do.py smoke`, and 26/26 `ctest` before the next merge.
Windows acceptance is complete; see
[`39 windows-consolidation-acceptance -test.md`](39%20windows-consolidation-acceptance%20-test.md).
