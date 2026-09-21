# Bound configuration integers before narrowing

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`libs/draxul-config/src/config_schema.cpp:539` applies only a lower bound for `ClampMin`, then narrows at `:553`. A value such as `chord_timeout_ms = 2147483648` becomes an invalid negative timeout on the supported integer representation.

**Investigation**

- [ ] Identify every minimum-only integer field and test representable boundaries.

**Fix strategy**

- [ ] Apply destination-type bounds before conversion.
- [ ] Keep configured minimum and fallback/clamping semantics explicit.

**Acceptance criteria**

- [ ] Values beyond `int` range cannot wrap into negative or unrelated settings.
- [ ] Ordinary values and documented minimum clamping remain unchanged.
