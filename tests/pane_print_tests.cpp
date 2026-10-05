// The print_pane action's composable pieces (libs/draxul-runtime-support):
// RGBA cropping, paper-white snapping, and the A4 PDF composer. The native
// print dialog is modal UI, deliberately untested (nothing to click in CI).

#include <catch2/catch_all.hpp>

#include <draxul/pane_print.h>

#include "support/scoped_env_var.h"
#include "support/temp_dir.h"

#include <fstream>
#include <string>
#include <vector>

using draxul::crop_rgba;
using draxul::CroppedImage;
using draxul::write_rgba_pdf_a4;

namespace
{

// A synthetic frame where every pixel encodes its own coordinates: R = x,
// G = y (mod 256) — crops are then verifiable per-byte.
std::vector<uint8_t> coordinate_frame(int width, int height)
{
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            uint8_t* px = rgba.data() + (static_cast<size_t>(y) * width + x) * 4u;
            px[0] = static_cast<uint8_t>(x & 0xFF);
            px[1] = static_cast<uint8_t>(y & 0xFF);
            px[2] = 0x30;
            px[3] = 0xFF;
        }
    }
    return rgba;
}

} // namespace

TEST_CASE("crop_rgba extracts the exact pane rectangle", "[pane-print]")
{
    const auto frame = coordinate_frame(200, 100);
    const CroppedImage pane = crop_rgba(frame, 200, 100, 50, 20, 60, 40);
    REQUIRE(pane.width == 60);
    REQUIRE(pane.height == 40);
    REQUIRE(pane.rgba.size() == 60u * 40u * 4u);
    // Corners encode their source coordinates.
    CHECK(pane.rgba[0] == 50); // top-left x
    CHECK(pane.rgba[1] == 20); // top-left y
    const size_t last = (static_cast<size_t>(39) * 60 + 59) * 4;
    CHECK(pane.rgba[last + 0] == 50 + 59);
    CHECK(pane.rgba[last + 1] == 20 + 39);
}

TEST_CASE("crop_rgba clamps to the frame and rejects degenerate rects", "[pane-print]")
{
    const auto frame = coordinate_frame(64, 64);
    const CroppedImage clamped = crop_rgba(frame, 64, 64, 48, 48, 100, 100);
    CHECK(clamped.width == 16);
    CHECK(clamped.height == 16);

    CHECK(crop_rgba(frame, 64, 64, 70, 0, 10, 10).rgba.empty());
    CHECK(crop_rgba(frame, 64, 64, 0, 0, 0, 10).rgba.empty());
    CHECK(crop_rgba({}, 0, 0, 0, 0, 10, 10).rgba.empty());
}

TEST_CASE("snap_paper_white whitens the sheet tint but not ink or edges", "[pane-print]")
{
    CroppedImage image;
    image.width = 4;
    image.height = 1;
    image.rgba = {
        252,
        251,
        248,
        255, // ScoreView's warm sheet tint -> pure white
        20,
        20,
        20,
        255, // ink -> untouched
        135,
        138,
        140,
        255, // antialiased glyph edge -> untouched
        239,
        245,
        250,
        255, // one channel below threshold -> untouched
    };
    draxul::snap_paper_white(image);
    CHECK(image.rgba[0] == 255);
    CHECK(image.rgba[1] == 255);
    CHECK(image.rgba[2] == 255);
    CHECK(image.rgba[4] == 20);
    CHECK(image.rgba[8] == 135);
    CHECK(image.rgba[12] == 239);
    CHECK(image.rgba[13] == 245);
}

namespace
{

// Points the OS temporary-directory lookup at `directory` for the scope:
// TMPDIR is consulted first on POSIX, TMP then TEMP by GetTempPathW.
struct ScopedTempDirectory
{
    explicit ScopedTempDirectory(const std::string& directory)
#ifdef _WIN32
        : tmp("TMP", directory.c_str())
        , temp("TEMP", directory.c_str())
#else
        : tmpdir("TMPDIR", directory.c_str())
#endif
    {
    }

#ifdef _WIN32
    draxul::tests::ScopedEnvVar tmp;
    draxul::tests::ScopedEnvVar temp;
#else
    draxul::tests::ScopedEnvVar tmpdir;
#endif
};

} // namespace

TEST_CASE("pane_print_temp_pdf_path reports unavailable temporary storage without throwing",
    "[pane-print]")
{
    draxul::tests::TempDir dir("pane-print-temp");

    SECTION("usable temporary storage yields a fresh PDF path inside it")
    {
        const ScopedTempDirectory override(dir.path.string());
        std::string error;
        const auto path = draxul::pane_print_temp_pdf_path(error);
        REQUIRE(path.has_value());
        CHECK(error.empty());
        CHECK(std::filesystem::equivalent(path->parent_path(), dir.path));
        CHECK(path->extension() == ".pdf");
        CHECK(path->filename().string().rfind("draxul-pane-", 0) == 0);
        CHECK_FALSE(std::filesystem::exists(*path));
    }

    SECTION("a missing temporary directory is a print error")
    {
        const ScopedTempDirectory override((dir.path / "missing").string());
        std::string error;
        std::optional<std::filesystem::path> path;
        REQUIRE_NOTHROW(path = draxul::pane_print_temp_pdf_path(error));
        CHECK_FALSE(path.has_value());
        CHECK(error.find("temporary storage is unavailable") != std::string::npos);
    }

    SECTION("a temporary location that is not a directory is a print error")
    {
        const auto file = dir.path / "not-a-directory";
        std::ofstream(file) << "x";
        const ScopedTempDirectory override(file.string());
        std::string error;
        std::optional<std::filesystem::path> path;
        REQUIRE_NOTHROW(path = draxul::pane_print_temp_pdf_path(error));
        CHECK_FALSE(path.has_value());
        CHECK_FALSE(error.empty());
    }
}

TEST_CASE("pane_print_path_text keeps international path characters as UTF-8", "[pane-print]")
{
    // Cyrillic directory, CJK + emoji file name (outside any single ANSI
    // code page, so path::string() would throw on Windows).
    const std::filesystem::path path = std::filesystem::path(u8"печать")
        / u8"打印-\U0001F5A8.pdf";
    const auto expected = path.u8string();
    CHECK(draxul::pane_print_path_text(path)
        == std::string(reinterpret_cast<const char*>(expected.data()), expected.size()));
}

#ifdef __APPLE__

TEST_CASE("write_rgba_pdf_a4 produces a one-page A4 PDF, auto-oriented", "[pane-print]")
{
    draxul::tests::TempDir dir("pane-print");

    // Tall pane -> portrait A4 (595 x 842 pt); wide pane -> landscape.
    struct Case
    {
        int width;
        int height;
        const char* media_box;
        const char* name;
    };
    const Case cases[] = {
        { 300, 500, "MediaBox [0 0 595.276 841.89]", "portrait.pdf" },
        { 500, 300, "MediaBox [0 0 841.89 595.276]", "landscape.pdf" },
    };
    for (const Case& c : cases)
    {
        const auto frame = coordinate_frame(c.width, c.height);
        const auto path = dir.path / c.name;
        std::string error;
        REQUIRE(write_rgba_pdf_a4(frame.data(), c.width, c.height, path, error));
        const std::string bytes = draxul::tests::read_file(path);
        REQUIRE(bytes.size() > 1000); // image stream present, not a stub
        CHECK(bytes.rfind("%PDF-", 0) == 0);
        INFO(c.name);
        const size_t mb = bytes.find("MediaBox");
        if (mb != std::string::npos)
            UNSCOPED_INFO("context: " << bytes.substr(mb, 48));
        CHECK(bytes.find(c.media_box) != std::string::npos);
        CHECK(bytes.find("/Count 1") != std::string::npos); // exactly one page
    }

    std::string error;
    CHECK_FALSE(write_rgba_pdf_a4(nullptr, 10, 10, dir.path / "bad.pdf", error));
    CHECK_FALSE(error.empty());
}

#endif
