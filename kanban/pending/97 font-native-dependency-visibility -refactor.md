# Make font-native compile dependencies private

**Priority:** P2 — closes an unnecessary compile-interface leak with limited production churn.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 8.  
**Owner:** One font/build agent.  
**Dependencies:** No dependency on another newly accepted refactor; coordinate `kanban/pending/00 internal-target-build-policy -refactor.md` for the new build-only consumer.

**Evidence:** `libs/draxul-font/CMakeLists.txt:13–15` exports FreeType/HarfBuzz publicly. `text_service.h:86–88` and `rich_text_service.h:81–83` hide implementation through opaque pointers; native types live in private `src/font_engine.h`. Font/style/raster tests reach private headers through source-relative includes and inherit native compile requirements implicitly.

**Boundary:** Keep `draxul-types` public; make FreeType/HarfBuzz private to `draxul-font`. Add explicit test-only font-internals access carrying private headers and their native compile requirements. Preserve public text APIs, plugin consumers, and native final-link dependencies. No new production library.

#### Boundary verification

- [x] Inventory all public font headers and confirm no native types/macros require public propagation.
- [x] Find every white-box consumer, including font, resolver, raster-error, style-model, and box-drawing tests.
- [x] Record production compile usage separately from static final-link requirements.

#### Implementation and migration

- [x] Introduce a test-only interface target for required private includes and native dependencies.
- [x] Migrate white-box includes and link their owning test target explicitly.
- [x] Change production FreeType/HarfBuzz visibility to private.
- [x] Add proposed `draxul-font-link-isolation`, consuming public headers through only `draxul-font`.
- [x] Keep implementation headers out of installed/public APIs.

#### Unit tests

- [x] Retain font/style/fallback/raster-error coverage without duplicating implementation-mirroring tests.
- [x] Verify the isolated public consumer builds/links and does not inherit native compile include requirements.
- [x] Verify white-box consumers receive those requirements only through explicit test access.
- [ ] Enable `cmake --build <cache> --config Debug --target draxul-font-link-isolation draxul-test-core --parallel`.
- [ ] During iteration run `ctest --test-dir <cache> -C Debug -R '^draxul-test-core-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Verify MSVC and Apple Clang compilation and retained native final links.
- [ ] Build affected plugin/font consumers and preserve Vulkan/Metal text rendering.
- [ ] Run `python3 do.py test debug --products`, then `python3 do.py smoke debug --skip-build`; retain affected font/style render checks.

#### Agent documentation/tooling

- [x] Register the build-only consumer in the appropriate aggregate.
- [x] Document public font contracts versus private native/test requirements.
- [ ] Preserve the completed scope and outstanding validation of `kanban/done/30 font-style-model -refactor.md`.

#### Acceptance criteria

- [x] Production consumers no longer inherit FreeType/HarfBuzz compile interfaces through `draxul-font`.
- [x] White-box tests declare their private dependencies explicitly.
- [x] Public font APIs, shaping/rasterization behavior, and final links remain compatible.
- [ ] The report distinguishes compile-interface cleanup from dependency build-cost reduction.

## Final target/module map

Arrows mean “depends on.” Bracketed names are private collaborators, not separate production libraries.

| Owner | Intended dependency direction |
|---|---|
| Existing `draxul-client` | `[SessionStreamPolicy]` uses protocol values; coordinator owns control transport and publication |
| Existing `draxul-app` | Router → typed App operations; App retains lifecycle/UI dependencies |
| Existing `draxul-session-model` | → existing `draxul-plugin-config-support`; remove → `draxul-config` |
| **New `draxul-rezonality-audio STATIC`** | → platform-appropriate SDL requirements and permission support; plugin/runtime → audio |
| Existing `draxul-host` | PluginHost → `[PluginStorage]` → private filesystem/JSON implementation |
| Existing `draxul-treesitter STATIC` | Scanner → `[SourceParser]` → Tree-sitter core/grammar |
| Existing `draxul-satview-core STATIC` | Catalog parsers → `[CSV tokenizer]` |
| Existing `draxul-font` | Public → types; private → FreeType/HarfBuzz; tests explicitly → font internals |

Previously accepted additions remain unchanged: `draxul-rezonality-project STATIC`, `draxul-plugin-nanovg-support STATIC`, `draxul-agent-integration STATIC`, and `draxul-weather STATIC`. Rezonality project values must not depend on audio implementation; runtime orchestrates the project/audio/backend boundaries. No new dependency between the eight follow-up refactors is required.
