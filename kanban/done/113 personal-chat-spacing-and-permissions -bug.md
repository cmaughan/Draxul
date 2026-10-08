# Tighten personal chat layout and handle explicit permissions

Chat bubbles leave excess space below their last line and button labels sit too high. Use font metrics for compact balanced layout. Personal chats already use Approve for me, but explicit filesystem/network permission requests currently return unsupported; route those requests through the existing user approval controls without expanding ordinary agent permissions.

- [x] Tighten bubble padding and centre button labels; inspect the render result.
- [x] Support explicit permission Allow/Decline replies with bounded scope and regression coverage.
- [x] Document permission limits and complete aggregate, same-cache smoke and render validation.


## Implementation

Removed the extra single-line height floor and final-line leading from bubble measurement, and reduced vertical padding from 12 to 8 logical pixels. Buttons use a compact minimum height with font-relative sizing and centre/middle text alignment; the composer button is vertically centred. Existing wrapped-text line spacing remains readable. Visually inspected the updated personal-assistant reference.

Explicit `item/permissions/requestApproval` requests use the personal chat approval UI. An Allow response returns exactly the requested permission object with `scope: turn`; Decline returns an empty permission object. Requests must match the active personal thread and turn; stale user replies remain rejected. Existing command/file approvals retain their decision response. Fresh/resumed provider settings remain workspace-write / on-request / auto_review. No ordinary agent permission settings or machine-wide account/OS permissions changed.

Verified response shape against the installed Codex generated JSON schema and [official app-server documentation](https://learn.chatgpt.com/docs/app-server). Extended the existing real-pipe fake-provider integration to exercise network plus filesystem permission Allow/Decline, stale replies and turn-scoped results. OS privacy consent and provider sign-in remain user-controlled prerequisites, documented in docs/personal-assistant.md.

## Validation

2026-10-07: final `python3 do.py test release` passed 56/56 core CTest entries in 23.51 s. One same-cache aggregate target build took 8.49 s; no configure/generate rerun or focused test overlap. An initial build failed on a new fake-provider JSON initializer brace and was corrected before tests ran. Isolated same-cache personal startup smoke passed in 0.51 s. Final personal-assistant render comparison passed in 1.39 s, against the intentionally updated reference.

Two earlier render captures accidentally received live keyboard input in the visible test window and failed comparison; the clean final reference used `SDL_WINDOW_ACTIVATE_WHEN_SHOWN=0 SDL_WINDOW_ACTIVATE_WHEN_RAISED=0 python3 do.py blesspersonal release --skip-build`. Inspected `/tmp/draxul-chat-final.png`; no stray input, labels centred, bubbles compact. No tolerance changes. Logs: `/tmp/draxul-chat-polish-tests.log` and `/tmp/draxul-chat-polish-render.log`. `git diff --check` clean. Windows/remote CI not run; portable protocol coverage remains in regular CI.

The live personal providers were doing scheduled work at completion, so they were left running. The rebuilt changes take effect after the next GUI/server restart. Changes remain uncommitted/unpushed.
