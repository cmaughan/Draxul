#include <draxul/terminal_snapshot.h>

#include <bit>
#include <string_view>
#include <type_traits>

namespace draxul
{

namespace
{

class DigestWriter
{
public:
    void bytes(std::string_view value)
    {
        integer(static_cast<uint64_t>(value.size()));
        for (const unsigned char byte : value)
            append(byte);
    }

    void boolean(bool value)
    {
        append(value ? 1 : 0);
    }

    template <typename T>
    void integer(T value)
    {
        using Unsigned = std::make_unsigned_t<T>;
        Unsigned bits = static_cast<Unsigned>(value);
        for (size_t index = 0; index < sizeof(Unsigned); ++index)
        {
            append(static_cast<uint8_t>(bits & 0xFF));
            bits >>= 8;
        }
    }

    void color(const Color& value)
    {
        integer(std::bit_cast<uint32_t>(value.r));
        integer(std::bit_cast<uint32_t>(value.g));
        integer(std::bit_cast<uint32_t>(value.b));
        integer(std::bit_cast<uint32_t>(value.a));
    }

    uint64_t value() const
    {
        return value_;
    }

private:
    void append(uint8_t byte)
    {
        value_ ^= byte;
        value_ *= 1099511628211ULL;
    }

    uint64_t value_ = 14695981039346656037ULL;
};

void append_attr(DigestWriter& writer, const HlAttr& attr)
{
    writer.color(attr.fg);
    writer.color(attr.bg);
    writer.color(attr.sp);
    writer.boolean(attr.has_fg);
    writer.boolean(attr.has_bg);
    writer.boolean(attr.has_sp);
    writer.boolean(attr.bold);
    writer.boolean(attr.italic);
    writer.boolean(attr.underline);
    writer.boolean(attr.undercurl);
    writer.boolean(attr.strikethrough);
    writer.boolean(attr.reverse);
}

TerminalCellSnapshot capture_grid_cell(
    const Grid& grid, const HighlightTable& highlights, int col, int row)
{
    const Cell& source = grid.get_cell(col, row);
    const uint16_t link_id = grid.effective_link_id(col, row);
    return capture_terminal_cell_snapshot(source, highlights,
        link_id != 0 ? grid.link_uri(link_id) : std::string_view{});
}

} // namespace

TerminalCellSnapshot capture_terminal_cell_snapshot(
    const Cell& source,
    const HighlightTable& highlights,
    std::string_view hyperlink)
{
    TerminalCellSnapshot cell;
    cell.text = source.text.view();
    cell.attr = highlights.get(source.hl_attr_id);
    cell.double_width = source.double_width;
    cell.double_width_continuation = source.double_width_cont;
    cell.hyperlink = hyperlink;
    return cell;
}

TerminalSemanticSnapshot capture_terminal_semantic_snapshot(
    const Grid& grid,
    const HighlightTable& highlights,
    TerminalSnapshotMetadata metadata)
{
    TerminalSemanticSnapshot snapshot;
    snapshot.cols = grid.cols();
    snapshot.rows = grid.rows();
    snapshot.metadata = std::move(metadata);

    const size_t cell_count = static_cast<size_t>(snapshot.cols) * snapshot.rows;
    snapshot.cells.reserve(cell_count);
    for (int row = 0; row < snapshot.rows; ++row)
    {
        for (int col = 0; col < snapshot.cols; ++col)
            snapshot.cells.push_back(
                capture_grid_cell(grid, highlights, col, row));
    }
    return snapshot;
}

TerminalDirtySnapshot capture_terminal_dirty_snapshot(
    const Grid& grid,
    const HighlightTable& highlights,
    TerminalSnapshotMetadata metadata)
{
    TerminalDirtySnapshot snapshot;
    snapshot.cols = grid.cols();
    snapshot.rows = grid.rows();
    snapshot.full = grid.is_full_dirty();
    snapshot.metadata = std::move(metadata);

    if (snapshot.full)
    {
        const size_t cell_count
            = static_cast<size_t>(snapshot.cols) * snapshot.rows;
        snapshot.cells.reserve(cell_count);
        for (int row = 0; row < snapshot.rows; ++row)
        {
            for (int col = 0; col < snapshot.cols; ++col)
            {
                snapshot.cells.push_back(TerminalDirtyCellSnapshot{
                    .col = col,
                    .row = row,
                    .cell = capture_grid_cell(grid, highlights, col, row),
                });
            }
        }
        return snapshot;
    }

    const auto dirty_cells = grid.get_dirty_cells();
    snapshot.cells.reserve(dirty_cells.size());
    for (const Grid::DirtyCell& position : dirty_cells)
    {
        snapshot.cells.push_back(TerminalDirtyCellSnapshot{
            .col = position.col,
            .row = position.row,
            .cell = capture_grid_cell(
                grid, highlights, position.col, position.row),
        });
    }
    return snapshot;
}

uint64_t terminal_semantic_digest(const TerminalSemanticSnapshot& snapshot)
{
    DigestWriter writer;
    writer.integer(snapshot.cols);
    writer.integer(snapshot.rows);

    for (const auto& cell : snapshot.cells)
    {
        writer.bytes(cell.text);
        append_attr(writer, cell.attr);
        writer.boolean(cell.double_width);
        writer.boolean(cell.double_width_continuation);
        writer.bytes(cell.hyperlink);
    }

    const auto& metadata = snapshot.metadata;
    writer.integer(metadata.cursor.col);
    writer.integer(metadata.cursor.row);
    writer.boolean(metadata.cursor.visible);
    writer.integer(static_cast<uint8_t>(metadata.cursor.shape));
    writer.boolean(metadata.cursor.blink);
    writer.boolean(metadata.modes.alternate_screen);
    writer.boolean(metadata.modes.auto_wrap);
    writer.boolean(metadata.modes.origin);
    writer.boolean(metadata.modes.cursor_application);
    writer.boolean(metadata.modes.bracketed_paste);
    writer.boolean(metadata.modes.focus_reporting);
    writer.boolean(metadata.modes.synchronized_output);
    writer.boolean(metadata.modes.mouse.normal_tracking);
    writer.boolean(metadata.modes.mouse.button_motion);
    writer.boolean(metadata.modes.mouse.any_motion);
    writer.boolean(metadata.modes.mouse.sgr_coordinates);
    writer.bytes(metadata.title);
    writer.bytes(metadata.working_directory);
    writer.integer(static_cast<uint64_t>(metadata.shell_marks.size()));
    for (const auto& mark : metadata.shell_marks)
    {
        writer.integer(static_cast<uint8_t>(mark.kind));
        writer.integer(mark.row);
        writer.integer(mark.exit_code);
    }
    return writer.value();
}

TerminalDirtySnapshot full_grid_update(
    const TerminalSemanticSnapshot& snapshot)
{
    TerminalDirtySnapshot update{
        .cols = snapshot.cols,
        .rows = snapshot.rows,
        .full = true,
        .metadata = snapshot.metadata,
    };
    update.cells.reserve(snapshot.cells.size());
    for (int row = 0; row < snapshot.rows; ++row)
    {
        for (int col = 0; col < snapshot.cols; ++col)
        {
            update.cells.push_back({
                .col = col,
                .row = row,
                .cell = snapshot.cells[static_cast<size_t>(row) * snapshot.cols + col],
            });
        }
    }
    return update;
}

} // namespace draxul
