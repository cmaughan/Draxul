# Windows acceptance for the shared-library consolidation

The seven slices in
[`kanban/done/38 shared-library-consolidation -refactor.md`](../done/38%20shared-library-consolidation%20-refactor.md)
were developed and validated entirely on macOS/Metal. Two categories of change
have **no macOS runtime coverage at all** and need a Windows pass before the
consolidation can be called fully accepted.

- [x] **Re-enable the `Build` GitHub workflow.** It is active again and the
      current workflow contains both Windows/Vulkan and macOS/Metal jobs. The workflow
      now
      checks out submodules recursively and hard-fails on an unmounted enabled
      plugin (`DRAXUL_REQUIRE_ENABLED_PLUGINS`), so a green run genuinely means
      product coverage ran.
- [x] **Compile and run the Vulkan paths.** Windows acceptance commit `de382caf`
      compiled and exercised the affected paths. No Vulkan SDK exists on the dev
      Mac, so Slice 5's Vulkan TUs were verified with `clang++ -fsyntax-only`
      against fetched Vulkan-Headers/VMA only — compile-level, never linked or
      executed. Covers `libs/draxul-plugin-support` (`vk_resource_helpers`,
      `vk_hdr_scene_pipeline`, `vk_plugin_allocator`), both products' VK
      renderers, and core `vk_renderer` (which newly adopts `grid_contract.h`
      and `pixel_w_/pixel_h_` for screen size).
- [x] **Re-bless the render references that legitimately change.**
      `scoreview-plugin.windows.bmp` will differ — Slice 2 gave ScoreView's
      ImGui context the `IsSRGB` flag the other products already set, so
      overlay gamma changes on purpose. MegaCity's reference may differ if the
      new per-format MSAA probe selects 2x where the old limits-only probe
      forced 4x. Verify each diff is explained by those two causes before
      blessing; an unexplained diff is a regression, not a re-bless.
- [x] **Run the full suite plus the render matrix** (`t.bat`) and the
      plugin/SDK smokes on Windows, including the external-SDK DLL smoke that
      needs the MSVC environment.

Non-blocking follow-ups recorded during the work:

- [x] SatView's polling helpers moved onto shared test wait support in submodule
      commit `91e315d`, adopted by `de382caf`.
- The atmosphere-scattering cleanup is unrelated product work and is now tracked
  by `plugins/satview/kanban/ice-box/40 satview-atmosphere-scattering-deduplication -refactor.md`.

The card remains pending only until the re-enabled Build workflow completes a
green Windows and macOS run on the current tree.

- [ ] Confirm the current Build workflow completes successfully on both jobs, then
      move this card to `kanban/done/`.

Current checkpoint (run `32040158417`): the Windows `Build and test` step failed while
the macOS job was still running. Inspect the completed run log and fix or classify that
failure before closing this card.
