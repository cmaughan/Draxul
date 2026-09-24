# Clear stale weather presentation when disabled
**Severity:** MEDIUM  
**Source:** Codex #28; `libs/draxul-weather/src/weather_service.cpp:49`.

Clearing `weather_location` stops the worker but retains its published emoji and temperature, leaving a permanently stale chrome pill.

**Investigation**

- [ ] Trace weather state through location changes, cancellation, worker completion, and chrome presentation.

**Fix strategy**

- [ ] Clear published state safely when disabling/changing location, or gate presentation explicitly.
- [ ] Prevent a completing old worker from republishing stale location data.

**Acceptance criteria**

- [ ] Clearing the location and reloading removes the pill; switching locations does not retain misleading old data.
- [ ] Re-enabling weather publishes fresh values; run core aggregate tests and same-cache smoke.

---

Authorship: `gpt-6-astra`.
