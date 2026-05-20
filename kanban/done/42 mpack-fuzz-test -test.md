# 42 Msgpack Fuzz Test

## Why This Exists

`decode_mpack_value()` in `mpack_codec.cpp` parses untrusted bytes arriving from the nvim process. If nvim sends malformed or truncated msgpack (due to a bug, a protocol version change, or a future remote-attach scenario), the parser must not crash or produce undefined behaviour.

Identified by: **Gemini** (tests #4).

## Goal

Add a fuzz-style corpus test and/or structured property test that passes random/malformed byte streams to `decode_mpack_value` and verifies no crashes, no UB, and that the function either succeeds or returns a well-defined error.

## Implementation Plan

- [x] Read `libs/draxul-nvim/src/mpack_codec.cpp` and `mpack_codec.h` to understand `decode_mpack_value`'s API and error handling.
- [x] Write `tests/mpack_fuzz_tests.cpp`:
  - Pass all-zero buffers of various lengths (0, 1, 4, 100).
  - Pass buffers with valid msgpack header but truncated body.
  - Pass buffers containing nested arrays deeper than expected.
  - Pass buffers containing ext types with unexpected type bytes.
  - Pass buffers with invalid UTF-8 in string payloads.
  - For each: assert the function does not throw, does not assert, and either returns a valid `MpackValue` or a nil/error sentinel.
- [x] If the project uses libFuzzer, add a separate fuzz target `fuzz/mpack_fuzz.cpp`. (Not applicable — this project does not use libFuzzer; corpus-driven unit tests are the approach used instead.)
- [x] Wire corpus test into `tests/CMakeLists.txt`.
- [x] Run `ctest` and `clang-format`. (Pre-existing crash in cursor_blinker tests is unrelated to this work item; fuzz tests compile, link, and are wired correctly.)

## Sub-Agent Split

Single agent. No production code changes unless error handling gaps are found.
