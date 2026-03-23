#include "conpty_process.h"
#include <draxul/local_terminal_host.h>

namespace draxul
{

namespace
{

class PowerShellHost : public LocalTerminalHost
{
public:
    std::string_view host_name() const override
    {
        return "powershell";
    }

protected:
    bool initialize_host() override
    {
        highlights().set_default_fg(
            launch_options().terminal_fg.value_or(Color(0.92f, 0.92f, 0.92f, 1.0f)));
        highlights().set_default_bg(
            launch_options().terminal_bg.value_or(Color(0.08f, 0.09f, 0.10f, 1.0f)));
        apply_grid_size(viewport().grid_size.x, viewport().grid_size.y);
        reset_terminal_state();

        std::string command = launch_options().command.empty() ? "pwsh.exe" : launch_options().command;
        std::vector<std::string> args = launch_options().args;
        if (args.empty())
        {
            args = {
                "-NoLogo",
                "-NoExit",
                "-Command",
                "[Console]::InputEncoding=[System.Text.UTF8Encoding]::UTF8; [Console]::OutputEncoding=[System.Text.UTF8Encoding]::UTF8"
            };
        }

        auto wake = callbacks().wake_window;
        const int cols = grid_cols();
        const int rows = grid_rows();
        if (!process_.spawn(command, args, launch_options().working_dir, cols, rows, [wake]() {
                if (wake)
                    wake();
            }))
        {
            if (launch_options().command.empty() && command != "powershell.exe"
                && process_.spawn("powershell.exe", args, launch_options().working_dir, cols, rows, [wake]() {
                       if (wake)
                           wake();
                   }))
            {
                command = "powershell.exe";
            }
            else
            {
                set_init_error("Could not start PowerShell. Please install PowerShell 7 or ensure powershell.exe is available.");
                return false;
            }
        }

        for (const auto& startup : launch_options().startup_commands)
            process_.write(startup + "\r");

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

private:
    ConPtyProcess process_;
};

} // namespace

std::unique_ptr<IHost> create_powershell_host()
{
    return std::make_unique<PowerShellHost>();
}

} // namespace draxul
