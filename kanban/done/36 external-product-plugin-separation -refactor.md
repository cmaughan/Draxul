# External product plugin separation

**Type:** refactor
**Completed:** 2026-08-15

## Resolution

SatView, MegaCity/BioView, and ScoreView are self-contained native plugins behind
the versioned C SDK. Product source, dependencies, shaders, assets, tests, and
packaging live in their owning repositories; core has no reverse dependency and
the runtime boundary remains C-only. Generic mounted-submodule registration and
the `Draxul::PluginSupport::*` allowlist provide the same-build support seam.

Windows acceptance covered no-product, each single-product, and all-product
build/smoke permutations plus real Vulkan captures. Commits `b3cf1e7b` and
`cdc2837c` subsequently verified live SatView, MegaCity, and ScoreView panes,
26/26 tests, headless ScoreView, and render comparisons on macOS/Metal.

The repository split that followed is tracked separately in
`kanban/pending/37 product-plugin-repo-split -refactor.md`.
