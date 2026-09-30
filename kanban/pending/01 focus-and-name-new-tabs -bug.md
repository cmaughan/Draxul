# Focus and name newly created tabs

Creating a tab should make it the active tab in the creating UI. A new
shared-session tab should begin with a useful name instead of `Tab`.

**Work**

- [x] Select a successfully created tab once its shared topology is projected, and route input to its host.
- [x] Use the launched host or plugin name as the initial tab name; preserve explicit rename and OSC 7 directory naming.
- [x] Cover local and shared-session creation, then run core aggregate and same-cache startup smoke.
- [x] Update the feature inventory with the resulting behavior.
- [ ] Confirm the palette-created shared-tab test on hosted Windows CI.

**Notes**

Local `TabController::add_tab` already activates the new tab and names it
from the created host. Shared-session commands currently send `Tab` as the
name, while GUI creation awaits asynchronous server acknowledgement. The
acknowledgement path must also update the input route after selecting the
newly projected tab.

**Implementation (2026-09-30):** New-tab mutations now carry the registered
host's display name or the selected plugin manifest's name to the server.
Local tabs still use their created host's name. Explicit user rename remains
sticky, and a shell's OSC 7 working-directory basename can replace a default
name afterward. The GUI waits for the shared tab acknowledgement before
changing focus; once the tab is projected, activation updates the keyboard
input route and requests a frame. This also avoids applying new-tab font
setup to the old tab while the server request is in flight.

**Validation:** A shared-session app smoke test opens the command palette,
selects `new_tab`, and uses an asynchronous control server to acknowledge the
command. It checks that the new tab becomes active and is named `Zsh` on macOS
(`PowerShell` on Windows), then verifies the next keypress reaches the new host
rather than the old one. It passed with 70 assertions.
The existing local new-tab keybinding test now checks selection and the
created host's default name. The focused palette test passed in 0.50 seconds.
After adding the palette path, the Release core aggregate passed 25/25 CTest
entries in 32.62 seconds, same-cache Release startup smoke passed in 0.56
seconds, and the basic render snapshot passed in 1.05 seconds. Hosted Windows
validation remains pending; no platform-specific code was changed.
