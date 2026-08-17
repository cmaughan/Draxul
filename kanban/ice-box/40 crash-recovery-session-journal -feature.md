# Crash-recovery session journal

**Type:** feature
**Priority:** 40
**Raised by:** GPT/Codex

## User need

The server writes durable Session checkpoints, but a server crash can lose
topology mutations since the last checkpoint. Record bounded authoritative
server mutations so restart can recover the last consistent Session revision.

## Implementation plan

- [ ] Land the remaining generic writer work in
      `kanban/ice-box/02 atomic-session-persistence -bug.md` and projection ownership in
      `kanban/ice-box/22 app-tab-session-controllers -refactor.md`.
- [ ] Define versioned journal records for server topology mutations and launch
  descriptors; exclude terminal contents, client-local focus, and secrets.
- [ ] Append length/checksum-framed records and flush on a bounded debounce rather than every input event.
- [ ] Periodically write an atomic full checkpoint and compact records already represented by it.
- [ ] During `ServerKernel` restore validate sequence/checksums, replay through
  pure authoritative topology operations, and stop at the last valid record.
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
