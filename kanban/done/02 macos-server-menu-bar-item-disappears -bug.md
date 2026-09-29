# Keep the macOS server menu bar item visible after UI exit

**Severity:** HIGH  
**Source:** User report, 2026-09-28.

The red D server status item appears on a fresh server launch, then disappears
while the server process continues running. Reopening the UI attaches to the
same server and does not restore the item, leaving the server's menu actions
inaccessible.

**Investigation**

- [x] Reproduce fresh launch, UI exit, and reopen with matching server PID.
- [x] Check whether the UI exit actually stops the server or the tray pump.
- [x] Distinguish server/SDL ownership and disabled permissions from macOS
  menu-bar placement, and verify a supported placement repair.

**Fix and acceptance**

- [x] Include the Draxul icon in the server helper's bundle and plist so its
  separate System Settings entry has an icon.
- [x] Keep the red D and its menu available while the server runs, independent
  of whether a UI client is open.
- [x] Verify fresh server launch, UI exit, and UI reopen on macOS; verify the
  icon remains after at least 10 seconds with no UI.
- [x] Run the scope-appropriate aggregate tests and same-cache smoke; transfer
  the unrelated Kanban test failure to its own tracked follow-up below.

**2026-09-28 evidence:** A fresh UI launch started server PID 48712 and briefly
showed the red D. Quitting UI PID 48709 removed the icon while PID 48712
remained alive; a new UI PID 49122 left the icon absent. A separate launch of
the helper app, with no UI client, also displayed the icon briefly before it
disappeared. The server's main thread continued pumping the SDL tray. A local
diagnostic build found that AppKit still reported the `NSStatusItem` visible,
with an image and visible window, after the icon vanished. Retaining the item
in the pinned SDL Cocoa backend did not change the behavior. All diagnostic
source edits were reverted; the Release app was rebuilt from the original
source. The server has bundle ID `com.draxul.draxul.server`, distinct from the
UI's `com.draxul.draxul`, so the two macOS menu bar permissions must be checked
separately. No fix is claimed yet.

Further checks found that the tray image's rendered TIFF data retained the
same hash for 12 seconds in a subsequent launch, longer than the previously
observed time to disappearance. The display was unavailable for a matching
visual capture on that launch, so this does not prove the image remained
unchanged in the first reproduction. Giving the
status item a fixed AppKit `autosaveName` did not change Control Center's
ephemeral classification. Control Center registered the helper item and did
not log a blocked-host or removal event during the first UI exit/reopen
reproduction. These checks reduce the likelihood of an SDL image lifetime or
server tray-destruction bug; they do not establish why the OS stopped drawing
the item. The user's confirmed “Draxul” allowance may apply to the UI bundle;
the separate “Draxul Server” allowance remains unconfirmed. All temporary
instrumentation and experimental changes were reverted, and the original
Release server was restarted.

**2026-09-29:** User confirmed both **Draxul** and **Draxul Server** are
listed and enabled in Menu Bar settings. Disabled permissions are ruled out.
The helper listing is blank because its bundle lacks `CFBundleIconFile` and
an icon resource. The user's live server has four terminals and two agents;
further diagnostic launches use an isolated runtime instead of stopping it.

The helper now includes `Resources/draxul.icns` and declares it with
`CFBundleIconFile`. The plist and icon are link dependencies so incremental
builds rerun helper staging when either changes. A built-bundle inspection
confirmed the resource exists (235,208 bytes) and matches the declared name.

The live status item was recoverable without restarting server PID 35832.
Making menu-bar room temporarily brought it back; Command-dragging it to the
right, beside Spotlight, saved its placement and kept it visible after the
isolated helper and native AppKit probe were stopped. The displayed menu
reported the original server's four live terminals and two agents. Both
Draxul permissions and all temporarily changed application switches were
restored to enabled. macOS also logged cross-application blocking while
switches were being toggled, so the precise OS tracking/placement failure is
not established; do not claim the missing bundle icon caused the status item
to disappear. No SDL lifetime, startup-order, polling, or permission-bypass
workaround is retained in Draxul code.

**Final macOS validation (2026-09-29):** After repairing the saved menu-bar
position, started UI PID 70936, quit it through its native Quit action, waited
12 seconds with no client, reopened UI PID 71012, and waited another 12
seconds. The red D remained visible throughout. Server PID 35832 was unchanged,
with four live terminals and two agents preserved. The UI remains open. The
icon-fixed helper also launched successfully in an isolated runtime, and
`NSWorkspace.icon(forFile:)` returned the Draxul droplet icon after refreshing
that helper's Launch Services registration. All diagnostic processes were
stopped; all temporarily toggled permissions are enabled again.

`python3 do.py test release` built one aggregate target in approximately
58 seconds (configure 12.3 s, generate 1.5 s, 331 compiled objects and 48 link
steps); 24/25 CTest groups passed in 32.83 s. The failing Kanban group reproduced
unchanged in a 2.40 s rerun, passing 19/20 cases. That existing test/event-boundary
issue is transferred to
`kanban/done/03 kanban-focus-preview-refresh-regression -bug.md`; it does
not exercise bundle icon staging. `python3 do.py smoke release --skip-build`
passed its one startup scenario in 2.5 s from the same Release cache. No
renderer code changed, so render snapshots were not repeated. This slice is
macOS-specific; Windows code and product submodules were not changed.
