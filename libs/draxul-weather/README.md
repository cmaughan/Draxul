# Weather service

`draxul-weather` owns the renderer-free weather service used by the App chrome.
It resolves either a `lat,lon` location or an Open-Meteo geocoding query, polls
current conditions on a worker thread, and publishes the emoji and rounded
Celsius temperature through thread-safe accessors.

The public boundary is `draxul/weather_service.h`. Its only public dependency is
`draxul-http`, which provides the injectable transport and cancellation contract.
JSON decoding and logging stay private to this library. App owns configuration,
service lifetime, reload stop/start ordering, and presentation in the chrome bar.

Coordinate parsing and provider-response decoding are synchronous private
helpers. The `draxul-weather-test-internals` target exposes their source include
directory only to the focused weather test executable, allowing malformed and
range cases to run deterministically without polling the worker.
