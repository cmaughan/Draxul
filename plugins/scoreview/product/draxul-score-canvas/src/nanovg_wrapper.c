// Thin wrapper that includes nanovg.c.
// NVG_NO_STB is defined in CMakeLists.txt to skip stb_image (we only use
// NanoVG for vector shapes, not image loading) and avoid duplicate symbols
// with draxul-megacity.
#include "nanovg.c"
