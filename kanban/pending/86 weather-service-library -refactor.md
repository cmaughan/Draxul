# Move weather fetching into a focused library

**Priority:** P2 — an existing self-contained service gains independent tests and build ownership.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 7.  
**Owner:** One weather/service agent.  
**Dependencies:** `kanban/pending/00 internal-target-build-policy -refactor.md` and `kanban/pending/01 app-local-cmake-ownership -refactor.md`. Independent of other accepted proposals.

**Evidence:** `app/weather_service.h:22–69` already exposes a service with injected `IHttpClient`. `weather_service.cpp:145–313` combines scheduling, coordinate parsing, provider decoding, and requests. Root CMake compiles it into `draxul-app`; `http_weather_tests.cpp` runs in the broad app suite. App reload stops/restarts it at `app/app.cpp:704–709`.

**Boundary:** Add `draxul-weather STATIC`, preserving `WeatherService` and HTTP injection. Public dependency is `draxul-http`; logging/types and JSON remain private. App retains configuration and lifetime ownership. Synchronous coordinate/provider parsing stays private. No network-policy or feature changes.

#### Boundary verification

- [x] Inventory lifecycle, display formatting, transport limits, cancellation, and reload callers.
- [x] Confirm no App/host/render dependency is required.
- [x] Keep offline/proxy/cache work in `kanban/ice-box/49 network-privacy-controls -feature.md`.

#### Implementation and migration

- [x] Move the service and its single public header to the library; update callers without duplicate forwarding headers.
- [x] Link `draxul-app` to the new target and remove its direct service source.
- [x] Extract synchronous coordinate/geocoding/current-weather decoding while retaining request and refresh behavior.
- [x] Move HTTP/weather cases out of the app test source partition.

#### Unit tests

- [x] Add `draxul-test-weather` with deterministic malformed/wrong-type/range/provider-response cases.
- [x] Retain injected-transport URL encoding, response-size limits, cancellation, and restart coverage.
- [x] Retain `app_smoke_tests.cpp` weather add/change/clear integration coverage.
- [x] Enable `cmake --build <cache> --config Debug --target draxul-test-weather --parallel`.
- [x] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-weather-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [x] Preserve Foundation/WinHTTP ownership in `draxul-http`, including cancellation and stop/join order.
- [x] Verify temperature/emoji formatting and configuration reload on macOS.
- [ ] Verify temperature/emoji formatting and configuration reload on Windows CI.
- [x] Run `python3 do.py test debug`, then `python3 do.py smoke debug --skip-build`.
- [x] Confirm the weather target has no Vulkan/Metal dependency and chrome presentation remains unchanged.

#### Agent documentation/tooling

- [x] Register focused tests in core aggregates, `do.py`, and selection tests.
- [x] Update the module map and document service-versus-App ownership.
- [x] Preserve documented `weather_location` behavior.

#### Acceptance criteria

- [x] Weather tests build without `draxul-app` or product targets.
- [x] Response parsing can be tested without worker polling.
- [x] Fetch cadence, transport policy, formatting, and reload behavior remain compatible.
