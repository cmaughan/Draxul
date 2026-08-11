# Refactoring review

No P0 findings. App/host/Nvim/Kanban/Megacity-model/SatView-host/Score-overlay/guidance work already covered by `kanban/pending/00`–`08` is excluded.

## 1. Split control-plane common logic from platform transport

- **Location:** `libs/draxul-control/src/control_plane.cpp:51-940`, `:965-1661`, `:1732-1892`; `libs/draxul-control/CMakeLists.txt`; `tests/control_plane_tests.cpp:1-1325`.
- **Priority:** P1.
- **Current structural problem:** One 1,892-line TU owns framing and JSON validation, endpoint naming, secure metadata replacement/cache, Windows ACLs and named pipes, POSIX locks and sockets, server queue/lifecycle, and synchronous client I/O. Windows/POSIX branches are interleaved. The test file likewise mixes CLI/integration/App tests with transport tests, so it is classified into `draxul-test-app`; isolated control transport changes therefore compile and test the App closure.
- **Proposed boundary:** Preserve the public `draxul-control` target and `control_plane.h`, but create backend-private sources for:
  - common codec/deadline logic;
  - metadata/cache logic;
  - Windows and POSIX endpoint/file primitives;
  - common server dispatch/queue state;
  - platform server listeners and client exchanges.
- **Dependency shape:** `draxul-client`, `draxul-server`, and App remain callers of `draxul-control`. JSON stays public because public request/result values expose it. `advapi32`, Windows handles, POSIX descriptors, locking, polling, and socket details remain private.
- **Migration path:** First extract pure framing/depth/response helpers without behavior changes; then metadata/cache; then move the existing client branches; finally move the two `ControlServer::Impl::run` branches. Select platform files in the existing CMake target after each extraction.
- **Testing improvement:** Split pure transport cases from `tests/control_plane_tests.cpp` into `control_transport_tests.cpp`, allowing default core-target classification. Keep CLI/router/App endpoint cases in App-owned files. Add direct codec boundary tests without creating sockets.
- **Agent-work benefit:** Common protocol, Windows transport, POSIX transport, and App integration become separately owned files. Platform fixes stop colliding in a single TU.
- **Risks and prerequisites:** Preserve absolute request deadlines, startup synchronization, stale-endpoint serialization, same-user security, endpoint abandonment, and non-blocking shutdown. Coordinate with pending `09 macos-remote-terminal-channel -bug.md` and `10 server-lifecycle-sigterm-eviction -bug.md`, which touch this area.

## 2. Decompose `ServerKernel::Impl` behind its existing public façade

- **Location:** `libs/draxul-server/src/server_kernel.cpp:339-557` (`Impl` and all state), `:558-1441` (publication/session lifecycle), `:1475-1808` (clients/checkpoints), `:1899-2873` (request dispatcher), `:2989-3215` (terminal/agent runtime), `:3216-3527` (event loop); `tests/server_kernel_tests.cpp` (4,928 lines); `libs/draxul-server/include/draxul/server_agent_service.h`.
- **Priority:** P1.
- **Current structural problem:** The PImpl hides implementation from callers but does not create internal ownership. One class owns singleton publication, session restore/delete/rename/checkpointing, client authentication and expiry, topology routing, agent launch/control, terminal runtime ownership, status projection, and the event loop. `handle_request` alone spans roughly 975 lines and coordinates every method family. The matching test TU combines resource budgets, discovery, transport, agents, topology, persistence, process behavior, and codecs. Additionally, `ServerAgentService` is published under `include/` solely for `server_kernel.cpp` and `tests/agent_protocol_tests.cpp`, forcing its control/JSON contract into the production public surface.
- **Proposed boundary:** Keep one `draxul-server` static library and the current `ServerKernel` API. Introduce a private `server_kernel_impl.h`, then split member definitions into lifecycle, sessions/checkpoints, requests, terminals/agents, and event-loop TUs. Move `server_agent_service.h` to `src/` and expose it to tests through a named `draxul-server-test-internals` interface.
- **Dependency shape:** Public server headers require the agent and protocol contracts. Control transport, session persistence, terminal-process implementations, topology internals, and agent-service implementation remain private. Existing clients continue linking only `draxul-server`; no new cross-library abstraction is needed.
- **Migration path:** Move private state declarations first; split existing member definitions mechanically; move the agent-service header and update its focused test; only then introduce narrower collaborators such as a client registry or session repository where state ownership is demonstrably independent.
- **Testing improvement:** Split `server_kernel_tests.cpp` into lifecycle/discovery, sessions/checkpoints, topology/terminal, agent-control, and codec/resource suites while preserving tags. Directly test the private agent service through the test-internals boundary instead of treating it as public API.
- **Agent-work benefit:** Server lifecycle, persistence, terminal, and agent changes stop competing for one implementation and one test file. Review context becomes method-family sized.
- **Risks and prerequisites:** The first phase should be mechanical because ordering and shared state are sensitive. Preserve lock ownership, checkpoint task lifetime, generation checks, controller leases, idempotency caches, and shutdown order. Pending server cards `09`, `10`, `11`, and `14` currently overlap these files; freeze the private state seam with one owner before parallelizing later moves.

## 3. Move Unicode implementation out of the universal public header

- **Location:** `libs/draxul-types/include/draxul/unicode.h:15-672`; especially lookup tables and classification logic at `:173-468` and clustering at `:484-672`; `libs/draxul-types/CMakeLists.txt`.
- **Priority:** P2.
- **Current structural problem:** A 672-line public header implements decoding, large East Asian/emoji range tables, width calculation, validation, clustering, and vector-producing helpers inline. It is directly included by 19 production TUs across App, font, grid, GUI, Nvim, terminal core, Markdown, Kanban, and SatView. Unicode implementation changes therefore trigger broad recompilation and expose substantial implementation context everywhere.
- **Proposed boundary:** Preserve Unicode ownership in `draxul-types`, consistent with the completed foundation cleanup. Add `src/unicode.cpp`; leave stable declarations and small value records in `unicode.h`. Retain only genuinely useful trivial/constexpr helpers inline.
- **Dependency shape:** Callers and target links remain unchanged. `unicode.cpp` privately owns range tables and high-level decoding/clustering implementations and depends only on the current types/STL surface.
- **Migration path:** Move large classification tables first, then vector-producing and cluster functions, then evaluate hot scalar helpers individually. Keep names, signatures, replacement-character behavior, and `UiOptions` defaults unchanged.
- **Testing improvement:** Existing `unicode_tests.cpp`, `ui_events_tests.cpp`, and overlay/grid tests become compiled-library tests rather than independently instantiated header implementations. Add a Unicode call to a `draxul-types` link-isolation consumer when the pending internal-target policy permits it.
- **Agent-work benefit:** Unicode behavior can change in one implementation file without recompiling unrelated consumers or creating widespread merge conflicts.
- **Risks and prerequisites:** Measure cell-rendering hot paths before removing beneficial inlining. Preserve identical behavior on MSVC and Apple Clang, especially malformed UTF-8, ambiguous width, emoji presentation, Indic virama, and ZWJ clustering.

## 4. Separate ScoreView keyboard geometry from NanoVG replay

- **Location:** `modules/score/draxul-scoreview/include/draxul/scoreview/keyboard_render_nvg.h:13-98`; `src/keyboard_layout.cpp:1-32`; `src/keyboard_render_nvg.cpp:13`; `draxul-scoreview/CMakeLists.txt:6-17,54-66`; `tests/scoreview_composer_tests.cpp:10,541-692`.
- **Priority:** P2.
- **Current structural problem:** One public header combines pure keyboard geometry/palette APIs with `NVGcontext`, `KeyboardLit`, and the host-only draw function. `keyboard_layout.cpp` belongs to `draxul-scoreview`, while `draw_piano_keyboard` is defined only in `draxul-scoreview-host`; consequently the core target publishes a declaration it does not implement. Pure keyboard tests are embedded in a host-classified composer suite.
- **Proposed boundary:** Add public core header `keyboard_layout.h` containing MIDI bounds, geometry helpers, palette data, and palette selection. Keep `KeyboardLit` and `draw_piano_keyboard` in a host-private NanoVG header.
- **Dependency shape:** `draxul-scoreview` owns layout/palette and depends on no rendering API. `draxul-scoreview-host` depends on core layout plus NanoVG and owns replay. No additional static library is warranted.
- **Migration path:** Introduce the core header, update core and host includes, move host-only declarations, then move keyboard cases into `scoreview_keyboard_layout_tests.cpp`. Remove the mixed header once all consumers migrate; do not retain forwarding duplicates.
- **Testing improvement:** Keyboard geometry/palette tests run in `draxul-test-scoreview` without the host, microphone, ImGui, SDL, or NanoVG closure. Keep one host-level draw smoke test if needed.
- **Agent-work benefit:** Keyboard model/layout and presentation become independently editable and reviewable.
- **Risks and prerequisites:** Coordinate with pending `07 scoreview-analysis-overlay-boundary -refactor.md`, since its draw/build split also consumes the guidance palette. Preserve palette byte values and visual output.

## Proposed target map

```text
draxul-types
└── unicode.h declarations + private unicode.cpp

draxul-control
├── public control_plane.h
└── private codec / metadata / server-common
    ├── Windows pipe + ACL backend
    └── POSIX socket + lock backend

draxul-server
├── public ServerKernel + protocol-facing API
├── private lifecycle / session / request / terminal-agent TUs
└── draxul-server-test-internals

draxul-scoreview
└── keyboard layout + palette

draxul-scoreview-host
└── NanoVG keyboard replay
```

## Isolated work packages

1. Unicode header-to-TU move: independent of product and server work.
2. Score keyboard boundary: independent, but land before or alongside pending Score overlay work.
3. Control codec/test-file extraction: platform-neutral first; Windows and POSIX backend moves can follow independently after the private seam freezes.
4. Server decomposition: one owner establishes private state/TU boundaries; test and method-family splits can then proceed independently after active server cards settle.

## Structural qualities to preserve

- CMake already expresses most library and optional-product boundaries explicitly.
- Foundation dependency checks reject renderer/UI/product leakage.
- Public PImpl façades already protect `ControlServer` and `ServerKernel` callers.
- Renderer backend types remain private while neutral scene contracts serve both Vulkan and Metal.
- Focused product test executables, named test-internals interfaces, and link-isolation consumers provide strong patterns for the proposed changes.
- `docs/module-map.md` and the Megacity-local guide clearly state dependency direction and cross-platform obligations.

Validation was static inspection only, as requested; no files were changed and no build or executable was run.
