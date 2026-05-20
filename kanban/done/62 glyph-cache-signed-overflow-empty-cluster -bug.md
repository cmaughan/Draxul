# Bug: Signed integer overflow in rasterize_cluster for zero-ink glyph clusters

**Severity**: MEDIUM
**File**: `libs/draxul-font/src/glyph_cache.cpp:297`
**Source**: review-bugs-consensus.md (M3)

## Description

`rasterize_cluster` initialises:
```cpp
int bbox_top    = INT_MIN;
int bbox_bottom = INT_MAX;
```
These are updated in the glyph loop only when a glyph has a non-zero bitmap. Lines 285–294 protect `bbox_right` and `bbox_left` from staying at their sentinel values by applying unconditional post-loop clamps. **No equivalent protection exists for `bbox_top`/`bbox_bottom`.**

For a shaped cluster where every glyph has a zero-size bitmap (e.g., a space character in some fonts, combining characters), the glyph loop runs but never updates `bbox_top` or `bbox_bottom`. Line 297 then computes:
```cpp
int cluster_height = bbox_top - bbox_bottom;  // INT_MIN - INT_MAX → signed overflow UB
```
The resulting `cluster_height` is garbage and is passed to `reserve_region`, potentially requesting a massive atlas allocation or causing downstream UB.

## Trigger Scenario

Render any cluster where all shaped glyphs have zero bitmap dimensions — space characters in certain fonts, some combining or control characters.

## Fix Strategy

- [x] After the glyph loop and before line 297, add:
  ```cpp
  if (bbox_top == INT_MIN)
  {
      // No ink glyphs — produce a zero-size region
      bbox_top = 0;
      bbox_bottom = 0;
  }
  ```
- [x] Verify `cluster_height == 0` is handled correctly by `reserve_region` and the composite buffer allocation (line 305)

## Acceptance Criteria

- [x] Rendering a space character through `rasterize_cluster` does not trigger UBSan signed-overflow
- [x] Zero-height clusters return a valid (empty) `AtlasRegion` without crashing
- [x] Normal glyph clusters with ink are unaffected
