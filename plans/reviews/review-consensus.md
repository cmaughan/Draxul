All 22 work items (WI 26–47) are in place. Here's a summary of everything produced:

---

## What was produced

### `plans/reviews/review-consensus.md` — full synthesis

The consensus document contains:
- **10 strong/partial agreements** across [C] Claude, [G] Gemini, [P] GPT — each attributed with agent tags
- **Disagreement table** with 4 resolutions (App decomposition scope, MegaCity priority, `void*` strategy, error idiom unification)
- **Stale/already-fixed items** called out explicitly (not re-planned)
- **4-tier recommended fix order** (bugs → tests → refactors → features)
- **Full interdependency map** with sub-agent recommendations
- Signed by `claude-sonnet-4-6`

### 22 new work items (WI 26–47)

| Range | Type | Items |
|---|---|---|
| 26–28 | Bugs | CLI numeric crash, unsafe HostManager downcast, RPC magic timeout |
| 29–35 | Tests | Chord state machine, dirty range coalescing, SDL wake ordering, city DB migration, CLI args, large paste stress, startup perf gate |
| 36–43 | Refactors | `param_or` dedup, window size accessors, App::Deps injection, BackgroundWorker template, IHost split, CMake CONFIGURE_DEPENDS, features.md repair, HostContext named init |
| 44–47 | Features | Pane zoom, pane close/duplicate/restart, chord visual indicator, diagnostics export |

### Key interdependencies flagged

- **WI 41** (cmake CONFIGURE_DEPENDS) → must land before any new test file work (29–35)
- **WI 26** (CLI crash) → **WI 33** (CLI test, regression guard)
- **WI 38** (App::Deps) → **WI 40** (IHost split needs stable App boundary first)
- **WI 29** (chord tests) → **WI 46** (chord indicator, build confidence first)
- **WI 36 + WI 41** — same agent pass (both mechanical, low blast radius)
- **WI 26 + WI 33** — same agent pass (fix + test)
- **WI 44 + WI 45** — sequential (both touch HostManager)
