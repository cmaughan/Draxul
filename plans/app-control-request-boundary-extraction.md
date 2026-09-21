# App control-request boundary extraction

This note records the compatibility contract and the remaining integration plan
for `kanban/done/91 app-control-request-boundary -refactor.md`. It describes
the local UI endpoint only. Server control routing remains authoritative for
server-backed Sessions and is outside this extraction.

## Current request inventory

The existing `ControlRequestRouter` continues to own the read path and unknown
method fallback:

| Method | Validation / bounds | Result or error behavior |
|---|---|---|
| `system.hello` | none | protocol, Session id, and capability list |
| `space.list` | none | all local Space summaries |
| `space.get` | integer `id` | Space summary or `not_found` |
| `agent.list` | none | all current local agent projections |
| `agent.get` | non-empty string `instance_id` | agent projection or `not_found` |
| `agent.explain` | non-empty string `instance_id` | identity/status explanation or `not_found` |
| `pane.read` | non-empty `pane_id`; `lines` defaults to 50 and is limited to 1..200 | bounded terminal observation, `unsupported`, or `not_found` |

The new `ControlRequestPolicy` owns validation, limits, response construction,
and error mapping for the effectful path:

| Method | Validation / bounds | Callback result mapping |
|---|---|---|
| `pane.focus` | non-empty `pane_id` | `not_found`, `focus_failed`, or route + `active=true` |
| `pane.action` | non-empty `pane_id` and action | `not_found`, `not_running`, `action_rejected`, or dispatched action |
| `plugin.reload` | non-empty `plugin_id` | `reload_failed` / `reload_rolled_back`, or generation/counts/warning |
| `pane.report_agent_session` | six non-empty strings plus unsigned version/sequence; known kind; shared semantic validation | `routing_mismatch`, `invalid_session_ref`, `duplicate_session_ref`, `stale_report`, or refreshed agent |
| `space.focus` | integer `id` | `not_found` or active Space id |
| `agent.start` | string profile; at most 64 args; each at most 4096 bytes; optional string cwd | `not_found`, `start_failed`, or refreshed agent |
| `agent.focus` | string `instance_id` | `not_found`, `focus_failed`, or refreshed agent |
| `agent.restart` | string `instance_id` | `not_found`, `restart_failed`, or refreshed agent |
| `agent.send_text` | string `instance_id`; string text at most 64 KiB | route/host errors, `input_failed`, or refreshed agent |
| `agent.send_keys` | string `instance_id`; at most 64 string keys accepted by `encode_agent_keys` | route/host/encoding errors, `input_failed`, or refreshed agent |
| `agent.wait` | string `instance_id`; optional unsigned generation; optional string array `until` | replacement completion, predicate result, or route/argument errors |
| `event.subscribe` | optional unsigned cursor; optional unsigned limit clamped to 1..128 (default 64) | event journal page |

## Compatibility-sensitive ordering

The extraction preserves these observable sequences:

1. `pane.action` validates `pane_id`, locates the pane, then validates `action`,
   then checks for a live host, dispatches, and requests a frame.
2. `pane.focus` activates Space then tab, focuses the leaf, refreshes shell
   layout, updates input routing, marks the Session dirty, and requests a frame.
3. `pane.report_agent_session` validates field shapes and ref kind, checks the
   pane/agent route, validates the session reference, scans for duplicates,
   performs stale-sequence acceptance, marks the Session dirty, then reads the
   updated agent projection.
4. `agent.start` validates `profile_id` and optional `space_id`, activates the
   requested Space, then validates `args` and `cwd`, launches, and reads the
   resulting agent. A bad later argument therefore does not undo Space focus.
5. All agent focus/restart/input/wait methods look up the agent before their
   method-specific validation. Input and wait then resolve the projected pane;
   a missing pane wins over malformed payload fields. A missing live host wins
   over malformed input fields, while wait does not require a live host.
6. `agent.wait` handles runtime-generation replacement before `until`, defaults
   to `blocked`, `done`, `exited`, or `failed`, and chooses a matching status as
   the outcome before a matching lifecycle.

## App adapter mapping after the shared source lock

`App::handle_control_request` will construct `ControlRequestPolicy::Operations`
on the main thread and call the policy. Each operation maps to existing App
behavior without passing `App&` into the policy:

- `read`: the existing `ControlRequestRouter` instance.
- `find_pane`: the current Space/tab/leaf scan, returning only pane and route
  values plus whether the host is live.
- `focus_pane`: existing activate-Space/activate-tab/focus/layout/input-route/
  dirty/frame sequence.
- `dispatch_pane_action`: `IHost::dispatch_action`, requesting a frame only on
  acceptance.
- `reload_plugin`: `App::reload_plugin`, copied into the policy's value result.
- `inspect_native_route`: current exact pane id + instance id + agent-kind scan.
- `accept_native_session`: current duplicate scan and
  `PaneManager::set_agent_session_ref`, marking the Session dirty on acceptance.
- `focus_space`: `App::activate_space` and its existing error text.
- `launch_agent`: `App::launch_agent` and its existing error text.
- `find_agent`: a fresh `AgentController::query` lookup.
- `focus_agent`: `AgentController::focus`, then layout/input-route/dirty/frame.
- `restart_agent`: `App::restart_agent_runtime`.
- `inspect_agent_route`: current Space/tab/leaf host resolution, distinguishing
  replaced route from no live host.
- `send_agent_input`: `IHost::send_agent_input` on the resolved projection.
- `read_events`: `ControlEventJournal::read_after`.

Then add `control_request_policy.cpp` to `app/CMakeLists.txt` and replace the
large policy body in `app/app.cpp` with the adapter construction. No public API,
production library, renderer/window callback type, or server routing changes are
needed.

## Direct policy tests

Add one new source to the existing `draxul-test-app` inventory. The fixture uses
only fake `Operations`; it constructs neither `App` nor graphics/font/transport
fixtures. The existing app test binary still links `draxul-app`, so this is
runtime isolation rather than a graphics-free build claim.

The direct cases should cover:

1. fallback/read forwarding and every malformed required field;
2. pane lookup-before-action validation and dispatch/focus response mapping;
3. reload success, failure, and rollback error codes;
4. native route-before-semantic-validation order, duplicate, stale, and accepted
   callback order;
5. `agent.start` Space activation before bad args/cwd, plus all argument bounds;
6. missing/replaced/no-host agents, text/key limits, encoded key bytes, rejected
   input, focus, restart, and readback;
7. wait generation replacement, default predicates, explicit predicates, status
   precedence, and a stopped host still being waitable;
8. event cursor defaults, limit clamp, invalid types, and response passthrough.

Retain the current endpoint/main-thread integration case in
`tests/control_plane_tests.cpp`; it verifies real transport pumping, host effects,
native-session projection, wait response, and event delivery.

## Validation after integration

Use the existing Debug cache and run, in order:

1. build `draxul-app` and `draxul-test-app` in parallel;
2. run the `draxul-test-app` shards while iterating only if this materially
   shortens diagnosis;
3. run the required aggregate `python3 do.py test debug`;
4. run the same-cache `python3 do.py smoke --skip-build`;
5. run `python3 do.py run release` for the completed refactor confirmation.

Update `docs/module-map.md` with the policy/effect ownership boundary, tick the
card, and move it to `kanban/done/` only after the aggregate and smoke gates pass.
