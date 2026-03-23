
#include <catch2/catch_all.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <draxul/text_service.h>
#include <filesystem>

using namespace draxul;

namespace
{

std::filesystem::path repo_root()
{
    auto here = std::filesystem::path(__FILE__).parent_path();
    return here.parent_path();
}

std::filesystem::path ligature_font_path()
{
    return repo_root() / "fonts" / "CascadiaCode-Regular.ttf";
}

std::filesystem::path color_emoji_font_path()
{
#ifdef _WIN32
    const char* windir = std::getenv("WINDIR");
    auto windows_dir = std::filesystem::path(windir ? windir : "C:\\Windows");
    return windows_dir / "Fonts" / "seguiemj.ttf";
#elif defined(__APPLE__)
    return "/System/Library/Fonts/Apple Color Emoji.ttc";
#else
    auto noto_color = std::filesystem::path("/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf");
    if (std::filesystem::exists(noto_color))
        return noto_color;
    return "/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf";
#endif
}

std::filesystem::path cjk_font_path()
{
#ifdef _WIN32
    const char* windir = std::getenv("WINDIR");
    auto windows_dir = std::filesystem::path(windir ? windir : "C:\\Windows") / "Fonts";
    const std::filesystem::path candidates[] = {
        windows_dir / "YuGothR.ttc",
        windows_dir / "YuGothM.ttc",
        windows_dir / "meiryo.ttc",
        windows_dir / "msgothic.ttc",
        windows_dir / "msyh.ttc",
        windows_dir / "simsun.ttc",
    };
#elif defined(__APPLE__)
    const std::filesystem::path candidates[] = {
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
    };
#else
    const std::filesystem::path candidates[] = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    };
#endif

    for (const auto& candidate : candidates)
    {
        if (std::filesystem::exists(candidate))
            return candidate;
    }
    return {};
}

} // namespace

TEST_CASE("bundled nerd font shapes and rasterizes current lazy icon", "[font]")
{
    auto font_path = repo_root() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
    INFO("bundled font exists");
    REQUIRE(std::filesystem::exists(font_path));

    TextService service;
    TextServiceConfig config;
    config.font_path = font_path.string();
    INFO("text service initializes");
    REQUIRE(service.initialize(config, 11, 96.0f));
    const std::string lazy_icon = "\xF3\xB0\x92\xB2"; // U+F04B2
    const auto region = service.resolve_cluster(lazy_icon);
    INFO("lazy icon rasterizes");
    REQUIRE(region.size.x > 0);
    INFO("lazy icon has height");
    REQUIRE(region.size.y > 0);
    INFO("configured font path is used");
    REQUIRE(service.primary_font_path() == font_path.string());
    service.shutdown();
}

TEST_CASE("glyph cache dirty rect accumulates newly rasterized glyphs", "[font]")
{
    auto font_path = repo_root() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
    INFO("bundled font exists");
    REQUIRE(std::filesystem::exists(font_path));

    TextService service;
    TextServiceConfig config;
    config.font_path = font_path.string();
    INFO("text service initializes");
    REQUIRE(service.initialize(config, 11, 96.0f));

    const auto region_l = service.resolve_cluster("L");
    const auto region_a = service.resolve_cluster("a");
    const auto dirty = service.atlas_dirty_rect();

    int atlas_width = service.atlas_width();
    int l_x = static_cast<int>(region_l.uv.x * atlas_width);
    int a_x = static_cast<int>(region_a.uv.x * atlas_width);
    int left = std::min(l_x, a_x);
    int right = std::max(l_x + region_l.size.x, a_x + region_a.size.x);

    INFO("dirty rect starts at the leftmost new glyph");
    REQUIRE(dirty.pos.x == left);
    INFO("dirty rect spans all newly rasterized glyphs");
    REQUIRE(dirty.size.x == right - left);

    service.clear_atlas_dirty();
    INFO("clearing dirtiness resets the dirty flag");
    REQUIRE(!service.atlas_dirty());
    INFO("clearing dirtiness resets the dirty rect");
    REQUIRE(service.atlas_dirty_rect().size.x == 0);
    service.shutdown();
}

TEST_CASE("font choice cache stays bounded under many unique clusters", "[font]")
{
    auto font_path = repo_root() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
    INFO("bundled font exists");
    REQUIRE(std::filesystem::exists(font_path));

    TextService service;
    TextServiceConfig config;
    config.font_path = font_path.string();
    config.font_choice_cache_limit = 8;
    INFO("text service initializes");
    REQUIRE(service.initialize(config, 11, 96.0f));

    for (int i = 0; i < 32; ++i)
    {
        service.resolve_cluster("cache-entry-" + std::to_string(i));
        INFO("font choice cache should never exceed its configured limit");
        REQUIRE(service.font_choice_cache_size() <= config.font_choice_cache_limit);
    }

    service.shutdown();
}

TEST_CASE("emoji fallback preserves color glyph pixels in the atlas", "[font]")
{
    auto primary_font_path = repo_root() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
    auto emoji_font_path = color_emoji_font_path();
    INFO("bundled font exists");
    REQUIRE(std::filesystem::exists(primary_font_path));
    if (!std::filesystem::exists(emoji_font_path))
        SKIP("color emoji font not available on this machine");

    TextService service;
    TextServiceConfig config;
    config.font_path = primary_font_path.string();
#ifdef _WIN32
    const char* windir = std::getenv("WINDIR");
    auto windows_dir = std::filesystem::path(windir ? windir : "C:\\Windows");
    auto symbol_font_path = windows_dir / "Fonts" / "seguisym.ttf";
    if (std::filesystem::exists(symbol_font_path))
        config.fallback_paths.push_back(symbol_font_path.string());
#endif
    config.fallback_paths.push_back(emoji_font_path.string());
    INFO("text service initializes");
    REQUIRE(service.initialize(config, 11, 96.0f));

    const std::string sleep_emoji = "\xF0\x9F\x92\xA4"; // U+1F4A4
    const auto region = service.resolve_cluster(sleep_emoji);
    INFO("emoji rasterizes");
    REQUIRE(region.size.x > 0);
    INFO("emoji has height");
    REQUIRE(region.size.y > 0);
    INFO("emoji region is flagged as color");
    REQUIRE(region.is_color);

    const auto* atlas = service.atlas_data();
    const int atlas_width = service.atlas_width();
    const int atlas_x = static_cast<int>(region.uv.x * atlas_width + 0.5f);
    const int atlas_y = static_cast<int>(region.uv.y * atlas_width + 0.5f);

    bool found_colored_pixel = false;
    for (int row = 0; row < region.size.y && !found_colored_pixel; row++)
    {
        for (int col = 0; col < region.size.x; col++)
        {
            const size_t pixel_index = (((size_t)(atlas_y + row) * atlas_width) + atlas_x + col) * 4;
            const uint8_t r = atlas[pixel_index + 0];
            const uint8_t g = atlas[pixel_index + 1];
            const uint8_t b = atlas[pixel_index + 2];
            const uint8_t a = atlas[pixel_index + 3];
            if (a > 0 && !(r == 255 && g == 255 && b == 255))
            {
                found_colored_pixel = true;
                break;
            }
        }
    }

    INFO("emoji atlas region contains non-monochrome pixels");
    REQUIRE(found_colored_pixel);
    service.shutdown();
}

TEST_CASE("wide japanese text resolves through CJK fallback fonts", "[font]")
{
    auto primary_font_path = repo_root() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
    auto cjk_path = cjk_font_path();
    INFO("bundled font exists");
    REQUIRE(std::filesystem::exists(primary_font_path));
    if (cjk_path.empty())
        SKIP("cjk fallback font not available on this machine");

    TextService service;
    TextServiceConfig config;
    config.font_path = primary_font_path.string();
    config.fallback_paths = { cjk_path.string() };
    INFO("text service initializes");
    REQUIRE(service.initialize(config, 11, 96.0f));

    constexpr char japanese_bytes[] = "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF";
    const std::string japanese = japanese_bytes;
    const auto region = service.resolve_cluster(japanese);
    INFO("japanese text rasterizes");
    REQUIRE(region.size.x > 0);
    INFO("japanese text has height");
    REQUIRE(region.size.y > 0);
    service.shutdown();
}

TEST_CASE("ligature shaping can be disabled from text service config", "[font]")
{
    auto font_path = ligature_font_path();
    INFO("ligature font exists");
    REQUIRE(std::filesystem::exists(font_path));

    TextService ligatures_enabled;
    TextServiceConfig enabled_config;
    enabled_config.font_path = font_path.string();
    enabled_config.enable_ligatures = true;
    INFO("text service initializes with ligatures enabled");
    REQUIRE(ligatures_enabled.initialize(enabled_config, 11, 96.0f));

    const std::string ligature = "->";
    INFO("enabled ligatures report the expected two-cell span");
    REQUIRE(ligatures_enabled.ligature_cell_span(ligature) == 2);
    const auto ligature_region = ligatures_enabled.resolve_cluster(ligature);
    INFO("expected ligature rasterizes");
    REQUIRE(ligature_region.size.x > 0);

    TextService ligatures_disabled;
    TextServiceConfig disabled_config;
    disabled_config.font_path = font_path.string();
    disabled_config.enable_ligatures = false;
    INFO("text service initializes with ligatures disabled");
    REQUIRE(ligatures_disabled.initialize(disabled_config, 11, 96.0f));
    INFO("disabled ligatures suppress the same programming ligature");
    REQUIRE(ligatures_disabled.ligature_cell_span(ligature) == 0);

    ligatures_disabled.shutdown();
    ligatures_enabled.shutdown();
}
