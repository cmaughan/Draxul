# Config reload under host activity

**Type:** test
**Disposition:** Covered under the main-thread reload contract.

`tests/app_smoke_tests.cpp` covers reload from disk, malformed reload retaining the
previous configuration, all-or-old behavior when font application fails, weather
lifecycle, inactive-tab propagation, and multi-pane propagation. The discarded
proposal to invoke input callbacks from a background thread would violate the app's
main-thread event contract.
