# Native plugin scaffolding command

**Type:** feature
**Priority:** 38
**Raised by:** Claude

## Developer need

Adding a native plugin requires repeated SDK manifest, C entry point, CMake registration,
platform rendering, staging, and test boilerplate. A generator should start from the
in-repo spinning-triangle contract without editing central product switches.

## Implementation plan

- [ ] Add `py do.py new-plugin <id> <path>` with validation and `--dry-run`.
- [ ] Derive templates from `plugins/spinning-triangle` for the SDK manifest, C entry
      point, CMake registration, platform sources, and a loading/render smoke.
- [ ] Generate Vulkan/Metal branches only when requested.
- [ ] Register through `draxul_register_bundled_plugin`; do not add product-specific
      switches or source lists to root CMake.
- [ ] Refuse existing paths/targets and roll back all generated files if any step fails.
- [ ] Print next steps for implementation, docs, optional-off validation, build, tests, and smoke.

## Tests and acceptance

- [ ] Generate sample plugins in temporary fixture repositories and configure them
      against the installed SDK and mounted-submodule contract.
- [ ] Test invalid names, collisions, dry-run, rollback, Windows/macOS template selection, and idempotent failure.
- [ ] Generated code follows SDK ABI, manifest, header placement, GLM, and CMake rules.
- [ ] No core host enum or central product source list changes.

## Dependencies and parallelism

The generic registration and plugin-support contracts are stable. This is an independent
developer-tooling task.

<model>GPT-5 Codex</model>
