# Move weather fetching into a focused library

**Priority:** P2 — an existing self-contained service gains independent tests and build ownership.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 7.  
**Owner:** One weather/service agent.  
**Dependencies:** `kanban/pending/00 internal-target-build-policy -refactor.md` and `kanban/pending/01 app-local-cmake-ownership -refactor.md`. Independent of other accepted proposals.

**Evidence:** `app/weather_service.h:22–69` already exposes a service with injected `IHttpClient`. `weather_service.cpp:145–313` combines scheduling, coordinate parsing, provider decoding, and requests. Root CMake compiles it into `draxul-app`; `http_weather_tests.cpp` runs in the broad app suite. App reload stops/restarts it at `app/app.cpp:704–709`.

**Boundary:** Add `draxul-weather STATIC`, preserving `WeatherService` and HTTP injection. Public dependency is `draxul-http`; logging/types and JSON remain private. App retains configuration and lifetime ownership. Synchronous coordinate/provider parsing stays private. No network-policy or feature changes.

#### Boundary verification

- [ ] Inventory lifecycle, display formatting, transport limits, cancellation, and reload callers.
- [ ] Confirm no App/host/render dependency is required.
- [ ] Keep offline/proxy/cache work in `kanban/ice-box/49 network-privacy-controls -feature.md`.

#### Implementation and migration

- [ ] Move the service and its single public header to the library; update callers without duplicate forwarding headers.
- [ ] Link `draxul-app` to the new target and remove its direct service source.
- [ ] Extract synchronous coordinate/geocoding/current-weather decoding while retaining request and refresh behavior.
- [ ] Move HTTP/weather cases out of the app test source partition.

#### Unit tests

- [ ] Add `draxul-test-weather` with deterministic malformed/wrong-type/range/provider-response cases.
- [ ] Retain injected-transport URL encoding, response-size limits, cancellation, and restart coverage.
- [ ] Retain `app_smoke_tests.cpp` weather add/change/clear integration coverage.
- [ ] Enable `cmake --build <cache> --config Debug --target draxul-test-weather --parallel`.
- [ ] Enable `ctest --test-dir <cache> -C Debug -R '^draxul-test-weather-shard-' --parallel 4 --output-on-failure`.

#### Cross-platform validation

- [ ] Preserve Foundation/WinHTTP ownership in `draxul-http`, including cancellation and stop/join order.
- [ ] Verify temperature/emoji formatting and configuration reload on Windows and macOS.
- [ ] Run `python3 do.py test debug`, then `python3 do.py smoke debug --skip-build`.
- [ ] Confirm the weather target has no Vulkan/Metal dependency and chrome presentation remains unchanged.

#### Agent documentation/tooling

- [ ] Register focused tests in core aggregates, `do.py`, and selection tests.
- [ ] Update the module map and document service-versus-App ownership.
- [ ] Preserve documented `weather_location` behavior.

#### Acceptance criteria

- [ ] Weather tests build without `draxul-app` or product targets.
- [ ] Response parsing can be tested without worker polling.
- [ ] Fetch cadence, transport policy, formatting, and reload behavior remain compatible.
