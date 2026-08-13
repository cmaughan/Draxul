#include <draxul/satview/satview_catalog_container.h>

namespace draxul::satview
{

bool catalog_container_range_fits(
    std::uint64_t offset, std::uint64_t size, std::uint64_t total_size)
{
    return offset <= total_size && size <= total_size - offset;
}

bool catalog_container_prologue_matches(const CatalogContainerPrologue& prologue,
    const std::array<char, 8>& expected_magic,
    std::uint32_t expected_version,
    std::uint32_t minimum_header_size)
{
    return prologue.magic == expected_magic
        && prologue.version == expected_version
        && prologue.header_size >= minimum_header_size;
}

bool catalog_container_table_fits(std::uint32_t record_size,
    std::uint32_t expected_record_size,
    std::uint32_t record_count,
    std::uint64_t table_offset,
    std::uint64_t file_size)
{
    if (record_size != expected_record_size
        || record_count > kCatalogContainerMaximumRecordCount)
    {
        return false;
    }
    const std::uint64_t table_bytes = static_cast<std::uint64_t>(record_count) * static_cast<std::uint64_t>(record_size);
    return catalog_container_range_fits(table_offset, table_bytes, file_size);
}

std::optional<SingleTableCatalogHeader> open_single_table_catalog(std::istream& stream,
    const std::array<char, 8>& expected_magic,
    std::uint32_t expected_version,
    std::uint32_t expected_record_size)
{
    stream.seekg(0, std::ios::end);
    const std::streamoff stream_size = stream.tellg();
    if (!stream || stream_size < 0)
        return std::nullopt;
    const std::uint64_t file_size = static_cast<std::uint64_t>(stream_size);
    stream.seekg(0, std::ios::beg);

    SingleTableCatalogHeader header;
    stream.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!stream
        || !catalog_container_prologue_matches(header.prologue, expected_magic, expected_version,
            static_cast<std::uint32_t>(sizeof(SingleTableCatalogHeader)))
        || !catalog_container_table_fits(header.record_size, expected_record_size,
            header.record_count, header.prologue.header_size, file_size))
    {
        return std::nullopt;
    }

    stream.seekg(static_cast<std::streamoff>(header.prologue.header_size), std::ios::beg);
    if (!stream)
        return std::nullopt;
    return header;
}

} // namespace draxul::satview
