#include "unix_pty_process.h"
#include <draxul/local_terminal_host.h>

#include <draxul/host_kind.h>
#include <draxul/perf_timing.h>

namespace draxul
{

namespace
{

class ShellHost : public LocalTerminalHost
{
public:
    std::string_view host_name() const override
    {
        return launch_options().kind == HostKind::Zsh ? "zsh" : "bash";
    }

protected:
    bool initialize_host() override
    {
        PERF_MEASURE();
        highlights().set_default_fg(
            launch_options().terminal_fg.value_or(Color(0.92f, 0.92f, 0.92f, 1.0f)));
        highlights().set_default_bg(
            launch_options().terminal_bg.value_or(Color(0.08f, 0.09f, 0.10f, 1.0f)));
        apply_grid_size(viewport().grid_size.x, viewport().grid_size.y);
        reset_terminal_state();

        std::string command = launch_options().command;
        if (command.empty())
            command = (launch_options().kind == HostKind::Zsh) ? "zsh" : "bash";

        if (!process_.spawn(command, launch_options().args, launch_options().working_dir, [this]() { callbacks().wake_window(); }, viewport().grid_size.x, viewport().grid_size.y))
        {
            set_init_error("Could not start " + command
                + ". Please ensure it is installed and available on your PATH.");
            return false;
        }

        for (const auto& cmd : launch_options().startup_commands)
            process_.write(cmd + "\r");

        update_cursor_style();
        return true;
    }

    bool do_process_write(std::string_view text) override
    {
        return process_.write(text);
    }
    std::vector<std::string> do_process_drain() override
    {
        return process_.drain_output();
    }
    bool do_process_resize(int cols, int rows) override
    {
        return process_.resize(cols, rows);
    }
    bool do_process_is_running() const override
    {
        return process_.is_running();
    }
    void do_process_shutdown() override
    {
        process_.shutdown();
    }
    void do_process_request_close() override
    {
        process_.request_close();
    }

private:
    UnixPtyProcess process_;
};

} // namespace

std::unique_ptr<IHost> create_shell_host()
{
    PERF_MEASURE();
    return std::make_unique<ShellHost>();
}

} // namespace draxul
