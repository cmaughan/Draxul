// Shared decoration geometry constants for GLSL and Metal shaders.
// local_uv.y = 0 at top of cell, 1 at bottom.
// This is the single authoritative source — edit here only.
// #define macros are valid in both GLSL 4.50 and Metal Shading Language.

#define UNDERLINE_TOP 0.86 // underline band top: bottom 14% of cell height
#define UNDERLINE_BOTTOM 0.93 // underline band bottom: bottom 7% of cell height
#define STRIKETHROUGH_TOP 0.48 // strikethrough band top: middle of cell
#define STRIKETHROUGH_BOTTOM 0.54 // strikethrough band bottom: ~6% band at midline
#define UNDERCURL_BASELINE 0.84 // undercurl center-line y offset (near cell bottom)
#define UNDERCURL_AMPLITUDE 0.05 // undercurl sine wave amplitude (fraction of cell)
#define UNDERCURL_FREQ 18.8495559 // 6*PI: two full sine periods per cell width
#define UNDERCURL_THICKNESS 0.03 // undercurl half-thickness (fraction of cell)

// Style flag bit constants — must match STYLE_FLAG_* in draxul/types.h
#define STYLE_FLAG_BOLD 1u // 1u << 0
#define STYLE_FLAG_ITALIC 2u // 1u << 1
#define STYLE_FLAG_UNDERLINE 4u // 1u << 2
#define STYLE_FLAG_STRIKETHROUGH 8u // 1u << 3
#define STYLE_FLAG_UNDERCURL 16u // 1u << 4
#define STYLE_FLAG_COLOR_GLYPH 32u // 1u << 5
#define STYLE_FLAG_HYPERLINK 64u // 1u << 6
