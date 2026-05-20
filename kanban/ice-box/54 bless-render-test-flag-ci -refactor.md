# 54 Bless Render Test Flag — CI Only

## Why This Exists

The `--bless-render-test` CLI flag permanently overwrites reference BMP images. It is currently exposed in the user-facing production binary. An end-user who discovers this flag could accidentally destroy the CI reference images. It should be a CI/developer-only capability.

Identified by: **Claude** (worst features #1).

## Goal

Move `--bless-render-test` behind a compile-time feature flag (`DRAXUL_ENABLE_BLESS`) that is only enabled in CI / developer builds. In production release builds, the flag should be silently ignored or produce an error message.

## Implementation Plan

- [ ] Read `app/main.cpp` for `--bless-render-test` argument parsing.
- [ ] Add a CMake option `DRAXUL_ENABLE_RENDER_BLESS` (default OFF for production, ON for CI).
- [ ] Wrap the `--bless-render-test` branch in `#ifdef DRAXUL_ENABLE_RENDER_BLESS`.
- [ ] In CMake CI preset (`cmake/Presets.json` or equivalent), add `-DDRAXUL_ENABLE_RENDER_BLESS=ON`.
- [ ] In release presets, ensure it is OFF.
- [ ] Update `CLAUDE.md` to note the flag is compile-time gated.
- [ ] Run `ctest` and `clang-format`.

## Sub-Agent Split

Single agent. Small change to `main.cpp` and CMake.
