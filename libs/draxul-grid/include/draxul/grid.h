#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <draxul/grid_sink.h>
#include <draxul/highlight.h>
#include <draxul/log.h>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

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
        // TODO: consider std::string for >32-byte clusters
        if (sv.size() > static_cast<size_t>(kMaxLen))
            DRAXUL_LOG_WARN(LogCategory::App, "CellText: cluster truncated from %zu to %d bytes",
                sv.size(), static_cast<int>(kMaxLen));
        len = static_cast<uint8_t>(std::min(sv.size(), static_cast<size_t>(kMaxLen)));
        std::memcpy(data.data(), sv.data(), len);
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

    bool is_dirty(int col, int row) const;
    void mark_dirty(int col, int row);
    void mark_all_dirty();
    void clear_dirty();
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

    int cols_ = 0, rows_ = 0;
    std::vector<Cell> cells_;
    std::vector<DirtyCell> dirty_cells_;
    std::vector<uint8_t> dirty_marks_;
    Cell empty_cell_;
};

} // namespace draxul
