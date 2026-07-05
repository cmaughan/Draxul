# Safe, cancellable network transport

**Type:** bug
**Priority:** 00
**Raised by:** GPT/Codex, Claude, Gemini

## Problem

`app/weather_service.cpp`, `satview_catalog_service.cpp`, and `satview_cloud_service.cpp` build shell command strings and execute `curl` through `popen`. `weather_location` can break out of the quoted URL, shutdown can wait for the command timeout, and weather JSON is parsed with fragile substring searches.

## Implementation plan

- [ ] Introduce `libs/draxul-http` with an injected `IHttpClient`, request/response structs, explicit connect/overall deadlines, byte limits, and a cancellation token.
- [ ] Implement Windows with WinHTTP and macOS with `NSURLSession`; keep platform types private and expose one synchronous worker-facing contract whose cancellation is bounded.
- [ ] Add complete RFC 3986 query encoding to the shared library; never pass URLs through a command shell.
- [ ] Add a real JSON parser dependency for weather responses and reject missing, wrong-type, non-finite, or out-of-range fields.
- [ ] Inject the transport into `WeatherService`, `SatViewCatalogService`, and `SatViewCloudService`; retain fake-fetch injection used by existing SatView tests.
- [ ] Remove all three `popen`/`curl` wrappers and temporary stderr-file handling.
- [ ] Make `stop()` cancel outstanding requests before joining; document and enforce a shutdown deadline.
- [ ] Update CMake for both platforms and `docs/features.md` if network prerequisites or behavior change.

## Tests

- [ ] Add hostile weather-location cases covering quotes, shell metacharacters, Unicode, empty input, and reserved URL characters.
- [ ] Add weather JSON cases for minified input, escaped strings, missing fields, wrong types, non-numeric numbers, and oversized responses.
- [ ] Use a blocked fake transport to prove all three services stop within the agreed deadline.
- [ ] Preserve SatView cache/fallback tests and verify cancellation does not discard last-good data.

## Acceptance criteria

- [ ] `rg "popen|_popen" app modules/satview` finds no network transport.
- [ ] No user or downloaded string is interpreted by `cmd.exe` or `/bin/sh`.
- [ ] Weather and SatView retain current success/cache behavior on Windows and macOS.
- [ ] Build `draxul` and `draxul-tests`, run focused network tests, `ctest`, and `py do.py smoke`.

## Dependencies and parallelism

Independent reliability root. Blocks `49 network-privacy-controls -feature.md` and part of the health center. A network-focused sub-agent can own this, but one agent should control the shared transport API and both migrations.

<model>GPT-5 Codex</model>
