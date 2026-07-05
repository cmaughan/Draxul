# Share binary catalog container framing

**Type:** refactor
**Priority:** 32
**Raised by:** Claude

## Goal

The `DXSTAR1`, `DXCLINE1`, and `DXCBND01` assets independently implement magic/version/count/record-size/bounds validation and Python writing. Share container framing while preserving semantic record formats and provenance.

## Implementation plan

- [ ] Wait for active boundary-catalog work and record each current on-disk header/record byte layout with golden fixtures.
- [ ] Define a small generic header reader/writer for magic, version, header size, record size/count, endian marker if needed, payload length, and optional checksum.
- [ ] Keep catalog-specific semantic validation in each loader.
- [ ] Add a matching Python helper used by all generator scripts.
- [ ] Support existing versions without rewriting shipped assets in the first change.
- [ ] If a new container version is justified, add deterministic migration/regeneration and attribution-preserving output.
- [ ] Document compatibility and maximum-size limits.

## Tests and acceptance

- [ ] Golden-load every existing asset and compare record counts/selected values.
- [ ] Cover bad magic/version/sizes/count overflow/truncation/non-finite semantic data.
- [ ] Run generators twice and require byte-identical output.
- [ ] Existing runtime assets remain loadable on Windows and macOS.

## Dependencies and parallelism

Follows current catalog work. A data-format sub-agent can own C++/Python helpers while catalog owners review compatibility.

<model>GPT-5 Codex</model>
