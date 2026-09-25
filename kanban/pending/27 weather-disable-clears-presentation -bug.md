# Clear stale weather presentation when disabled
**Severity:** MEDIUM  
**Source:** Codex #28; `libs/draxul-weather/src/weather_service.cpp:49`.

Clearing `weather_location` stops the worker but retains its published emoji and temperature, leaving a permanently stale chrome pill.

**Investigation**

- [x] Trace weather state through location changes, cancellation, worker completion, and chrome presentation.

**Fix strategy**

- [x] Clear published state safely when disabling/changing location, or gate presentation explicitly.
- [x] Prevent a completing old worker from republishing stale location data.

**Acceptance criteria**

- [x] Service tests show disabling/switching locations clears old values, and re-enabling publishes fresh values.
- [x] Run the Windows core/product aggregate and a same-cache startup smoke with a sufficient bound.
- [ ] Confirm in the live UI that config reload removes the pill and a new location shows only fresh data.

---

Authorship: `gpt-6-astra`.

**Implementation notes:** App reload already calls `stop()` before `start()`
when `weather_location` changes, and chrome reads the service's separate
emoji/temperature accessors. `stop()` now cancels and joins before clearing
those values and `has_data`; the worker also checks `running_` after fetch and
under the publication lock so a cancelled response cannot republish. Transport
tests assert old values disappear, remain absent while a new location fetch is
pending, and reappear only with the new location's result.

**Validation (Windows, 2026-09-25):** `py do.py test debug --products`
passed 49/49 CTest cases in 238.19s. The standard same-cache smoke exceeded
its fixed 30s limit while restoring the existing nine-pane session; the
identical wrapper environment passed with a 90s bound in approximately 45s.
Live chrome confirmation remains pending.
An isolated Windows Debug UI was launched with `weather_location = "London"`,
but the headless control route exposes no rendered chrome state and the window
was hidden. No pill appearance/removal was visually observed, so the live UI
acceptance checkbox remains open.
