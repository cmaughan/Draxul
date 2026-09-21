# Terminal process ownership

`UnixPtyProcess` and `ConPtyProcess` own process creation, terminal I/O,
resize, native process/job resources, and exit status. The private
`AgentProcessObserver` owns lazy observation-thread startup, output-activity
generations, debounce/reconciliation scheduling, cached publication, and
stop/join/reset. `AgentObservationSchedule` is a timestamp-only state machine so
its edge cases do not require a live shell or wall-clock sleeps.

The observer borrows every native resource passed through its callbacks. On
Unix, its probe borrows the PTY master file descriptor and reads the current
foreground process group; it never closes the descriptor. On Windows, its probe
borrows the process ID and Job Object, while its native wait borrows the I/O
completion port. The Job Object associates completion packets with the owning
`ConPtyProcess` address as the completion key; the wait callback ignores packets
with any other key. A zero-key packet exists only to interrupt stop.

Shutdown ordering is part of the contract: stop and join the observer first,
then stop and join the terminal reader, then close or transfer the borrowed PTY,
process, Job Object, and completion-port resources. Reader activity may still be
recorded after observer stop while the reader is joining, but no probe runs and
the observer object remains alive until that reader has stopped. Spawn creates a
fresh observer, and `stop()` clears its cached publication before restart or
destruction.

The deterministic private-boundary cases use the Catch tag
`[agent_process_observer]`; real macOS/Linux discovery remains under
`[unix_pty_process][agent][discovery]`, with the corresponding ConPTY coverage
compiled on Windows.
