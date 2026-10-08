#pragma once

#include <nanovg.h>
#include <filesystem>
#include <string>

namespace draxul::personal_detail
{
inline int chat_font(NVGcontext* vg,const std::string& path)
{
    // Fontstash truncates names to 63 bytes. Paths exceed that limit in normal
    // app bundles, so using the path as the lookup key reloads it every frame.
    // The host replaces its NanoVG context when the primary font path changes.
    constexpr auto name="personal-body";
    int font=nvgFindFont(vg,name);
    if(font>=0) return font;
    font=nvgCreateFont(vg,name,path.c_str());
    if(font<0) return font;
#ifdef __APPLE__
    const char* fallback="/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc";
#elif defined(_WIN32)
    const char* fallback="C:/Windows/Fonts/meiryo.ttc";
#else
    const char* fallback="/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc";
#endif
    if(std::filesystem::exists(fallback))
    {
        const int other=nvgCreateFont(vg,"personal-cjk",fallback);
        if(other>=0) nvgAddFallbackFontId(vg,font,other);
    }
    return font;
}
}
