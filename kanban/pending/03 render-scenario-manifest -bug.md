# Truthful render-scenario manifest

**Type:** bug
**Priority:** 03
**Raised by:** GPT/Codex, Claude, Gemini

## Problem

Render scenarios are independently listed in root CMake, multiple `do.py` branches, files, references, and documentation. Missing required assets are silently skipped. The current tree registers a missing `ligatures-view`, omits `wide-char-scroll` and `nanovg-demo` from CTest, and contains additional documentation-only scenarios with no declared status.

## Implementation plan

- [ ] Add `tests/render/manifest.json` as the single inventory with name, purpose, supported platforms, CTest gating, reference requirement, and developer/documentation-only status.
- [ ] Parse the manifest from CMake 3.25 using `string(JSON ...)` and from `do.py` using Python's JSON library.
- [ ] Fail configuration or a dedicated integrity test when a required scenario/reference is absent; report unregistered TOML/reference files.
- [ ] Decide `ligatures-view` explicitly: restore its scenario/references from history or remove all claims/registration.
- [ ] Register `wide-char-scroll` after valid platform references exist and register `nanovg-demo` in the appropriate test-only runner.
- [ ] Mark `claude-logo`/README scenarios as documentation-only or promote them deliberately.
- [ ] Drive `renderall`, `blessall`, command help, and `docs/features.md` from the manifest.
- [ ] Register `tests/do_py_tests.py` so manifest/tooling checks run under CTest or CI.

## Tests

- [ ] Test missing TOML, missing one-platform reference, duplicate name, unknown field, and orphaned file diagnostics.
- [ ] Verify optional platform exclusions are explicit rather than silent.
- [ ] Compare the CTest inventory and `do.py renderall` inventory in an automated check.

## Acceptance criteria

- [ ] One manifest controls every supported render scenario.
- [ ] Required coverage cannot disappear with a status-only CMake message.
- [ ] Existing reference images are not re-blessed unless a rendering change requires it.
- [ ] Configure, build, run render integrity tests, `ctest`, and `py do.py smoke`.

## Dependencies and parallelism

Blocks item 36 and should accompany reopening the completed CI and render-test-isolation cards. Independent of product renderer refactors.

<model>GPT-5 Codex</model>
