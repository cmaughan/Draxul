// Golden fixtures for the SatView binary catalog container formats.
//
// These tests pin the exact on-disk byte layout of the three catalog
// containers (DXSTAR1, DXCLINE1, DXCBND01). The fixture byte arrays are
// handcrafted little-endian bytes — NOT produced by the production structs —
// so any accidental change to the header or record layout fails loudly here.

#include <draxul/satview/satview_catalog_container.h>
#include <draxul/satview/satview_constellation_boundary_catalog.h>
#include <draxul/satview/satview_constellation_catalog.h>
#include <draxul/satview/satview_star_catalog.h>
#include <draxul/satview/satview_texture_assets.h>

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace draxul::satview;
using Catch::Approx;

namespace
{

std::filesystem::path write_temp_catalog(const std::string& name,
    const std::vector<unsigned char>& bytes)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return path;
}

std::vector<unsigned char> read_file_prefix(const std::filesystem::path& path, std::size_t count)
{
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    std::vector<unsigned char> bytes(count, 0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count));
    REQUIRE(file.gcount() == static_cast<std::streamsize>(count));
    return bytes;
}

std::uintmax_t require_file_size(const std::filesystem::path& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    REQUIRE_FALSE(error);
    return size;
}

// clang-format off

// DXSTAR1 v1: 32-byte header, 32-byte records (vec4 direction+magnitude,
// vec4 premultiplied color + size), little-endian, no padding.
const std::vector<unsigned char> kGoldenStarCatalog = {
    // header
    0x44, 0x58, 0x53, 0x54, 0x41, 0x52, 0x31, 0x00, // magic "DXSTAR1\0"
    0x01, 0x00, 0x00, 0x00,                         // version 1
    0x20, 0x00, 0x00, 0x00,                         // header_size 32
    0x20, 0x00, 0x00, 0x00,                         // record_size 32
    0x02, 0x00, 0x00, 0x00,                         // record_count 2
    0x00, 0x00, 0x00, 0x00,                         // flags 0
    0x01, 0x00, 0x00, 0x00,                         // source_id 1 (Hipparcos)
    // record 0: direction (1, 0, 0), magnitude -1.5, color (0.5, 0.25, 1), size 0.125
    0x00, 0x00, 0x80, 0x3f,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0xc0, 0xbf,
    0x00, 0x00, 0x00, 0x3f,  0x00, 0x00, 0x80, 0x3e,  0x00, 0x00, 0x80, 0x3f,  0x00, 0x00, 0x00, 0x3e,
    // record 1: direction (0, 1, 0), magnitude 2.5, color (1, 1, 1), size 0.25
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x80, 0x3f,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x20, 0x40,
    0x00, 0x00, 0x80, 0x3f,  0x00, 0x00, 0x80, 0x3f,  0x00, 0x00, 0x80, 0x3f,  0x00, 0x00, 0x80, 0x3e,
};

// DXCLINE1 v1: 32-byte header, 24-byte records (vec3 start, vec3 end).
const std::vector<unsigned char> kGoldenLineCatalog = {
    // header
    0x44, 0x58, 0x43, 0x4c, 0x49, 0x4e, 0x45, 0x31, // magic "DXCLINE1"
    0x01, 0x00, 0x00, 0x00,                         // version 1
    0x20, 0x00, 0x00, 0x00,                         // header_size 32
    0x18, 0x00, 0x00, 0x00,                         // record_size 24
    0x02, 0x00, 0x00, 0x00,                         // record_count 2
    0x00, 0x00, 0x00, 0x00,                         // flags 0
    0x01, 0x00, 0x00, 0x00,                         // source_id 1
    // record 0: start (1, 0, 0), end (0, 1, 0)
    0x00, 0x00, 0x80, 0x3f,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x80, 0x3f,  0x00, 0x00, 0x00, 0x00,
    // record 1: start (0, 0, 1), end (0, -1, 0)
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x80, 0x3f,
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x80, 0xbf,  0x00, 0x00, 0x00, 0x00,
};

// DXCBND01 v1: 56-byte header, 32-byte segment records, 32-byte label
// records, then a UTF-8 string blob referenced by (offset, size) pairs.
const std::vector<unsigned char> kGoldenBoundaryCatalog = {
    // header
    0x44, 0x58, 0x43, 0x42, 0x4e, 0x44, 0x30, 0x31, // magic "DXCBND01"
    0x01, 0x00, 0x00, 0x00,                         // version 1
    0x38, 0x00, 0x00, 0x00,                         // header_size 56
    0x20, 0x00, 0x00, 0x00,                         // segment_record_size 32
    0x20, 0x00, 0x00, 0x00,                         // label_record_size 32
    0x02, 0x00, 0x00, 0x00,                         // segment_count 2
    0x01, 0x00, 0x00, 0x00,                         // label_count 1
    0x38, 0x00, 0x00, 0x00,                         // segment_offset 56
    0x78, 0x00, 0x00, 0x00,                         // label_offset 120
    0x98, 0x00, 0x00, 0x00,                         // string_offset 152
    0x09, 0x00, 0x00, 0x00,                         // string_size 9
    0x01, 0x00, 0x00, 0x00,                         // source_id 1
    0x00, 0x00, 0x00, 0x00,                         // reserved 0
    // segment 0: start (1, 0, 0), end (0, 1, 0), source_feature 0, flags 0
    0x00, 0x00, 0x80, 0x3f,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x80, 0x3f,  0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
    // segment 1: start (0, 0, 1), end (1, 0, 0), source_feature 7, flags 0
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x80, 0x3f,
    0x00, 0x00, 0x80, 0x3f,  0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
    // label 0: direction (0, 1, 0), rank 1, name_offset 0, name_size 9,
    //          designation "And\0", area_index 0
    0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x80, 0x3f,  0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,  0x09, 0x00, 0x00, 0x00,
    0x41, 0x6e, 0x64, 0x00,  0x00, 0x00, 0x00, 0x00,
    // strings: "Andromeda"
    0x41, 0x6e, 0x64, 0x72, 0x6f, 0x6d, 0x65, 0x64, 0x61,
};

// Exact header bytes of the shipped assets. If a regenerated asset or a
// writer change alters the container framing, these comparisons fail first.
const std::vector<unsigned char> kShippedStarHeader = {
    0x44, 0x58, 0x53, 0x54, 0x41, 0x52, 0x31, 0x00, // magic "DXSTAR1\0"
    0x01, 0x00, 0x00, 0x00,                         // version 1
    0x20, 0x00, 0x00, 0x00,                         // header_size 32
    0x20, 0x00, 0x00, 0x00,                         // record_size 32
    0xa0, 0x86, 0x01, 0x00,                         // record_count 100000
    0x00, 0x00, 0x00, 0x00,                         // flags 0
    0x01, 0x00, 0x00, 0x00,                         // source_id 1
};

const std::vector<unsigned char> kShippedLineHeader = {
    0x44, 0x58, 0x43, 0x4c, 0x49, 0x4e, 0x45, 0x31, // magic "DXCLINE1"
    0x01, 0x00, 0x00, 0x00,                         // version 1
    0x20, 0x00, 0x00, 0x00,                         // header_size 32
    0x18, 0x00, 0x00, 0x00,                         // record_size 24
    0x90, 0x02, 0x00, 0x00,                         // record_count 656
    0x00, 0x00, 0x00, 0x00,                         // flags 0
    0x01, 0x00, 0x00, 0x00,                         // source_id 1
};

const std::vector<unsigned char> kShippedBoundaryHeader = {
    0x44, 0x58, 0x43, 0x42, 0x4e, 0x44, 0x30, 0x31, // magic "DXCBND01"
    0x01, 0x00, 0x00, 0x00,                         // version 1
    0x38, 0x00, 0x00, 0x00,                         // header_size 56
    0x20, 0x00, 0x00, 0x00,                         // segment_record_size 32
    0x20, 0x00, 0x00, 0x00,                         // label_record_size 32
    0x48, 0x13, 0x00, 0x00,                         // segment_count 4936
    0x59, 0x00, 0x00, 0x00,                         // label_count 89
    0x38, 0x00, 0x00, 0x00,                         // segment_offset 56
    0x38, 0x69, 0x02, 0x00,                         // label_offset 158008
    0x58, 0x74, 0x02, 0x00,                         // string_offset 160856
    0xa9, 0x02, 0x00, 0x00,                         // string_size 681
    0x01, 0x00, 0x00, 0x00,                         // source_id 1
    0x00, 0x00, 0x00, 0x00,                         // reserved 0
};

// clang-format on

} // namespace

TEST_CASE("SatView star catalog golden byte layout loads exactly", "[satview][catalog]")
{
    const auto path = write_temp_catalog("draxul-satview-golden-stars.dxstar", kGoldenStarCatalog);
    const std::vector<SatViewStarInstance> stars = load_satview_star_catalog(16, path);
    std::error_code error;
    std::filesystem::remove(path, error);

    REQUIRE(stars.size() == 2u);
    CHECK(stars[0].direction_magnitude.x == Approx(1.0f).margin(1e-9));
    CHECK(stars[0].direction_magnitude.y == Approx(0.0f).margin(1e-9));
    CHECK(stars[0].direction_magnitude.z == Approx(0.0f).margin(1e-9));
    CHECK(stars[0].direction_magnitude.w == Approx(-1.5f).margin(1e-9));
    CHECK(stars[0].color_size.x == Approx(0.5f).margin(1e-9));
    CHECK(stars[0].color_size.y == Approx(0.25f).margin(1e-9));
    CHECK(stars[0].color_size.z == Approx(1.0f).margin(1e-9));
    CHECK(stars[0].color_size.w == Approx(0.125f).margin(1e-9));
    CHECK(stars[1].direction_magnitude.y == Approx(1.0f).margin(1e-9));
    CHECK(stars[1].direction_magnitude.w == Approx(2.5f).margin(1e-9));
    CHECK(stars[1].color_size.w == Approx(0.25f).margin(1e-9));
}

TEST_CASE("SatView star catalog honors the maximum count", "[satview][catalog]")
{
    const auto path = write_temp_catalog("draxul-satview-golden-stars-cap.dxstar", kGoldenStarCatalog);
    const std::vector<SatViewStarInstance> stars = load_satview_star_catalog(1, path);
    std::error_code error;
    std::filesystem::remove(path, error);

    REQUIRE(stars.size() == 1u);
    CHECK(stars[0].direction_magnitude.w == Approx(-1.5f).margin(1e-9));
}

TEST_CASE("SatView constellation line catalog golden byte layout loads exactly",
    "[satview][catalog]")
{
    const auto path = write_temp_catalog("draxul-satview-golden-lines.dxline", kGoldenLineCatalog);
    const std::vector<SatViewCelestialLineInstance> lines = load_satview_constellation_catalog(path);
    std::error_code error;
    std::filesystem::remove(path, error);

    REQUIRE(lines.size() == 2u);
    CHECK(lines[0].start_direction_width.x == Approx(1.0f).margin(1e-9));
    CHECK(lines[0].start_direction_width.y == Approx(0.0f).margin(1e-9));
    CHECK(lines[0].start_direction_width.z == Approx(0.0f).margin(1e-9));
    CHECK(lines[0].start_direction_width.w == Approx(1.35f).margin(1e-6));
    CHECK(lines[0].end_direction_dash.y == Approx(1.0f).margin(1e-9));
    CHECK(lines[0].end_direction_dash.w == Approx(0.0f).margin(1e-9));
    CHECK(lines[1].start_direction_width.z == Approx(1.0f).margin(1e-9));
    CHECK(lines[1].end_direction_dash.y == Approx(-1.0f).margin(1e-9));
}

TEST_CASE("SatView constellation boundary catalog golden byte layout loads exactly",
    "[satview][catalog]")
{
    const auto path = write_temp_catalog("draxul-satview-golden-boundaries.dxbnd", kGoldenBoundaryCatalog);
    const SatViewConstellationBoundaryCatalog catalog = load_satview_constellation_boundary_catalog(path);
    std::error_code error;
    std::filesystem::remove(path, error);

    REQUIRE(catalog.segments.size() == 2u);
    REQUIRE(catalog.labels.size() == 1u);
    CHECK(catalog.segments[0].start_direction.x == Approx(1.0f).margin(1e-9));
    CHECK(catalog.segments[0].end_direction.y == Approx(1.0f).margin(1e-9));
    CHECK(catalog.segments[0].source_feature == 0u);
    CHECK(catalog.segments[1].start_direction.z == Approx(1.0f).margin(1e-9));
    CHECK(catalog.segments[1].source_feature == 7u);
    CHECK(catalog.labels[0].direction.y == Approx(1.0f).margin(1e-9));
    CHECK(catalog.labels[0].rank == 1u);
    CHECK(catalog.labels[0].designation == "And");
    CHECK(catalog.labels[0].name == "Andromeda");
    CHECK(catalog.labels[0].area_index == 0u);
}

TEST_CASE("SatView shipped star catalog matches its golden framing", "[satview][catalog]")
{
    const auto path = resolve_satview_asset_path("catalog/stars.dxstar");
    REQUIRE(std::filesystem::exists(path));

    CHECK(read_file_prefix(path, kShippedStarHeader.size()) == kShippedStarHeader);
    // Payload is exactly record_count * record_size bytes — no trailing data.
    CHECK(require_file_size(path) == 32u + 100000u * 32u);

    const std::vector<SatViewStarInstance> stars = load_satview_star_catalog(200000, path);
    REQUIRE(stars.size() == 100000u);
    // Brightest star (Sirius, sorted first by magnitude).
    CHECK(stars[0].direction_magnitude.x == Approx(-0.93922764f).margin(1e-6));
    CHECK(stars[0].direction_magnitude.y == Approx(-0.28758022f).margin(1e-6));
    CHECK(stars[0].direction_magnitude.z == Approx(0.18748087f).margin(1e-6));
    CHECK(stars[0].direction_magnitude.w == Approx(-1.44f).margin(1e-6));
    CHECK(stars[0].color_size.x == Approx(0.76922685f).margin(1e-6));
    CHECK(stars[0].color_size.y == Approx(0.85606933f).margin(1e-6));
    CHECK(stars[0].color_size.z == Approx(1.0f).margin(1e-6));
    CHECK(stars[0].color_size.w == Approx(0.009f).margin(1e-6));
}

TEST_CASE("SatView shipped constellation line catalog matches its golden framing",
    "[satview][catalog]")
{
    const auto path = resolve_satview_asset_path("catalog/constellations.dxline");
    REQUIRE(std::filesystem::exists(path));

    CHECK(read_file_prefix(path, kShippedLineHeader.size()) == kShippedLineHeader);
    CHECK(require_file_size(path) == 32u + 656u * 24u);

    const std::vector<SatViewCelestialLineInstance> lines = load_satview_constellation_catalog(path);
    REQUIRE(lines.size() == 656u);
    CHECK(lines[0].start_direction_width.x == Approx(0.06733634f).margin(1e-5));
    CHECK(lines[0].start_direction_width.y == Approx(0.72488374f).margin(1e-5));
    CHECK(lines[0].start_direction_width.z == Approx(-0.68557233f).margin(1e-5));
    CHECK(lines[0].end_direction_dash.x == Approx(0.06107398f).margin(1e-5));
}

TEST_CASE("SatView shipped constellation boundary catalog matches its golden framing",
    "[satview][catalog]")
{
    const auto path = resolve_satview_asset_path("catalog/constellation_boundaries.dxbnd");
    REQUIRE(std::filesystem::exists(path));

    CHECK(read_file_prefix(path, kShippedBoundaryHeader.size()) == kShippedBoundaryHeader);
    // string_offset + string_size == file size — no trailing data.
    CHECK(require_file_size(path) == 160856u + 681u);

    const SatViewConstellationBoundaryCatalog catalog = load_satview_constellation_boundary_catalog(path);
    REQUIRE(catalog.segments.size() == 4936u);
    REQUIRE(catalog.labels.size() == 89u);
    CHECK(catalog.segments[0].source_feature == 0u);
    CHECK(catalog.segments[0].start_direction.x == Approx(0.21893497f).margin(1e-5));
    CHECK(catalog.labels[0].designation == "And");
    CHECK(catalog.labels[0].name == "Andromeda");
    CHECK(catalog.labels[0].rank == 1u);
    CHECK(catalog.labels[88].designation == "Vul");
    CHECK(catalog.labels[88].name == "Vulpecula");
    CHECK(catalog.labels[88].area_index == 88u);
}

// --- Negative framing: malformed containers must be rejected, not crash. ---
//
// The single-table header lays out (little-endian, no padding):
//   [0..7] magic, [8] version, [12] header_size, [16] record_size,
//   [20] record_count, [24] flags, [28] source_id.
// kCatalogContainerMaximumRecordCount is 1'000'000, so record_count
// 1'000'001 = 0x000F4241 overflows the limit.

TEST_CASE("SatView star catalog rejects malformed framing", "[satview][catalog]")
{
    auto load_bad = [](std::vector<unsigned char> bytes) {
        const auto path = write_temp_catalog("draxul-satview-bad-stars.dxstar", bytes);
        const auto stars = load_satview_star_catalog(16, path);
        std::error_code error;
        std::filesystem::remove(path, error);
        return stars;
    };

    SECTION("bad magic")
    {
        auto bytes = kGoldenStarCatalog;
        bytes[0] = 0xff;
        CHECK(load_bad(bytes).empty());
    }
    SECTION("bad version")
    {
        auto bytes = kGoldenStarCatalog;
        bytes[8] = 0x02;
        CHECK(load_bad(bytes).empty());
    }
    SECTION("header_size smaller than the 32-byte header")
    {
        auto bytes = kGoldenStarCatalog;
        bytes[12] = 0x10; // header_size 16
        CHECK(load_bad(bytes).empty());
    }
    SECTION("wrong record_size")
    {
        auto bytes = kGoldenStarCatalog;
        bytes[16] = 0x18; // record_size 24, expected 32
        CHECK(load_bad(bytes).empty());
    }
    SECTION("record_count overflow")
    {
        auto bytes = kGoldenStarCatalog;
        bytes[20] = 0x41; // record_count 1'000'001 (> limit)
        bytes[21] = 0x42;
        bytes[22] = 0x0f;
        bytes[23] = 0x00;
        CHECK(load_bad(bytes).empty());
    }
    SECTION("truncated payload")
    {
        auto bytes = kGoldenStarCatalog;
        bytes[20] = 0x03; // claims 3 records; only 2 are present
        CHECK(load_bad(bytes).empty());
    }
    SECTION("truncated header")
    {
        std::vector<unsigned char> bytes(
            kGoldenStarCatalog.begin(), kGoldenStarCatalog.begin() + 20);
        CHECK(load_bad(bytes).empty());
    }
}

TEST_CASE("SatView constellation line catalog rejects malformed framing", "[satview][catalog]")
{
    auto load_bad = [](std::vector<unsigned char> bytes) {
        const auto path = write_temp_catalog("draxul-satview-bad-lines.dxline", bytes);
        const auto lines = load_satview_constellation_catalog(path);
        std::error_code error;
        std::filesystem::remove(path, error);
        return lines;
    };

    SECTION("bad magic")
    {
        auto bytes = kGoldenLineCatalog;
        bytes[7] = 0x00; // "DXCLINE\0" != "DXCLINE1"
        CHECK(load_bad(bytes).empty());
    }
    SECTION("bad version")
    {
        auto bytes = kGoldenLineCatalog;
        bytes[8] = 0x09;
        CHECK(load_bad(bytes).empty());
    }
    SECTION("wrong record_size")
    {
        auto bytes = kGoldenLineCatalog;
        bytes[16] = 0x20; // record_size 32, expected 24
        CHECK(load_bad(bytes).empty());
    }
    SECTION("record_count overflow")
    {
        auto bytes = kGoldenLineCatalog;
        bytes[20] = 0x41; // record_count 1'000'001 (> limit)
        bytes[21] = 0x42;
        bytes[22] = 0x0f;
        bytes[23] = 0x00;
        CHECK(load_bad(bytes).empty());
    }
    SECTION("truncated payload")
    {
        auto bytes = kGoldenLineCatalog;
        bytes[20] = 0x08; // claims 8 records; only 2 are present
        CHECK(load_bad(bytes).empty());
    }
}

TEST_CASE("SatView constellation line catalog skips non-finite segments", "[satview][catalog]")
{
    // Poison record 0's start.x (first float after the 32-byte header) with a
    // quiet NaN (0x7fc00000). Semantic validation lives in the loader, so the
    // bad segment is dropped while the well-formed record 1 survives.
    auto bytes = kGoldenLineCatalog;
    bytes[32] = 0x00;
    bytes[33] = 0x00;
    bytes[34] = 0xc0;
    bytes[35] = 0x7f;
    const auto path = write_temp_catalog("draxul-satview-nonfinite-lines.dxline", bytes);
    const std::vector<SatViewCelestialLineInstance> lines = load_satview_constellation_catalog(path);
    std::error_code error;
    std::filesystem::remove(path, error);

    REQUIRE(lines.size() == 1u);
    CHECK(lines[0].start_direction_width.z == Approx(1.0f).margin(1e-9));
}

// --- Direct unit tests for the shared container framing primitives. ---

TEST_CASE("catalog_container_range_fits bounds payload regions", "[satview][catalog]")
{
    CHECK(catalog_container_range_fits(0, 100, 100));
    CHECK(catalog_container_range_fits(32, 64, 96));
    CHECK(catalog_container_range_fits(100, 0, 100)); // empty region at the end
    CHECK_FALSE(catalog_container_range_fits(32, 96, 96));
    CHECK_FALSE(catalog_container_range_fits(200, 0, 100)); // offset past the end
    CHECK_FALSE(catalog_container_range_fits(64, 64, 96));
}

TEST_CASE("catalog_container_prologue_matches enforces magic/version/header", "[satview][catalog]")
{
    const std::array<char, 8> star = { 'D', 'X', 'S', 'T', 'A', 'R', '1', '\0' };
    const std::array<char, 8> line = { 'D', 'X', 'C', 'L', 'I', 'N', 'E', '1' };
    const CatalogContainerPrologue prologue{ star, 1, 32 };

    CHECK(catalog_container_prologue_matches(prologue, star, 1, 32));
    CHECK(catalog_container_prologue_matches(prologue, star, 1, 16)); // larger header ok
    CHECK_FALSE(catalog_container_prologue_matches(prologue, line, 1, 32)); // magic
    CHECK_FALSE(catalog_container_prologue_matches(prologue, star, 2, 32)); // version
    CHECK_FALSE(catalog_container_prologue_matches(prologue, star, 1, 40)); // header too small
}

TEST_CASE("catalog_container_table_fits enforces record framing", "[satview][catalog]")
{
    CHECK(catalog_container_table_fits(32, 32, 2, 32, 96));
    CHECK_FALSE(catalog_container_table_fits(24, 32, 2, 32, 96)); // wrong record size
    CHECK_FALSE(catalog_container_table_fits(
        32, 32, kCatalogContainerMaximumRecordCount + 1, 32, std::uint64_t{ 1 } << 40)); // overflow
    CHECK_FALSE(catalog_container_table_fits(32, 32, 3, 32, 96)); // truncated payload
}
