# Bug: Signed integer UB in bmp.cpp write and read paths

**Severity**: CRITICAL
**File**: `libs/draxul-types/src/bmp.cpp` — line 46 (write) and line 97 (read)
**Source**: review-bugs-consensus.md (C2)

## Description

Two distinct signed-integer UB issues in the BMP codec:

1. **Write path — line 46**: `frame.width * frame.height * 4` multiplies two `int` values before casting the result to `uint32_t`. For HiDPI frames the intermediate product overflows `int`, which is undefined behaviour in C++.

2. **Read path — line 97 (`read_u32`)**: `bytes[offset + 3] << 24` promotes a `uint8_t` to `int` then shifts it into bit 31. When the byte is `>= 0x80`, this is signed overflow UB. Because `write_bmp_rgba` stores a negative height (line 60) for top-down BMPs, *reading back any file written by this code* hits this path.

## Trigger Scenario

1. Write a BMP from a HiDPI render test (width × height × 4 > INT_MAX).
2. Read any BMP back with `read_bmp_rgba` — the negative height field reliably triggers the shift UB.

## Investigation Steps

- [x] Confirm `frame.width` and `frame.height` types in `CapturedFrame` (expected: `int`)
- [x] Confirm `read_u32` is also called from `read_u16` or other paths (check for similar issues there)

## Fix Strategy

- [x] Line 46: widen before multiply:
  ```cpp
  const auto image_size = static_cast<uint32_t>(
      static_cast<uint64_t>(frame.width) * frame.height * 4);
  ```
- [x] Line 97: cast each byte to `uint32_t` before shifting:
  ```cpp
  return static_cast<uint32_t>(bytes[offset])
       | (static_cast<uint32_t>(bytes[offset + 1]) << 8)
       | (static_cast<uint32_t>(bytes[offset + 2]) << 16)
       | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
  ```
- [x] Check `read_u16` at line 94 for the same pattern (safe: `<< 8` cannot overflow `uint16_t` range)

## Acceptance Criteria

- [x] UBSan reports no signed overflow or shift UB in `bmp.cpp` for any frame size
- [x] Round-trip write/read test passes for a 4096×4096 frame (large enough to overflow int multiply)
- [x] Existing render snapshot tests still pass after the change
