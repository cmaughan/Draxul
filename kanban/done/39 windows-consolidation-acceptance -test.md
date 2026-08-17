# Windows acceptance for the shared-library consolidation

The seven slices in
[`kanban/done/38 shared-library-consolidation -refactor.md`](../done/38%20shared-library-consolidation%20-refactor.md)
were developed and validated entirely on macOS/Metal. Two categories of change
have **no macOS runtime coverage at all** and need a Windows pass before the
consolidation can be called fully accepted.

- [x] **Re-enable the `Build` GitHub workflow.** The workflow is enabled and
      its pre-existing infrastructure failures are repaired: Windows is pinned
      to the VS 2022 runner required by the preset, and the removed Vulkan
      1.3.283 installer is replaced by the current stable LunarG SDK. The
      Windows job is green for commit `9c30659d`. The subsequently exposed
      Xcode 26 optional Metal toolchain is now installed and verified before
      the macOS build. The POSIX test runner now gives CMake an explicit,
      CPU-scaled build limit instead of unlimited `make -j`, with hosted macOS
      pinned to four jobs. Windows now always runs the LunarG installer rather
      than restoring SDK files without its system Vulkan loader. The complete
      workflow passed for commit `3d03471b` in
      [run 32052055286](https://github.com/cmaughan/Draxul/actions/runs/32052055286).
- [x] **Compile and run the Vulkan paths.** No Vulkan SDK exists on the dev
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

- [x] Migrate SatView's service-test `wait_for_idle` clones onto the shared
      bounded `tests::pump_until` helper
      (outside Slice 7's file-ownership boundary at the time).
- [x] Revisit SatView's triplicated atmosphere scattering math. Slice 5
      deliberately left it: the three copies differ in step counts, ray-start
      policy, scattering multiplier, and night tinting, so a shared include
      would need enough parameters to stop being shared math.

## Acceptance evidence

- Full Windows Debug MSVC/Vulkan and Release/Ninja builds link the core
  renderer and the real MegaCity, SatView, ScoreView, and spinning-triangle
  DLLs. The Vulkan continuation-pass smoke found and fixed a missing disabled
  depth-stencil state in the shared fullscreen pipeline helper.
- `DRAXUL_RUN_SLOW_TESTS=1 t.bat debug`: 29/29 tests passed, including all
  product shards, topology integration, seven render tests, bundled plugin
  discovery, and the external spinning-triangle SDK DLL smoke.
- `do.py renderall`: all seven Windows images passed. MegaCity and ScoreView
  matched their references exactly (zero changed pixels). Their scenarios now
  expand `${PROJECT_ROOT}` inside plugin JSON, so direct runs and CTest resolve
  the same fixtures.
- ScoreView's reference change is the intended Grieg reference plus the prior
  sRGB ImGui correction. MegaCity had no checked-in Windows product reference;
  its new deterministic reference scans `plugins/megacity/tests` and contains
  real scanned buildings. No MSAA-only MegaCity drift needed blessing.
- The opt-in copied-tree ScoreView extraction target passed after building its
  real DLL from the installed SDK, discovering ABI 2 metadata, and rendering
  the Grieg fixture. This keeps the expensive Verovio rebuild out of the normal
  edit/test loop while proving the extraction boundary.
- Hosted Windows CI has a Vulkan SDK/compiler but no `VK_KHR_surface`. Its SDK
  smoke therefore still builds, loads, and inspects the external DLL while the
  raw-GPU render remains enabled by default on GPU-capable developer machines.
- The corrected hosted Windows job is fully green in run `32042382344`.
  Four-way CTest load exposed two asynchronous tests whose original deadlines
  assumed idle developer-machine scheduling. SatView's catalog/cloud service
  waits now retain their early exit but allow five seconds, and the remote
  history selection test pumps until the selected content is copyable. The
  focused core + SatView matrix passed 12/12, followed by five parallel passes
  of both exact failing cases.
- A later run proved the SDK-directory cache was invalid: a cache miss ran the
  LunarG installer and passed, while a cache hit restored 355 MB of headers and
  tools but omitted the system `vulkan-1.dll`, causing every renderer-linked
  executable to exit with `STATUS_DLL_NOT_FOUND`. CI now always installs the
  SDK/runtime and checks the 64-bit loader before the expensive build.
- The complete Windows/macOS Build workflow passed for commit `3d03471b` in
  [run 32052055286](https://github.com/cmaughan/Draxul/actions/runs/32052055286).
- Windows filesystem error matching now compares against the portable error
  condition. Server control-transfer and lease tests use realistic Debug timing
  headroom, and the alternate-screen integration waits for the independently
  projected mode transition (five consecutive focused runs passed).
- The atmosphere implementations remain separate by design. Orbital and ground
  views differ in ray origin/clipping, step/sample policy, scattering gain,
  alpha cap, and night floor; Metal already shares its density/optical-depth
  leaves internally. A cross-shader abstraction would currently hide those
  semantics behind parameters without removing meaningful duplication.
