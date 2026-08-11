# Renderer-free terminal core

**Plan:** `plans/server-client-terminal-runtime.md`, Slice 1

- [x] Extract VT parsing, terminal modes, grid mutation, alternate-screen behavior,
      attributes, metadata, and semantic snapshots into `draxul-terminal-core`.
- [x] Adapt the existing local terminal host through a process/UI callback boundary.
- [x] Keep selection, copy mode, process lifecycle, rendering, clipboard access, and
      other client concerns in the host adapter.
- [x] Prove `draxul-terminal-core` has no window, renderer, SDL, `IHost`, or process
      dependency.
- [x] Preserve local terminal replay digests and visible behavior.
- [x] Update the module map, feature documentation, plan status, and focused tests.
- [x] Pass focused terminal tests, full app/tests build, smoke, and relevant render
      validation.
