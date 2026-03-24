#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <draxul/grid_sink.h>
#include <draxul/highlight.h>
#include <draxul/log.h>
#include <draxul/thread_check.h>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

namespace detail
{

inline bool is_utf8_continuation_byte(uint8_t byte)
{
    return (byte & 0xC0) == 0x80;
}

inline bool utf8_next_codepoint_two_byte(const uint8_t* bytes, size_t offset, size_t remaining, size_t& next_offset)
{
    if (remaining < 2)
        return false;
    if (const uint8_t lead = bytes[offset];
        lead < 0xC2 || lead > 0xDF || !is_utf8_continuation_byte(bytes[offset + 1]))
        return false;
    next_offset = offset + 2;
    return true;
}

inline bool utf8_next_codepoint_three_byte(const uint8_t* bytes, size_t offset, size_t remaining, size_t& next_offset)
{
    if (remaining < 3)
        return false;

    const uint8_t lead = bytes[offset];
    const uint8_t b1 = bytes[offset + 1];
    const uint8_t b2 = bytes[offset + 2];
    if (const bool valid = (lead == 0xE0 && b1 >= 0xA0 && b1 <= 0xBF && is_utf8_continuation_byte(b2))
            || ((lead >= 0xE1 && lead <= 0xEC) && is_utf8_continuation_byte(b1) && is_utf8_continuation_byte(b2))
            || (lead == 0xED && b1 >= 0x80 && b1 <= 0x9F && is_utf8_continuation_byte(b2))
            || ((lead >= 0xEE && lead <= 0xEF) && is_utf8_continuation_byte(b1) && is_utf8_continuation_byte(b2));
        !valid)
        return false;

    next_offset = offset + 3;
    return true;
}

inline bool utf8_next_codepoint_four_byte(const uint8_t* bytes, size_t offset, size_t remaining, size_t& next_offset)
{
    if (remaining < 4)
        return false;

    const uint8_t lead = bytes[offset];
    const uint8_t b1 = bytes[offset + 1];
    const uint8_t b2 = bytes[offset + 2];
    const uint8_t b3 = bytes[offset + 3];
    if (const bool valid = (lead == 0xF0 && b1 >= 0x90 && b1 <= 0xBF && is_utf8_continuation_byte(b2)
                               && is_utf8_continuation_byte(b3))
            || ((lead >= 0xF1 && lead <= 0xF3) && is_utf8_continuation_byte(b1) && is_utf8_continuation_byte(b2)
                && is_utf8_continuation_byte(b3))
            || (lead == 0xF4 && b1 >= 0x80 && b1 <= 0x8F && is_utf8_continuation_byte(b2)
                && is_utf8_continuation_byte(b3));
        !valid)
        return false;

    next_offset = offset + 4;
    return true;
}

inline bool utf8_next_codepoint(std::string_view text, size_t offset, size_t limit, size_t& next_offset)
{
    if (offset >= limit)
        return false;

    const auto* bytes = reinterpret_cast<const uint8_t*>(text.data());
    const uint8_t lead = bytes[offset];
    const size_t remaining = limit - offset;

    if (lead < 0x80)
    {
        next_offset = offset + 1;
        return true;
    }

    if (utf8_next_codepoint_two_byte(bytes, offset, remaining, next_offset))
        return true;

    if (utf8_next_codepoint_three_byte(bytes, offset, remaining, next_offset))
        return true;

    if (utf8_next_codepoint_four_byte(bytes, offset, remaining, next_offset))
        return true;

    return false;
}

inline size_t utf8_valid_prefix_length(std::string_view text, size_t max_len)
{
    const size_t limit = std::min(text.size(), max_len);
    size_t offset = 0;
    size_t valid = 0;

    while (offset < limit)
    {
        size_t next = offset;
        if (!utf8_next_codepoint(text, offset, limit, next))
            break;
        offset = next;
        valid = next;
    }

    return valid;
}

} // namespace detail

struct CellText
{
    static constexpr uint8_t kMaxLen = 32;

    std::array<char, kMaxLen> data{};
    uint8_t len = 0;

    CellText() = default;

    explicit CellText(std::string_view sv)
    {
        assign(sv);
    }

    void assign(std::string_view sv)
    {
        // TODO: consider std::string for >32-byte clusters instead of truncating.
        if (sv.size() > static_cast<size_t>(kMaxLen))
            DRAXUL_LOG_WARN(LogCategory::App, "CellText: cluster truncated from %zu to %d bytes",
                sv.size(), static_cast<int>(kMaxLen));
        const size_t valid_len = sv.size() > static_cast<size_t>(kMaxLen)
            ? detail::utf8_valid_prefix_length(sv, static_cast<size_t>(kMaxLen))
            : sv.size();
        len = static_cast<uint8_t>(valid_len);
        std::memcpy(data.data(), sv.data(), valid_len);
    }

    std::string_view view() const
    {
        return { data.data(), len };
    }
    bool empty() const
    {
        return len == 0;
    }

    bool operator==(const CellText& o) const
    {
        return len == o.len && std::memcmp(data.data(), o.data.data(), len) == 0;
    }

    // Convenience: compare with string_view or string literals
    bool operator==(std::string_view sv) const
    {
        return view() == sv;
    }
};

struct Cell
{
    CellText text;
    uint16_t hl_attr_id = 0;
    bool dirty = true;
    bool double_width = false;
    bool double_width_cont = false;
};

class Grid : public IGridSink
{
public:
    void resize(int cols, int rows) override;
    void clear() override;

    void set_cell(int col, int row, const std::string& text, uint16_t hl_id, bool double_width) override;
    const Cell& get_cell(int col, int row) const;

    void scroll(int top, int bot, int left, int right, int rows, int cols = 0) override;

    int cols() const
    {
        return cols_;
    }
    int rows() const
    {
        return rows_;
    }

    int sink_cols() const override
    {
        return cols_;
    }
    int sink_rows() const override
    {
        return rows_;
    }

    bool is_dirty(int col, int row) const;
    void mark_dirty(int col, int row);
    void mark_all_dirty();
    void clear_dirty();
    template <typename Remap>
    void remap_highlight_ids(Remap&& remap);
    size_t dirty_cell_count() const
    {
        return dirty_cells_.size();
    }

    struct DirtyCell
    {
        int col, row;
    };
    std::vector<DirtyCell> get_dirty_cells() const;

private:
    void mark_dirty_index(int index);

    int cols_ = 0;
    int rows_ = 0;
    std::vector<Cell> cells_;
    std::vector<DirtyCell> dirty_cells_;
    std::vector<uint8_t> dirty_marks_;
    Cell empty_cell_;
    MainThreadChecker thread_checker_;
};

template <typename Remap>
void Grid::remap_highlight_ids(Remap&& remap)
{
    for (size_t i = 0; i < cells_.size(); ++i)
    {
        auto& cell = cells_[i];
        const uint16_t remapped = remap(cell.hl_attr_id);
        if (remapped == cell.hl_attr_id)
            continue;
        cell.hl_attr_id = remapped;
        mark_dirty_index(static_cast<int>(i));
    }
}

} // namespace draxul
