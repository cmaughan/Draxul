# Fix recurring disappearance of the macOS server D

**Summary:** Find why the Draxul server icon keeps disappearing from the Mac menu bar and prevent it from happening again so the server controls remain accessible.

**Source:** User report, 2026-09-30.

The server D has disappeared again while the server is running. The previous
placement workaround in `kanban/done/02 macos-server-menu-bar-item-disappears -bug.md`
did not survive subsequent use; that card does not establish a durable fix.

- [x] Capture the current process, menu-bar registration, and display geometry.
- [ ] Establish a reproducible cause and implement or verify a durable repair.
- [x] Verify the corrected position across three isolated server launches while
  preserving the live server and its terminals.
- [x] Verify UI open/close and continued visibility with no UI attached.
- [x] Assess aggregate/smoke requirements: no application/build source changed;
  no aggregate, compilation, render tests, or smoke rebuild required this turn.

## Evidence

Live server PID 52798 started at 11:53 on 2026-09-30; previous PID 35832 exited
shortly before it. Control Center registered `com.draxul.draxul.server-Item-0-52798`
as an ephemeral displayable. No removal for this live host is logged. The helper
defaults contain `NSStatusItem Preferred Position Item-0 = 604`. The built-in
display is 1512 points wide, with a notch exclusion from x=665 to x=850. Current
screenshots and diagnostics are in `/tmp/draxul-menu-investigation-20260930/`.
The running server owns live terminals; investigate with isolated runtime paths.

## 2026-09-30 investigation and live recovery

- Confirmed both Draxul permissions enabled; all other app switches also enabled.
- A Launch Services launch and a separately ad-hoc-signed helper remained
  ephemeral in Control Center. Signing is not established as a cause or fix.
- A minimal native AppKit status item also became clipped beside the notch.
  Its in-process `visible` and window `visible` values remained true even when
  absent from the screenshot, so those properties alone cannot diagnose this.
- Temporarily hiding the Microsoft Teams menu item brought the D and native
  probe into view. Command-dragging the live D beside Spotlight changed its
  saved preferred position from 604 to 218. Restored Microsoft Teams to enabled.
- The live D remained visible after restoring all permissions and stopping every
  diagnostic process. Its menu reported server ready, zero clients, two live
  terminals, one Session, one Space, and two Agents. PID 52798 never restarted.
- A named native probe restored its position after relaunch. The existing SDL
  implementation also restored position 218 in three isolated server launches
  (PIDs 25687, 25719, 25751), each captured after four seconds, with cleanup after
  each. An additional concurrent-server check did not overwrite position 218.
  Thus neither a native rewrite nor a test-instance collision fix is justified
  by current evidence. Those earlier hypotheses were not confirmed.
- All probe servers used unique runtime paths under the diagnostic directory
  and were shut down explicitly. The native probe preference was removed.
- Open Draxul from the recovered live menu launched UI PID 25854 and attached
  to PID 52798. Native Quit followed by 12 seconds with no UI left the D
  visible; reopening and quitting again also preserved it. Final status:
  PID 52798, zero clients, two terminals, two Agents, checkpoint healthy.
  Screenshots: `live-menu.png`, `ui-open.png`, `ui-closed-12s.png`,
  `ui-reopened.png`, and `final.png` in the diagnostic directory above.
  Every menu-bar permission is enabled again. Probe bundle registrations were
  removed. The user's original no-UI state is restored.

Validation cost: three isolated restart checks took approximately 17 seconds;
UI/menu lifecycle checks approximately two minutes including inspection and
screenshots. No configure/generate, compilation, aggregate, render, or remote
CI runs were performed because executable sources were not changed.

**Still unresolved:** What moved/rewrote the previous repaired position back
to 604 before this report. The prior acceptance never covered restarting the
real server. Current placement recovery is verified but must not be described
as a permanent product-code fix. Keep this card pending until the recurrence
trigger and durable prevention are established.
