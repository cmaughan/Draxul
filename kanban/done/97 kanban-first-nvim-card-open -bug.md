# Load the Kanban card on the first Neovim split

**Summary:** Pressing Enter on a Kanban card creates a Neovim split but loses the
file-open request on the first launch. Retain the requested action until the new
pane exists, so the card opens without requiring another Enter.

- [x] Trace and reproduce the initial split/open delivery failure.
- [x] Fix delivery for delayed server-backed pane creation, preserving board focus.
- [x] Cover first open, repeated opens and relevant rejection/cleanup behavior.
- [x] Run core aggregate, same-cache smoke and Release startup; record costs and limits.
- [x] Update feature documentation and hand off the completed change.

Reported by the user on 2026-10-04. Local split creation is synchronous, while
shared-server topology creation can return before the client projects its host.
`App::dispatch_to_nvim_host()` treated an enqueued shared split as though its host
already existed. With no returned pane ID it fell back to the focused Kanban
pane, sent the Neovim action there and silently lost it. Later Enter presses
found the already-projected Neovim host and worked.

The split now carries a caller correlation ID. App retains its requested actions
until the command completion's revision has been applied, then dispatches to the
exact created Neovim pane. Further requests in that tab reuse the pending split
and retain their action order. Background opens preserve current focus rather
than allowing normal split activation to focus the editor. Rejected commands,
server epoch changes and shutdown clear the retained actions. Local synchronous
split behavior is preserved; this requires no Neovim timer or protocol change.

Regression coverage uses a real asynchronous ControlServer and App with fake
window/renderer/hosts. Five scenarios exercise first open, repeated opens while
the server is paused, rejected split followed by a new request, transient host
initialization failure, and explicit editor focus. Assertions check the target
and action order, absence of extra splits, subsequent reuse, and keyboard routing.
The original App implementation fails all five generated scenarios (10 failed
assertions, 15.72 seconds); the fixed implementation passes. The original source
was restored only temporarily for that proof, then the fix was restored before
aggregate validation. `docs/features.md` documents the first-open behavior.

Validation on Windows, 2026-10-04:

- Configure/generate: both existing Ninja caches reused; no reconfigure.
- Focused iteration: `py do.py test debug --target draxul-test-app --catch
  "[app_dispatch]"` passed 202 assertions in five test cases, including the five
  generated server scenarios, in 0.95 seconds. An earlier test-source compile
  error needed one retry; focused compilation was not separately timed. The
  subsequent original-code run was an intentional negative regression proof.
- Debug aggregate build: one `draxul-tests-core` target, 16 incremental Ninja
  build/staging steps, 25.03 seconds. `py do.py test debug` ran 56 CTest entries
  in 63.53 seconds (89.15 seconds for the whole workflow): 54 passed, two already
  tracked failures. Both App shards and both Kanban shards passed.
- Existing aggregate failures: joined family emoji shapes to seven glyphs;
  see [63 joined-family-emoji-fallback -bug.md](../pending/63%20joined-family-emoji-fallback%20-bug.md).
  Windows test-scope integration omits its fixture labels; see
  [64 windows-test-scope-selection -bug.md](../pending/64%20windows-test-scope-selection%20-bug.md).
  No font or test-selection implementation changed in this fix.
- Same-cache smoke: `py do.py smoke --skip-build` passed on retry, 32.55 seconds
  for the workflow. The initial smoke overlapped the aggregate and exceeded the
  process's 30-second timeout (34.81 seconds including cleanup); its wrapper
  stopped only its owned process tree. The retry ran after aggregate completion.
- Render: `draxul-render-basic-view` passed, one Neovim scenario, 3.49 seconds
  CTest time (3.65 seconds including invocation). No references were blessed.
- Release: `py do.py run release --console -- --smoke-test` passed, 34.57 seconds
  total: 26.33 seconds for the app build with 13 Ninja steps, approximately
  8.24 seconds for launch/startup and wrapper overhead.
- No macOS or remote CI run was performed locally. The change uses the shared
  topology/App path without platform-specific implementation. No additional
  product suites were run; the normal build staged its configured plugins.

Detailed logs remain in the ignored Debug/Release build trees as
`kanban-first-open-*.log`. The fix is complete; the two existing suite failures
remain owned by their separate pending cards.
