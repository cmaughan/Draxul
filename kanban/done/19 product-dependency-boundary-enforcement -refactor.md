# Enforce product dependency direction

**Type:** refactor
**Completed:** 2026-08-15
**Disposition:** Delivered through the external-plugin boundary.

## Resolution

`cmake/DraxulPlugins.cmake` restricts mounted products to their own targets,
third-party targets, and the explicit `Draxul::PluginSupport::*` allowlist.
`cmake/CheckDependencyBoundaries.cmake` rejects core-to-product target links and
product-header includes from core. Commits `9ff37992`, `d86445d6`, `01766701`, and
`8456ce7b` established and then preserved those checks through the submodule
cutover.
