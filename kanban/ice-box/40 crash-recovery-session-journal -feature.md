# Crash-recovery session journal

**Type:** feature
**Priority:** 40
**Raised by:** GPT/Codex

## User need

Clean shutdown saves shell topology, but a crash loses changes since the last checkpoint. Record bounded topology mutations so startup can recover the last consistent tab.

## Implementation plan

- [ ] Land atomic persistence item 02 and SessionController item 22.
- [ ] Define versioned journal records for tab/pane create/close/move/rename/focus and host launch descriptors; exclude terminal contents and secrets.
- [ ] Append length/checksum-framed records and flush on a bounded debounce rather than every input event.
- [ ] Periodically write an atomic full checkpoint and compact records already represented by it.
- [ ] On startup validate sequence/checksums, replay through pure session-state operations, and stop at the last valid record.
- [ ] Present a recovery choice after abnormal termination; never overwrite a valid clean checkpoint silently.
- [ ] Cap file size/record count and quarantine corrupt journals with useful diagnostics.

## Tests and acceptance

- [ ] Fault-inject torn headers/payloads, checksum errors, duplicate/out-of-order records, crash during compaction, and disk-full errors.
- [ ] Replay randomized valid topology mutations and compare with the live final state.
- [ ] Recovery restores the last consistent shell topology and never creates fake non-restorable product hosts.
- [ ] Normal clean startup/shutdown behavior remains unchanged.

## Dependencies and parallelism

Depends on items 02 and 22. A persistence sub-agent can implement the journal engine after the controller owns mutations.

<model>GPT-5 Codex</model>
