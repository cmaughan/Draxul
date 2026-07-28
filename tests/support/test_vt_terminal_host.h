#pragma once

#include <draxul/terminal_host_base.h>

#include <string>
#include <string_view>
#include <vector>

namespace draxul::tests
{

class TestVtTerminalHost : public TerminalHostBase
{
public:
    void feed(std::string_view bytes)
    {
        consume_output(bytes);
    }

    void queue_drain(std::string bytes)
    {
        drained_chunks_.push_back(std::move(bytes));
    }

    std::string cell_text(int col, int row)
    {
        return std::string(grid().get_cell(col, row).text.view());
    }

    uint16_t cell_hl(int col, int row)
    {
        return grid().get_cell(col, row).hl_attr_id;
    }

    uint16_t cell_link(int col, int row)
    {
        return grid().effective_link_id(col, row);
    }

    std::string cell_link_uri(int col, int row)
    {
        return std::string(grid().link_uri(grid().effective_link_id(col, row)));
    }

    TerminalSemanticSnapshot snapshot() const
    {
        return semantic_snapshot();
    }

    int col() const
    {
        return vt_state().col;
    }

    int row() const
    {
        return vt_state().row;
    }

    int scroll_top() const
    {
        return vt_state().scroll_top;
    }

    int scroll_bottom() const
    {
        return vt_state().scroll_bottom;
    }

    std::string written;

    int cols_ = 20;
    int rows_ = 5;

protected:
    std::string_view host_name() const override
    {
        return "test";
    }

    bool initialize_host() override
    {
        highlights().set_default_fg({ 1.0f, 1.0f, 1.0f, 1.0f });
        highlights().set_default_bg({ 0.0f, 0.0f, 0.0f, 1.0f });
        apply_grid_size(cols_, rows_);
        reset_terminal_state();
        set_content_ready(true);
        return true;
    }

    bool do_process_write(std::string_view text) override
    {
        written += text;
        return true;
    }

    std::vector<std::string> do_process_drain() override
    {
        auto out = std::move(drained_chunks_);
        drained_chunks_.clear();
        return out;
    }

    bool do_process_resize(int, int) override
    {
        return true;
    }

    bool do_process_is_running() const override
    {
        return true;
    }

    void do_process_shutdown() override {}

private:
    std::vector<std::string> drained_chunks_;
};

} // namespace draxul::tests
