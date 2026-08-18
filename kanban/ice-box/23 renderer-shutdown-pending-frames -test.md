# Renderer shutdown with pending GPU frames

**Type:** test  
**Priority:** 23

A stub renderer cannot prove Metal/Vulkan fence and resource behavior. This must be a
platform-gated backend integration or be enabled by an explicit backend lifecycle seam.

- [ ] Start a real backend frame, leave work in flight, and request shutdown before normal
      frame completion.
- [ ] Verify bounded fence/drain behavior, resource release, and idempotent shutdown.
- [ ] Use `kanban/ice-box/14 metal-headless-init -test.md` for Metal and the corresponding
      Vulkan test surface; retain a strict timeout.
- [ ] Run the owning backend tests and same-cache smoke on each available platform.
