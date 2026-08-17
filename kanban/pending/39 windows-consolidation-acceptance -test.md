# Windows acceptance for the shared-library consolidation

The seven slices in
[`kanban/done/38 shared-library-consolidation -refactor.md`](../done/38%20shared-library-consolidation%20-refactor.md)
were developed and validated entirely on macOS/Metal. Two categories of change
have **no macOS runtime coverage at all** and need a Windows pass before the
consolidation can be called fully accepted.

- [ ] **Re-enable the `Build` GitHub workflow.** It is currently
      `disabled_manually` (last runs failed in mid-July, before this work), so
      nothing has compiled the Vulkan half of these changes. Investigate those
      pre-existing failures first, then enable:
      `gh workflow enable Build --repo cmaughan/Draxul`. The workflow now
      checks out submodules recursively and hard-fails on an unmounted enabled
      plugin (`DRAXUL_REQUIRE_ENABLED_PLUGINS`), so a green run genuinely means
      product coverage ran.
- [ ] **Compile and run the Vulkan paths.** No Vulkan SDK exists on the dev
      Mac, so Slice 5's Vulkan TUs were verified with `clang++ -fsyntax-only`
      against fetched Vulkan-Headers/VMA only — compile-level, never linked or
      executed. Covers `libs/draxul-plugin-support` (`vk_resource_helpers`,
      `vk_hdr_scene_pipeline`, `vk_plugin_allocator`), both products' VK
      renderers, and core `vk_renderer` (which newly adopts `grid_contract.h`
      and `pixel_w_/pixel_h_` for screen size).
- [ ] **Re-bless the render references that legitimately change.**
      `scoreview-plugin.windows.bmp` will differ — Slice 2 gave ScoreView's
      ImGui context the `IsSRGB` flag the other products already set, so
      overlay gamma changes on purpose. MegaCity's reference may differ if the
      new per-format MSAA probe selects 2x where the old limits-only probe
      forced 4x. Verify each diff is explained by those two causes before
      blessing; an unexplained diff is a regression, not a re-bless.
- [ ] **Run the full suite plus the render matrix** (`t.bat`) and the
      plugin/SDK smokes on Windows, including the external-SDK DLL smoke that
      needs the MSVC environment.

Non-blocking follow-ups recorded during the work:

- [ ] Migrate SatView's four `wait_for_idle` clones onto `tests::wait_until`
      (outside Slice 7's file-ownership boundary at the time).
- [ ] Revisit SatView's triplicated atmosphere scattering math. Slice 5
      deliberately left it: the three copies differ in step counts, ray-start
      policy, scattering multiplier, and night tinting, so a shared include
      would need enough parameters to stop being shared math.
