# Match image formats to uploaded pixel storage

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/rezonality/src/live_project.cpp:1142` normalizes HDR formats only. An LDR image with `format: rgba16f` retains that override, while `rezonality_plugin.cpp:1187` allocates four bytes per pixel and `:2513` copies the full image into a wider format.

**Investigation**

- [x] Exercise LDR images with float/depth overrides and HDR images with explicit formats.

**Fix strategy**

- [x] Derive format from decoded storage and reject incompatible overrides.
- [x] Validate upload size and row stride in both backend upload paths.

**Acceptance criteria**

- [x] No accepted image upload reads beyond its source storage.
- [x] Invalid combinations report diagnostics and preserve the active generation.

**Completion evidence**

- LDR byte images now reject float/depth overrides while accepting matching byte
  storage; HDR environments reject 16-bit/depth overrides and accept explicit
  `rgba32f` storage.
- Metal and Vulkan both call one product-internal upload validator before row
  stride or staging-buffer copies. Direct cases reject short byte/float vectors,
  mixed storage, and incompatible formats.
- Focused project coverage passed 78 assertions across six cases. The core plus
  Rezonality aggregate passed all 29 CTest entries in 106.80 seconds and the
  same-cache Metal smoke passed. Vulkan upload wiring was verified by source
  review but was not executed locally.
