# 35 MpackValue Ext Dead Code

## Why This Exists

`MpackValue::Ext` appears in the public `Type` enum in `mpack_codec.h`, but the `Storage` variant never includes an `Ext` type — ext values are decoded by reusing `int64_t` storage. This means `value.type() == MpackValue::Ext` is always false at runtime. Any future code that branches on `Ext` will silently misbehave.

Identified by: **Claude**. Partially implied by: **GPT** (weakly-typed RPC layer).

## Goal

Remove the dead `Ext` enum entry or, preferably, make it actually work: add an `Ext` variant to `Storage` holding the ext type byte and raw bytes so callers can distinguish neovim `Buffer`/`Window`/`Tabpage` handles from plain integers.

## Implementation Plan

- [x] Read `libs/draxul-nvim/src/mpack_codec.h` and `mpack_codec.cpp` to understand current `MpackValue` layout.
- [x] Decide approach: (a) add `struct ExtValue { int8_t type; int64_t data; }` to `Storage` and set `type()` correctly, or (b) remove the `Ext` enum entry and document that ext types decode as `Int`.
- [x] Preferred approach (a): add `ExtValue` to `Storage`, update the mpack ext decoder to set `storage = ExtValue{...}` and `type_ = Ext`.
- [x] Update any callers that use `as_int()` on neovim handle types — no existing callers use `as_int()` on ext-typed values; no shim needed.
- [x] Add a unit test in `tests/rpc_codec_tests.cpp` (or equivalent) asserting that an ext-encoded value returns `type() == MpackValue::Ext`.
- [x] Run `ctest` and `clang-format` on touched files.

## Sub-Agent Split

Single agent. The change is confined to `mpack_codec.h`, `mpack_codec.cpp`, and possibly `ui_events.cpp` for callers.
