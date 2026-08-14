#pragma once

// Shared container framing for SatView's binary catalog assets.
//
// Every catalog (DXSTAR1, DXCLINE1, DXCBND01) starts with the same 16-byte
// little-endian prologue: an 8-byte magic tag, a u32 format version, and a
// u32 header size. Single-table catalogs (DXSTAR1, DXCLINE1) extend the
// prologue to the 32-byte SingleTableCatalogHeader; multi-table catalogs
// (DXCBND01) append their own u32 fields (per-table record sizes, counts and
// offsets, string blob bounds). The Python writer counterpart lives in
// tools/satview_catalog_container.py.
//
// Compatibility:
// - Structures are packed (no padding) and stored little-endian; loaders run
//   on little-endian targets only (Windows x64, macOS arm64/x64).
// - The shipped assets use version 1 of each format. Existing versions must
//   keep loading; do not bump a version or rewrite shipped assets without a
//   deterministic migration plan that preserves provenance/attribution.
// - header_size may exceed the struct size so a future version can append
//   header fields; payload locations always come from the header values.
//
// Limits:
// - At most kCatalogContainerMaximumRecordCount records per table.
// - At most kCatalogContainerMaximumStringBytes bytes of string payload.
// - Offsets and sizes are u32, so a container must stay below 4 GiB.

#include <array>
#include <cstdint>
#include <istream>
#include <optional>

namespace draxul::satview
{

inline constexpr std::uint32_t kCatalogContainerMaximumRecordCount = 1'000'000;
inline constexpr std::uint32_t kCatalogContainerMaximumStringBytes = 1'000'000;

// Common first 16 bytes of every catalog header.
struct CatalogContainerPrologue
{
    std::array<char, 8> magic{};
    std::uint32_t version = 0;
    std::uint32_t header_size = 0;
};
static_assert(sizeof(CatalogContainerPrologue) == 16);

// Single-table catalog header used by DXSTAR1 and DXCLINE1 (32 bytes).
struct SingleTableCatalogHeader
{
    CatalogContainerPrologue prologue{};
    std::uint32_t record_size = 0;
    std::uint32_t record_count = 0;
    std::uint32_t flags = 0;
    std::uint32_t source_id = 0;
};
static_assert(sizeof(SingleTableCatalogHeader) == 32);

// True when [offset, offset + size) fits inside a payload of total_size bytes.
[[nodiscard]] bool catalog_container_range_fits(
    std::uint64_t offset, std::uint64_t size, std::uint64_t total_size);

// Validates the shared framing prefix: magic, version, and a header of at
// least minimum_header_size bytes (larger headers are forward-compatible).
[[nodiscard]] bool catalog_container_prologue_matches(
    const CatalogContainerPrologue& prologue,
    const std::array<char, 8>& expected_magic,
    std::uint32_t expected_version,
    std::uint32_t minimum_header_size);

// Validates one record table: exact record size, count within the container
// limit, and a payload region that fits inside the file.
[[nodiscard]] bool catalog_container_table_fits(
    std::uint32_t record_size,
    std::uint32_t expected_record_size,
    std::uint32_t record_count,
    std::uint64_t table_offset,
    std::uint64_t file_size);

// Reads and validates the framing of a single-table catalog: magic, version,
// header size, record size, record count, and payload bounds. On success the
// stream is positioned at the first record and the header is returned.
// Catalog-specific semantic validation stays with each loader.
[[nodiscard]] std::optional<SingleTableCatalogHeader> open_single_table_catalog(
    std::istream& stream,
    const std::array<char, 8>& expected_magic,
    std::uint32_t expected_version,
    std::uint32_t expected_record_size);

} // namespace draxul::satview
