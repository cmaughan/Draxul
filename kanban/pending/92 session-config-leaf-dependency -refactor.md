# Remove Session persistence’s application-config dependency

**Priority:** P2 — a small dependency correction removes unnecessary application/SDL closure from headless consumers.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 3.  
**Owner:** One core dependency agent.  
**Dependencies:** None among newly accepted refactors; coordinate shared CMake audit edits.

**Evidence:** `libs/draxul-session-model/CMakeLists.txt:12–15` privately links `draxul-config`, whose private dependencies include SDL. `session_state.cpp:3–6,1163–1166` needs config-document/TOML support and `ConfigDocument::default_path()`. These already belong to `draxul-plugin-config-support` (`libs/draxul-plugin-support/CMakeLists.txt:47–52`; `src/config_document.cpp:38–42`). Current dependency guards inspect direct links; existing isolated consumers alone do not reject unwanted transitive dependencies.

**Boundary:** Replace the private application-config edge with existing `draxul-plugin-config-support`. Preserve Session public APIs and public agent/host-identity dependencies. Configuration schema/keybindings remain in `draxul-config`; path/document support remains in its existing leaf. No new production library.

#### Boundary verification

- [x] Inventory Session implementation includes/symbols and the Session → protocol/client/server transitive closure.
- [x] Confirm config-support supplies every required symbol without application configuration implementation.
- [x] Record current directory selection, serialization, and durable replacement behavior.

#### Implementation and migration

- [x] Replace only the obsolete target edge and retain necessary performance/JSON dependencies.
- [x] Extend boundary validation to inspect the relevant transitive target closure, including static-library link-only edges.
- [x] Reject application config, SDL implementation, and UI dependencies reaching headless Session/protocol/client/server owners.
- [x] Handle aliases, cycles, and relevant generator-expression wrappers explicitly; avoid an unrelated graph-tool rewrite.

#### Unit tests

- [x] Add meaningful configure fixtures proving an indirect forbidden edge fails and the intended leaf closure passes.
- [ ] Retain existing Session codec, checkpoint, default-path, and durability regressions.
- [ ] Build `cmake --build <cache> --config Debug --target draxul-session-model draxul-protocol-link-isolation draxul-client-link-isolation draxul-server-link-isolation --parallel`.
- [ ] Verify the isolated consumer link closures, not merely successful compilation through the broad test executable.

#### Cross-platform validation

- [ ] Check Windows/macOS default directories and durable I/O remain unchanged.
- [x] Verify the headless closure gains neither Vulkan nor Metal dependencies.
- [ ] Run `python3 do.py test debug`, then `python3 do.py smoke debug --skip-build`.

#### Agent documentation/tooling

- [x] Update dependency comments/module-map claims to describe the verified leaf dependency.
- [x] Register configure fixtures in the existing validation flow and keep them platform-aware.
- [x] Document target-closure isolation without promising graphics-SDK-free root configuration.

#### Acceptance criteria

- [x] Session persistence no longer depends directly or transitively on application configuration or SDL implementation.
- [ ] Protocol/client/server isolation consumers retain valid links.
- [ ] Default paths, persisted formats, public APIs, and replacement semantics are unchanged.
- [x] A regression through an indirect forbidden dependency is detected.
