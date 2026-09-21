# Match image formats to uploaded pixel storage

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/rezonality/src/live_project.cpp:1142` normalizes HDR formats only. An LDR image with `format: rgba16f` retains that override, while `rezonality_plugin.cpp:1187` allocates four bytes per pixel and `:2513` copies the full image into a wider format.

**Investigation**

- [ ] Exercise LDR images with float/depth overrides and HDR images with explicit formats.

**Fix strategy**

- [ ] Derive format from decoded storage, perform an explicit conversion, or reject incompatible overrides.
- [ ] Validate upload size and row stride on both backends.

**Acceptance criteria**

- [ ] No accepted image upload reads beyond its source storage.
- [ ] Invalid combinations report diagnostics and preserve the active generation.
