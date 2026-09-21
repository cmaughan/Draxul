# Dispatch CSI commands by their complete prefix

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-terminal-core/src/terminal_core_csi.cpp:129` recognizes only `?`, while dispatch at `:330` ignores that distinction for cursor restore. Keyboard-protocol sequences such as `CSI ? u` move the cursor; `CSI > 4;2 m` changes SGR, and DA2 receives the wrong response.

**Investigation**

- [x] Replay prefixed keyboard, attribute, and device-query sequences alongside ordinary CSI commands.

**Fix strategy**

- [x] Parse private markers and intermediates separately from numeric parameters.
- [x] Dispatch supported combinations explicitly and ignore unsupported combinations.

**Acceptance criteria**

- [x] Unsupported extensions cannot mutate cursor or rendition state accidentally.
- [x] Existing cursor, SGR, and supported device-query behavior remains correct.
