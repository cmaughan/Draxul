# Concrete-host lifecycle fault coverage

**Type:** test
**Priority:** 16

`IHost` has no generic lifecycle state/result contract, so exercising a `FakeHost` would
only test the fake. Add cases only where a concrete host exposes an observable risk.

- [ ] Inventory partial-initialization, idempotent shutdown, early viewport, and post-exit
      behavior for NvimHost, RemoteTerminalHost, PluginHost, and built-in grid hosts.
- [ ] Reuse existing concrete-host fixtures and add only uncovered fault transitions.
- [ ] Verify partial initialization cannot leak callbacks, processes, subscriptions, or
      render passes.
- [ ] Run available sanitizer and platform lifecycle coverage.
