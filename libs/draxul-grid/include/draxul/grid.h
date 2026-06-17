#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <draxul/grid_sink.h>
#include <draxul/highlight.h>
#include <draxul/thread_check.h>
#include <string>
#include <string_view>
#include <unordered_map>
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

    void assign(std::string_view sv);

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
    uint16_t hyperlink_id = 0;
    uint16_t detected_url_id = 0;
};

class Grid : public IGridSink
{
public:
    void resize(int cols, int rows) override;
    void clear() override;

    void set_cell(int col, int row, const std::string& text, uint16_t hl_id, bool double_width) override;
    const Cell& get_cell(int col, int row) const;
    void set_cell_hyperlink_id(int col, int row, uint16_t link_id);
    uint16_t link_id_for_uri(std::string_view uri);
    std::string_view link_uri(uint16_t link_id) const;
    uint16_t effective_link_id(int col, int row) const;
    bool cell_has_explicit_hyperlink(int col, int row) const;
    void refresh_url_detection_for_dirty_rows(bool enable);

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
        if (full_dirty_)
            return static_cast<size_t>(cols_) * static_cast<size_t>(rows_);
        return dirty_cells_.size();
    }
    bool is_full_dirty() const
    {
        return full_dirty_;
    }

    struct DirtyCell
    {
        int col, row;
    };
    std::vector<DirtyCell> get_dirty_cells() const;

private:
    void mark_dirty_index(int index);
    void clear_detected_url_ids_for_row(int row);
    void detect_urls_for_row(int row);

    int cols_ = 0;
    int rows_ = 0;
    std::vector<Cell> cells_;
    std::vector<std::string> link_uris_;
    std::unordered_map<std::string, uint16_t> link_ids_;
    std::vector<DirtyCell> dirty_cells_;
    std::vector<uint8_t> dirty_marks_;
    bool full_dirty_ = false;
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
