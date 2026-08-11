# Agent cards and token activity

**Status:** planned  
**Date:** 2026-08-01  
**Scope:** replace the current Agents pill rows with compact cards showing pane,
agent, model, semantic status, session cost, token rate, and a TokenFu-derived
activity coin  
**Related:** [local agent harness](local-agent-harness-implementation.md),
[server/client runtime](server-client-terminal-runtime.md), and
[Herdr research](herdr-agent-harness-research.md)

## Goal

The Agents section should become a glanceable operational view rather than a list
of names followed by status text. Each agent is represented by a rounded card:

```text
+------------------------------------------------+
|   (coin)   PowerShell                      ●    |
|            Codex · GPT-5.6 Sol                  |
|            $0.42  ·  18.6k tok  ·  240 tok/s   |
+------------------------------------------------+
```

The card answers four questions without opening the pane:

1. Where is it? — the associated pane's resolved display name.
2. What is it? — agent name and current model.
3. Does it need attention? — a small colored status light.
4. How hard is it working and what has it cost? — token rate, session total,
   estimated session cost, and the spinning coin.

Clicking anywhere on the card keeps the existing behavior: focus the owning
Space, tab, and pane.

## Current state

Draxul already has the important ownership boundary:

- `ServerAgentService` is the Session-scoped authority for server-owned agent
  discovery, lifecycle, and semantic status.
- `ServerAgentProjection` is revisioned, sanitized, and shared by every UI.
- `AgentController` maps that server projection back to local Space/tab/leaf
  identities and focuses the correct pane.
- `ChromeHost` converts the projection into `ChromeAgentInput`; the current
  layout then renders each agent as a one-row segmented pill.
- Pane labels are already stable and resolved through `PaneManager`; they should
  be reused rather than duplicated in the agent protocol.

The current projection does **not** contain model or usage information. It also
does not expose the resolved pane display label directly. The existing
`AgentSessionRef` is the key that can safely associate an agent with native
Codex or Claude session telemetry.

## TokenFu findings

The relevant TokenFu implementation is under `D:/dev/tokenfu`:

- `src/coin_mesh.h`, `shaders/coin.vert`, and `shaders/coin.frag` define the
  beveled 3D coin used on Windows.
- `src/coin_renderer_vulkan.cpp` and the `MetalCoinRenderer` in
  `src/renderer_metal.mm` provide equivalent Vulkan and Metal rendering.
- `src/local_usage.cpp` and `src/claude_usage.cpp` incrementally tail local
  Codex and Claude JSONL session data and calculate token totals and cost.
- `src/session_watcher_win32.cpp` and `src/session_watcher_macos.mm` provide
  debounced native filesystem notifications with a reconciliation path.
- `App::observe_usage_refresh()` turns timestamped token deltas into a rolling
  rate. It retains up to ten intervals and linearly decays the displayed rate
  to zero over one minute.
- `update_coin()` maps activity up to 50,000 tokens/second onto approximately
  0.1–3 rotations/second, eases toward the target speed, and settles the coin
  face-on when activity stops.

TokenFu currently aggregates usage by provider and model. Draxul must not put
those aggregate values on every agent card. Two simultaneous Codex agents need
independent session totals, models, rates, and costs. Draxul therefore reuses
the parsing, pricing, rate, and animation ideas but introduces a session-keyed
service owned by the server.

## Design decisions

### Server owns telemetry; clients own animation

The server already owns agents and serves multiple UIs. It should scan each
known native agent session once and publish sanitized usage values. Every UI
then animates its own coin from the same published token rate.

This avoids:

- every open UI scanning the same files;
- clients disagreeing about totals or the current model;
- filesystem paths and raw session content crossing the protocol;
- animation phase becoming shared state.

Only numeric usage summaries and a display-safe model name cross the wire.
Coin angle and angular velocity remain ephemeral client presentation state.

### Native session identity is required for attribution

Per-agent cost and model are only authoritative when the agent has an
`AgentSessionRef`. Official Codex/Claude hooks already report this reference.

If no native reference is available, the card still shows pane, agent, status,
and lifecycle, but displays an unavailable usage state (`—`) and leaves the
coin settled. Provider-wide TokenFu totals must never be guessed onto a single
agent.

### The card is structured data, not a formatted status string

Replace `ChromeAgentInput::status_suffix` with structured values. Formatting
belongs in a pure presenter/layout layer so narrow widths, estimated prices,
unknown models, and lifecycle states are deterministic and testable.

Suggested client-side view model:

```cpp
struct AgentCardView
{
    std::string instance_id;
    std::string pane_name;
    std::string agent_name;
    std::string model_name;
    AgentLifecycle lifecycle;
    AgentStatus status;
    bool attention;
    bool focused;
    std::optional<uint64_t> session_tokens;
    std::optional<double> session_cost_usd;
    std::optional<double> tokens_per_second;
    bool cost_estimated = false;
};
```

The app resolves `pane_name` from the projected pane route and current
`PaneManager::pane_display_name()`. It is not repeated in the server agent
protocol because topology already owns pane naming.

### Usage is optional and revisioned

Add an optional sanitized value to `ServerAgentProjection`:

```cpp
struct AgentUsageProjection
{
    std::string model_display_name;
    uint64_t input_tokens = 0;
    uint64_t cached_tokens = 0;
    uint64_t output_tokens = 0;
    uint64_t total_tokens = 0;
    double estimated_cost_usd = 0.0;
    double tokens_per_second = 0.0;
    bool cost_estimated = false;
    uint64_t usage_revision = 0;
};
```

The fields describe the native conversation/session associated with that card,
not today's provider total. If a session uses several models, totals and cost
cover the complete session while `model_display_name` shows the latest model.

The optional field keeps older checkpoints and protocol messages compatible.
Usage is live derived state and does not need to be written into durable Session
topology; it can be reconstructed from the native session on server restart.

### Status light is separate from selection

The card background/outline communicates focus. The small circular light
communicates semantic status:

| State | Light | Additional treatment |
|---|---|---|
| starting / unknown | muted grey | none |
| idle | palette blue | none |
| working | green | coin responds to measured rate |
| blocked / input needed | amber | subtle attention ring |
| done | teal | attention ring until focused |
| failed | red | persistent until restart/dismiss |
| exited | dim grey | card and coin desaturated |

Color must not be the only discoverable representation. Hovering the light or
card should expose the textual status and explanation in a tooltip. That text
does not occupy permanent card space.

## Card layout

Use a pure `AgentCardLayout` calculation in `chrome_layout.*`; NanoVG draws the
rounded card, status light, outlines, and metrics decoration, while the existing
grid text layer draws glyphs. The activity coin is a separate 3D render pass.

Default geometry at a 20 px cell height:

- outer horizontal inset: 6 px;
- gap between cards: 6 px;
- corner radius: 7 px;
- normal height: three text rows, about 60 px;
- coin diameter: 32–36 px, vertically centered on the card;
- status light: 7 px diameter in the upper-right corner;
- text starts after the coin plus a 7 px gap;
- card contents are clipped to the Agents section and card rectangle.

Text hierarchy:

1. Pane name — strongest foreground color, one line, ellipsized.
2. Agent name `·` model — secondary foreground, one line, ellipsized.
3. `cost · total tokens · tok/s` — compact metrics color.

Estimated prices use `~$0.42`; unavailable values use `—`, never `$0.00`.

Responsive tiers:

| Available card width | Layout |
|---|---|
| 190 px or wider | 34–36 px coin, all three lines |
| 150–189 px | 28–30 px coin; metrics reduce to cost and rate |
| below 150 px | 22–24 px coin; two text lines; rate becomes tooltip-only if needed |

The existing configurable sidebar width remains authoritative. Tests pin all
three tiers at 1x and 2x display scale and with wide Unicode labels.

Because cards consume more height than pills, the Agents section must become a
clipped, vertically scrollable list. Store scroll offset as client-local
presentation state. Wheel events over the Agents section scroll cards; clicks
use the final clipped card hit rectangles. Layout work remains proportional to
the visible cards.

## Activity coin architecture

Create a small reusable target, tentatively `libs/draxul-activity-coin/`, rather
than embedding backend code in `app/chrome_vector_pass.cpp`.

It owns:

- TokenFu's renderer-neutral coin mesh generation;
- renderer-neutral `ActivityCoinInstance` values containing card rectangle,
  provider style, angle, and opacity;
- the rate-to-speed and settle animation state machine;
- a Vulkan `IRenderPass` implementation;
- a Metal `IRenderPass` implementation;
- the coin shaders and resource lifecycle.

`ChromeHost` owns one animation state per `(instance_id, runtime_generation)`.
Generation changes reset live speed and angle; disappearing cards prune state.
Changing native session reference retains nothing from the previous session.

The pass records after card backgrounds/text are laid out and clips each coin to
its card. It must not require a separate window or swapchain. Render scheduling
requests approximately 30 fps only while at least one visible coin is spinning
or settling; a completely stationary list returns to event-driven rendering.

Provider styling can retain TokenFu's Codex blue and Claude coral initially,
but colors should enter Draxul's theme/config surface rather than remain shader
constants. Unknown providers use a neutral coin mark and palette.

## Session usage service

Add a renderer-free `AgentUsageService` below `app/`, preferably alongside the
server agent service. It receives the currently projected agents and watches only
the native sessions they reference.

Responsibilities:

1. Resolve a Codex/Claude `AgentSessionRef` to a local session file without
   publishing that path.
2. Incrementally tail new JSONL bytes with bounded pending buffers.
3. Record the current/latest model, typed token totals, and priced cost.
4. Produce timestamped token deltas for the rolling-rate calculation.
5. Publish immutable, sanitized per-session snapshots to `ServerAgentService`.
6. Debounce native file notifications and periodically reconcile missed or
   overflowed events.
7. Remove watchers and transient rate state when no live/restorable agent refers
   to a session.

Start from TokenFu's proven parsers, pricing rules, watcher behavior, and fixtures,
but narrow the output to a keyed native session. Keep a source-provenance comment
and parity fixtures so future pricing/parser changes can be compared between the
projects. Do not introduce a runtime dependency on the TokenFu executable.

Longer term, if both products continue evolving the same parsing code, extract a
small shared source library. Doing that before the Draxul session-keyed API is
stable would couple two repositories prematurely.

## Phased vertical implementation

### Phase 0 — static cards and status lights

Replace one-row agent pills with rounded, structured cards using existing agent
data only.

- Introduce `AgentCardView` and pure responsive layout calculations.
- Resolve and display the owning pane's current display name.
- Display agent name; show `Model unavailable` until telemetry exists.
- Replace persistent status suffix text with the colored status light.
- Preserve focused/attention treatment and whole-card click-to-focus.
- Add clipped vertical scrolling and stable hit testing.
- Update snapshots and `docs/features.md`.

Acceptance:

- managed and manually detected agents render as cards;
- pane rename immediately updates its card in both clients;
- status transitions change only the light/attention treatment;
- ten synthetic cards remain navigable in a short Agents region;
- no token data is invented.

Tests:

- pure card layout at narrow/default/wide widths and 1x/2x scale;
- Unicode truncation and absent fields;
- status-to-light palette mapping;
- scrolled hit regions still focus the correct agent;
- two-client projection retains identical structured card data.

### Phase 1 — per-agent model, tokens, and cost

Build the server-owned session usage service and extend the optional wire
projection.

- Port/refactor TokenFu Codex and Claude incremental session parsing.
- Resolve usage strictly through `AgentSessionRef`.
- Publish latest model, typed/session total, session cost, estimation flag, and
  usage revision.
- Render the real model and metrics line in each card.
- Use `~` for fallback pricing and `—` for unavailable telemetry.

Acceptance:

- two simultaneous Codex sessions show independent totals and costs;
- a model switch updates the model label while retaining the session total;
- controller and observer converge on the same usage revision;
- stopping/restarting the UI does not create another scanner;
- restarting the server reconstructs totals from native files.

Tests:

- Codex and Claude session fixtures with known model/token/cost outcomes;
- two interleaved sessions never cross-attribute usage;
- malformed/truncated records degrade to partial/unavailable safely;
- protocol round-trip, size limits, backward-compatible missing usage;
- watcher overflow/reconciliation and clean server shutdown.

### Phase 2 — static TokenFu coin port

Port the exact TokenFu mesh, provider marks, lighting, and Vulkan/Metal appearance
into the reusable Draxul render pass. Render coins face-on with no animation yet.

Acceptance:

- every visible card has one correctly clipped coin;
- Codex and Claude provider styles match TokenFu closely;
- resize, sidebar-width changes, scrolling, and multi-client windows do not leak
  or misplace GPU resources;
- unknown providers have a neutral fallback.

Tests:

- mesh invariants and bounded instance list;
- backend initialization/shutdown/resize smoke;
- Windows and macOS render snapshots for default/narrow/scrolled cards;
- no coin draw calls for clipped cards.

### Phase 3 — live token rate and spinning speed

Port TokenFu's rolling interval calculation, one-minute decay, eased angular
velocity, and face-on settle behavior.

- Publish per-session `tokens_per_second` from the server.
- Key client animation by agent instance plus runtime generation.
- Request frames only while visible coins are moving or settling.
- Show the formatted rate in the card metrics line.
- Preserve a brief filesystem-activity hint only as a clearly lower-confidence
  animation hint; cost and numeric rate remain measurement-derived.

Acceptance:

- giving one of two agents work spins only its coin;
- higher measured token rate produces visibly faster rotation;
- the displayed rate and coin decay smoothly after output stops;
- the coin settles face-on and Draxul returns to event-driven rendering;
- reconnect starts from the server's current rate without sharing animation phase.

Tests:

- deterministic fake-clock rate window and decay tests copied from TokenFu's
  behavior;
- deterministic speed mapping, easing, wrap, and settle tests;
- generation/session changes reset the correct coin only;
- idle `next_deadline()` is empty; moving coins schedule bounded frames;
- two-client rates match while angles may differ.

### Phase 4 — polish and operational detail

- Hover tooltip: textual status, authority/evidence, full pane/agent/model names,
  typed token counts, exact/estimated price note, lifecycle, and runtime age.
- Optional compact/comfortable density setting if the responsive default proves
  insufficient.
- Keyboard focus/selection treatment and accessible non-color status cues.
- Optional sorting/filtering after observing real usage; preserve stable
  Space/tab/pane order by default.
- Performance counters for scan latency, projection size, visible card count,
  and coin pass time in diagnostics—not in pane labels.

## Multi-client acceptance script

1. Start two `build-ninja-release/draxul.exe` UIs against the default server.
2. Launch two managed agents in different panes and give only one a task.
3. Confirm both UIs show the same pane, agent, model, cost, token total, rate,
   status light, and ordering.
4. Confirm only the active agent's coin accelerates; coin angles need not match.
5. Rename the owning pane in UI A; both cards update without agent restart.
6. Focus a card in UI B; B navigates to the owner without moving A's local focus.
7. Resize and scroll both sidebars independently; hit testing remains correct.
8. Close A, continue the agent, then reopen A; totals/rate catch up from the
   server projection.
9. Stop and restart the server; session totals/model reconstruct from native
   session files and live rate resumes on the next measured delta.

## Risks and safeguards

- **Incorrect attribution:** never fall back from missing session identity to a
  provider-wide total.
- **Pricing drift:** preserve TokenFu parity fixtures and mark fallback prices as
  estimates.
- **Sensitive data:** raw session text, prompts, file paths, and token events stay
  on the server; only sanitized aggregates cross IPC.
- **Render tax:** schedule animation frames only while visible coins move; cull
  clipped cards before submitting instances.
- **Cross-platform drift:** land the static coin only when Vulkan and Metal are
  both implemented and snapshot-tested.
- **Layout crowding:** implement scrolling in Phase 0, not after cards make the
  existing clipped list unusable.
- **Protocol churn:** make usage optional and version/size bounded before adding
  more metrics.

## Recommended first slice

Implement Phase 0 alone: structured rounded cards, pane/agent text, status light,
responsive layout, scrolling, and preserved focus behavior. It produces an
immediately useful UI and establishes the geometry/hit-test contract before GPU
coin work or telemetry expands the protocol.
