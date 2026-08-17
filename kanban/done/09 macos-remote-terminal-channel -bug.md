# Fix the macOS remote-terminal channel and server suite failures

**Type:** bug
**Completed:** 2026-08-03

## Resolution

BSD/macOS accepted sockets inherited `O_NONBLOCK` from the listener. The server
therefore treated an ordinary early `EAGAIN` as an invalid control frame, which
made the long-lived remote-terminal channel flap while small RPCs often won the
race.

Commit `59e1e13b` clears `O_NONBLOCK` on accepted descriptors, retries interrupted
frame I/O, normalizes executable evidence, and double-forks detached launches so
dead servers are reaped. The macOS server/remote-terminal suites and a live GUI
run passed; later Windows server and acceptance work preserved parity.

Toast coalescing was deliberately not part of this bug: once the channel stopped
flapping, the repeated recovery toast disappeared.
