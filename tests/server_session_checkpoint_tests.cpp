#include <catch2/catch_test_macros.hpp>

#include "support/server_kernel_test_support.h"

using namespace draxul;
using draxul::tests::TempDir;
using namespace draxul::tests::server_kernel;

TEST_CASE("server-owned shell survives every client detaching and reconnecting",
    "[server][remote-terminal][process]")
{
    TempDir temp("draxul-real-remote-terminal");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto controller
        = remote_client(temp.path, "real-a", "fixed-epoch", "terminal");
    auto observer
        = remote_client(temp.path, "real-b", "fixed-epoch", "terminal");
    std::string error;
    REQUIRE(controller.attach(error));
    INFO(error);
    REQUIRE(observer.attach(error));
    INFO(error);

    // The pane is named after the platform's default shell: deterministic on
    // Windows, $SHELL-dependent on POSIX (Zsh on stock macOS, Bash elsewhere).
#ifdef _WIN32
    CHECK(controller.projection().pane().name == "PowerShell");
#else
    CHECK_FALSE(controller.projection().pane().name.empty());
    CHECK(controller.projection().pane().name != "PowerShell");
#endif
    const uint64_t process_id = controller.projection().pane().process_id;
    const uint64_t generation
        = controller.projection().version().generation;
    REQUIRE(process_id != 0);
    REQUIRE(observer.projection().pane().process_id == process_id);
    REQUIRE(observer.projection().version().generation == generation);
#ifdef _WIN32
    REQUIRE(parent_process_id(process_id) == server.process_id());
    const std::string shared_command
        = "Write-Output '__DRAXUL_SHARED__'\r";
    const std::string delayed_command
        = "Start-Sleep -Milliseconds 250; Write-Output '__DRAXUL_DETACHED__'\r";
#else
    const std::string shared_command
        = "printf '__DRAXUL_SHARED__\\n'\r";
    const std::string delayed_command
        = "sleep 0.25; printf '__DRAXUL_DETACHED__\\n'\r";
#endif

    REQUIRE(controller.send_input(shared_command, error));
    REQUIRE(wait_for_text(observer, "__DRAXUL_SHARED__", error));
    INFO(error);
    REQUIRE(controller.resize(72, 20, error));
    bool resized = false;
    for (int attempt = 0; attempt < 50 && !resized; ++attempt)
    {
        bool changed = false;
        REQUIRE(observer.poll(changed, error));
        resized = observer.projection().snapshot().cols == 72
            && observer.projection().snapshot().rows == 20;
        if (!resized)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    REQUIRE(resized);

    REQUIRE(controller.send_input(delayed_command, error));
    REQUIRE(controller.disconnect(error));
    REQUIRE(observer.disconnect(error));
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    auto reconnected
        = remote_client(temp.path, "real-c", "fixed-epoch", "terminal");
    REQUIRE(reconnected.attach(error));
    INFO(error);
    REQUIRE(reconnected.projection().pane().process_id == process_id);
    REQUIRE(reconnected.projection().version().generation == generation);
    REQUIRE(wait_for_text(reconnected, "__DRAXUL_DETACHED__", error));
    INFO(error);

    REQUIRE(reconnected.send_input("exit\r", error));
    bool exit_observed = false;
    for (int attempt = 0; attempt < 100 && !exit_observed; ++attempt)
    {
        bool changed = false;
        REQUIRE(reconnected.poll(changed, error));
        exit_observed
            = !reconnected.projection().pane().process_running
            && reconnected.projection().pane().exit_code.has_value();
        if (!exit_observed)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(25));
        }
    }
    REQUIRE(exit_observed);
    REQUIRE(reconnected.projection().pane().exit_code);
    CHECK(*reconnected.projection().pane().exit_code == 0);
    auto after_restart
        = remote_client(temp.path, "real-d", "fixed-epoch", "terminal");
    REQUIRE(after_restart.attach(error));
    REQUIRE(after_restart.projection().version().generation == generation + 1);
    REQUIRE(after_restart.projection().pane().process_id != 0);
    REQUIRE(server.epoch() == "fixed-epoch");

    std::string shutdown_error;
    REQUIRE_FALSE(ServerClient::shutdown(
        temp.path, {}, shutdown_error));
    CHECK(shutdown_error.find("live terminal")
        != std::string::npos);
    CHECK(server.running());
    REQUIRE(ServerClient::shutdown(temp.path,
        { .confirm_live_terminals = true }, shutdown_error));
    run_guard.join();
}

TEST_CASE("binary shell output without subscribers leaves the server running",
    "[server][remote-terminal][process][unicode]")
{
    TempDir temp("draxul-real-remote-binary");
    ServerKernel server({
        .runtime_directory = temp.path,
        .build_version = "unit-test",
        .epoch_override = "fixed-epoch",
    });
    REQUIRE(server.start().disposition == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    auto client
        = remote_client(temp.path, "binary-a", "fixed-epoch", "terminal");
    std::string error;
    REQUIRE(client.attach(error));
    const auto completed = temp.path / "binary-output-complete";
#ifdef _WIN32
    const std::string binary_command
        = "Start-Sleep -Milliseconds 200; $b=[byte[]](0x80,0xFF); [Console]::OpenStandardOutput().Write($b,0,$b.Length); [IO.File]::WriteAllText('"
        + completed.string() + "','ok')\r";
#else
    const std::string binary_command
        = "sleep 0.2; printf '\\200\\377'; : > '"
        + completed.string() + "'\r";
#endif
    REQUIRE(client.send_input(binary_command, error));
    REQUIRE(client.disconnect(error));
    for (int attempt = 0;
        attempt < 400 && !std::filesystem::exists(completed);
        ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    REQUIRE(std::filesystem::exists(completed));

    const auto probe = ServerClient::probe(probe_options(temp.path));
    INFO(probe.error_code);
    INFO(probe.error_message);
    REQUIRE(probe.ready());
    REQUIRE(server.running());

    auto reconnected
        = remote_client(temp.path, "binary-b", "fixed-epoch", "terminal");
    REQUIRE(reconnected.attach(error));
    INFO(error);

    run_guard.join();
}

TEST_CASE("named server Sessions isolate topology and terminal identity across cold restore",
    "[server][topology][persistence][sessions]")
{
    TempDir temp("draxul-server-named-sessions");
    uint64_t alpha_process_id = 0;
    uint64_t beta_process_id = 0;

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .session_checkpoint_interval
            = std::chrono::milliseconds(20),
            .build_version = "unit-test",
            .epoch_override = "named-epoch-1",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard run_guard(server);

        TopologyClient alpha({
            .runtime_directory = temp.path,
            .client_id = "alpha-client",
            .session_id = "alpha",
        });
        TopologyClient beta({
            .runtime_directory = temp.path,
            .client_id = "beta-client",
            .session_id = "beta",
        });
        std::string error;
        REQUIRE(alpha.refresh(error));
        REQUIRE(beta.refresh(error));
        REQUIRE(alpha.snapshot().session_id == "alpha");
        REQUIRE(beta.snapshot().session_id == "beta");
        REQUIRE(alpha.snapshot().spaces.front().tabs.front().panes.front().terminal_id
            == beta.snapshot().spaces.front().tabs.front().panes.front().terminal_id);

        TopologyCommand rename_alpha{
            .command_id = "rename-alpha",
            .expected_revision = alpha.snapshot().revision,
            .kind = TopologyCommandKind::RenameSpace,
            .space_id = alpha.snapshot().spaces.front().space_id,
            .name = "Alpha Work",
        };
        TopologyCommandResult renamed_alpha;
        REQUIRE(alpha.execute(
            rename_alpha, renamed_alpha, error));
        REQUIRE(beta.snapshot().spaces.front().name == "Space 1");

        TopologyCommand rename_beta{
            .command_id = "rename-beta",
            .expected_revision = beta.snapshot().revision,
            .kind = TopologyCommandKind::RenameSpace,
            .space_id = beta.snapshot().spaces.front().space_id,
            .name = "Beta Work",
        };
        TopologyCommandResult renamed_beta;
        REQUIRE(beta.execute(rename_beta, renamed_beta, error));
        REQUIRE(alpha.snapshot().spaces.front().name
            == "Alpha Work");

        RemoteTerminalClient alpha_terminal({
            .runtime_directory = temp.path,
            .client_id = "alpha-terminal-client",
            .session_id = "alpha",
            .expected_server_epoch = "named-epoch-1",
            .method_prefix = "terminal",
            .terminal_id = std::string(kServerShellTerminalId),
        });
        RemoteTerminalClient beta_terminal({
            .runtime_directory = temp.path,
            .client_id = "beta-terminal-client",
            .session_id = "beta",
            .expected_server_epoch = "named-epoch-1",
            .method_prefix = "terminal",
            .terminal_id = std::string(kServerShellTerminalId),
        });
        REQUIRE(alpha_terminal.attach(error));
        REQUIRE(beta_terminal.attach(error));
        alpha_process_id
            = alpha_terminal.projection().pane().process_id;
        beta_process_id
            = beta_terminal.projection().pane().process_id;
        REQUIRE(alpha_process_id != 0);
        REQUIRE(beta_process_id != 0);
        REQUIRE(alpha_process_id != beta_process_id);

        const auto status = ServerClient::status(temp.path);
        REQUIRE(status.ok);
        REQUIRE(status.status->sessions == 3);
        REQUIRE(status.status->spaces == 3);
        REQUIRE(status.status->session_statuses.size() == 3);
        const auto alpha_status = std::ranges::find(
            status.status->session_statuses, "alpha",
            &ServerSessionStatusSnapshot::session_id);
        REQUIRE(alpha_status
            != status.status->session_statuses.end());
        REQUIRE(alpha_status->terminals == 1);
        REQUIRE(alpha_status->live_terminals == 1);

        run_guard.join();
    }

    const auto alpha_path
        = server_session_state_path(temp.path, "alpha");
    const auto beta_path
        = server_session_state_path(temp.path, "beta");
    REQUIRE(alpha_path != beta_path);
    REQUIRE(std::filesystem::exists(alpha_path));
    REQUIRE(std::filesystem::exists(beta_path));
    std::string error;
    const auto saved_alpha
        = load_session_state_from_path(alpha_path, &error);
    INFO(error);
    REQUIRE(saved_alpha);
    REQUIRE(saved_alpha->session_id == "alpha");
    const auto saved_beta
        = load_session_state_from_path(beta_path, &error);
    INFO(error);
    REQUIRE(saved_beta);
    REQUIRE(saved_beta->session_id == "beta");

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .build_version = "unit-test",
            .epoch_override = "named-epoch-2",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard run_guard(server);
        TopologyClient alpha({
            .runtime_directory = temp.path,
            .client_id = "alpha-restored",
            .session_id = "alpha",
        });
        TopologyClient beta({
            .runtime_directory = temp.path,
            .client_id = "beta-restored",
            .session_id = "beta",
        });
        REQUIRE(alpha.refresh(error));
        REQUIRE(beta.refresh(error));
        REQUIRE(alpha.snapshot().spaces.front().name
            == "Alpha Work");
        REQUIRE(beta.snapshot().spaces.front().name
            == "Beta Work");

        RemoteTerminalClient alpha_terminal({
            .runtime_directory = temp.path,
            .client_id = "alpha-terminal-restored",
            .session_id = "alpha",
            .expected_server_epoch = "named-epoch-2",
            .method_prefix = "terminal",
            .terminal_id = std::string(kServerShellTerminalId),
        });
        RemoteTerminalClient beta_terminal({
            .runtime_directory = temp.path,
            .client_id = "beta-terminal-restored",
            .session_id = "beta",
            .expected_server_epoch = "named-epoch-2",
            .method_prefix = "terminal",
            .terminal_id = std::string(kServerShellTerminalId),
        });
        REQUIRE(alpha_terminal.attach(error));
        REQUIRE(beta_terminal.attach(error));
        REQUIRE(alpha_terminal.projection().pane().process_id
            != alpha_process_id);
        REQUIRE(beta_terminal.projection().pane().process_id
            != beta_process_id);
        REQUIRE(alpha_terminal.projection().pane().process_id
            != beta_terminal.projection().pane().process_id);

        TopologyClient invalid({
            .runtime_directory = temp.path,
            .client_id = "invalid-session",
            .session_id = "invalid\nidentity",
        });
        REQUIRE_FALSE(invalid.refresh(error));
        REQUIRE(invalid.last_error_code() == "invalid_session");
        run_guard.join();
    }
}

TEST_CASE("server deletes a detached Session and its checkpoint",
    "[server][topology][persistence][sessions][delete]")
{
    TempDir temp("draxul-server-delete-session");
    const auto alpha_checkpoint
        = server_session_state_path(temp.path, "alpha");

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .session_checkpoint_interval
            = std::chrono::milliseconds(20),
            .build_version = "unit-test",
            .epoch_override = "delete-session-1",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard run_guard(server);

        TopologyClient alpha({
            .runtime_directory = temp.path,
            .client_id = "alpha-ui",
            .session_id = "alpha",
        });
        std::string error;
        REQUIRE(alpha.refresh(error));

        RemoteTerminalClient alpha_terminal({
            .runtime_directory = temp.path,
            .client_id = "alpha-ui",
            .session_id = "alpha",
            .expected_server_epoch = "delete-session-1",
            .method_prefix = "terminal",
            .terminal_id
            = std::string(kServerShellTerminalId),
        });
        REQUIRE(alpha_terminal.attach(error));

        for (int attempt = 0;
            attempt < 100
            && !std::filesystem::exists(alpha_checkpoint);
            ++attempt)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
        REQUIRE(std::filesystem::exists(alpha_checkpoint));

        REQUIRE(ServerClient::rename_session(
            temp.path, "alpha", "Renamed Alpha", error));
        const auto renamed_status
            = ServerClient::status(temp.path);
        REQUIRE(renamed_status.ok);
        const auto renamed_session = std::ranges::find(
            renamed_status.status->session_statuses,
            std::string("alpha"),
            &ServerSessionStatusSnapshot::session_id);
        REQUIRE(renamed_session
            != renamed_status.status->session_statuses.end());
        CHECK(renamed_session->session_name
            == "Renamed Alpha");
        for (int attempt = 0; attempt < 100; ++attempt)
        {
            const auto current
                = ServerClient::status(temp.path);
            REQUIRE(current.ok);
            const auto alpha_status = std::ranges::find(
                current.status->session_statuses,
                std::string("alpha"),
                &ServerSessionStatusSnapshot::session_id);
            REQUIRE(alpha_status
                != current.status->session_statuses.end());
            if (alpha_status->checkpoint_state == "ok")
                break;
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
        auto renamed_checkpoint = load_session_state_from_path(
            alpha_checkpoint, &error);
        INFO(error);
        REQUIRE(renamed_checkpoint);
        CHECK(renamed_checkpoint->session_name
            == "Renamed Alpha");

        REQUIRE_FALSE(ServerClient::delete_session(
            temp.path, "alpha",
            { .confirm_live_terminals = true }, error));
        CHECK(error.find("still attached")
            != std::string::npos);

        REQUIRE(ServerClient::disconnect(
            temp.path, "alpha-ui", error));
        REQUIRE_FALSE(ServerClient::delete_session(
            temp.path, "alpha", {}, error));
        CHECK(error.find("Retry with --yes")
            != std::string::npos);

        REQUIRE(ServerClient::delete_session(
            temp.path, "alpha",
            { .confirm_live_terminals = true }, error));
        CHECK(error.empty());
        CHECK_FALSE(std::filesystem::exists(alpha_checkpoint));

        const auto status = ServerClient::status(temp.path);
        REQUIRE(status.ok);
        CHECK(status.status->sessions == 1);
        CHECK(std::ranges::none_of(
            status.status->session_statuses,
            [](const auto& session) {
                return session.session_id == "alpha";
            }));

        REQUIRE_FALSE(ServerClient::delete_session(
            temp.path, "alpha",
            { .confirm_live_terminals = true }, error));
        CHECK(error.find("does not exist")
            != std::string::npos);
        run_guard.join();
    }

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .build_version = "unit-test",
            .epoch_override = "delete-session-2",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard run_guard(server);
        const auto status = ServerClient::status(temp.path);
        REQUIRE(status.ok);
        CHECK(status.status->sessions == 1);
        CHECK(status.status->session_statuses.front()
                  .session_id
            == "default");
        run_guard.join();
    }
}

TEST_CASE("server deletes all detached Sessions and stops their terminals",
    "[server][topology][persistence][sessions][delete-all]")
{
    TempDir temp("draxul-server-delete-all-sessions");
    const auto alpha_checkpoint
        = server_session_state_path(temp.path, "alpha");
    const auto beta_checkpoint
        = server_session_state_path(temp.path, "beta");

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .session_checkpoint_interval
            = std::chrono::milliseconds(20),
            .build_version = "unit-test",
            .epoch_override = "delete-all-sessions-1",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard run_guard(server);

        std::string error;
        TopologyClient alpha({
            .runtime_directory = temp.path,
            .client_id = "alpha-ui",
            .session_id = "alpha",
        });
        TopologyClient beta({
            .runtime_directory = temp.path,
            .client_id = "beta-ui",
            .session_id = "beta",
        });
        REQUIRE(alpha.refresh(error));
        REQUIRE(beta.refresh(error));

        RemoteTerminalClient alpha_terminal({
            .runtime_directory = temp.path,
            .client_id = "alpha-ui",
            .session_id = "alpha",
            .expected_server_epoch
            = "delete-all-sessions-1",
            .method_prefix = "terminal",
            .terminal_id
            = std::string(kServerShellTerminalId),
        });
        RemoteTerminalClient beta_terminal({
            .runtime_directory = temp.path,
            .client_id = "beta-ui",
            .session_id = "beta",
            .expected_server_epoch
            = "delete-all-sessions-1",
            .method_prefix = "terminal",
            .terminal_id
            = std::string(kServerShellTerminalId),
        });
        REQUIRE(alpha_terminal.attach(error));
        REQUIRE(beta_terminal.attach(error));
        REQUIRE(ServerClient::rename_session(
            temp.path, "alpha", "Alpha", error));
        REQUIRE(ServerClient::rename_session(
            temp.path, "beta", "Beta", error));
        REQUIRE(std::filesystem::exists(alpha_checkpoint));
        REQUIRE(std::filesystem::exists(beta_checkpoint));

        REQUIRE_FALSE(ServerClient::delete_all_sessions(
            temp.path,
            { .confirm_live_terminals = true }, error));
        CHECK(error.find("still attached")
            != std::string::npos);

        REQUIRE(ServerClient::disconnect(
            temp.path, "alpha-ui", error));
        REQUIRE(ServerClient::disconnect(
            temp.path, "beta-ui", error));
        REQUIRE_FALSE(ServerClient::delete_all_sessions(
            temp.path, {}, error));
        CHECK(error.find("--yes") != std::string::npos);

        REQUIRE(ServerClient::delete_all_sessions(
            temp.path,
            { .confirm_live_terminals = true }, error));
        CHECK(error.empty());
        CHECK_FALSE(std::filesystem::exists(alpha_checkpoint));
        CHECK_FALSE(std::filesystem::exists(beta_checkpoint));

        const auto status = ServerClient::status(temp.path);
        REQUIRE(status.ok);
        CHECK(status.status->sessions == 0);
        CHECK(status.status->terminals == 0);
        CHECK(status.status->session_statuses.empty());
        run_guard.join();
    }

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .build_version = "unit-test",
            .epoch_override = "delete-all-sessions-2",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard run_guard(server);
        const auto status = ServerClient::status(temp.path);
        REQUIRE(status.ok);
        CHECK(status.status->sessions == 1);
        REQUIRE(status.status->session_statuses.size() == 1);
        CHECK(status.status->session_statuses.front()
                  .session_id
            == "default");
        run_guard.join();
    }
}

TEST_CASE("server topology checkpoints and cold-restores stable terminal descriptors",
    "[server][topology][persistence]")
{
    TempDir temp("draxul-server-persistence");
    const auto checkpoint = server_session_state_path(temp.path);
    std::string dynamic_terminal_id;
    std::string dynamic_pane_id;
    std::string restored_dynamic_pane_id;
    uint64_t original_process_id = 0;

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .epoch_override = "persistence-first",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard run_guard(server);
        TopologyClient client({
            .runtime_directory = temp.path,
            .client_id = "persistence-writer",
        });
        std::string error;
        REQUIRE(client.refresh(error));

        TopologyCommand create_space{
            .command_id = "persist-space",
            .expected_revision = client.snapshot().revision,
            .kind = TopologyCommandKind::CreateSpace,
            .name = "Restored Space",
            .root_directory = "D:/restored",
        };
        TopologyCommandResult created;
        REQUIRE(client.execute(create_space, created, error));

        const auto& first_space = client.snapshot().spaces.front();
        const auto& first_tab = first_space.tabs.front();
        TopologyCommand split{
            .command_id = "persist-terminal",
            .expected_revision = client.snapshot().revision,
            .kind = TopologyCommandKind::SplitPane,
            .space_id = first_space.space_id,
            .tab_id = first_tab.tab_id,
            .pane_id = first_tab.panes.front().pane_id,
            .name = "Persistent Shell",
            .direction = TopologySplitDirection::Horizontal,
            .pane_domain = TopologyPaneDomain::ServerTerminal,
        };
        TopologyCommandResult split_result;
        REQUIRE(client.execute(split, split_result, error));
        const TopologyPane& dynamic
            = split_result.snapshot.spaces.front()
                  .tabs.front()
                  .panes.back();
        dynamic_terminal_id = dynamic.terminal_id;
        dynamic_pane_id = dynamic.pane_id;

        RemoteTerminalClient terminal({
            .runtime_directory = temp.path,
            .client_id = "persistence-terminal-first",
            .expected_server_epoch = "persistence-first",
            .method_prefix = "terminal",
            .terminal_id = dynamic_terminal_id,
        });
        REQUIRE(terminal.attach(error));
        original_process_id = terminal.projection().pane().process_id;
        REQUIRE(original_process_id != 0);
        run_guard.join();
    }

    REQUIRE(std::filesystem::exists(checkpoint));
    std::string load_error;
    auto saved = load_session_state_from_path(
        checkpoint, &load_error);
    INFO(load_error);
    REQUIRE(saved);
    REQUIRE(saved->spaces.size() == 2);
    REQUIRE(saved->spaces.back().name == "Restored Space");
    REQUIRE_FALSE(saved->spaces.front()
            .tabs.front()
            .name_user_set);
    auto& saved_panes
        = saved->spaces.front().tabs.front().pane_layout.panes;
    const auto saved_dynamic = std::ranges::find(
        saved_panes, dynamic_pane_id,
        &SessionPaneSnapshot::pane_id);
    REQUIRE(saved_dynamic != saved_panes.end());
    saved_dynamic->agent = AgentIdentity{
        .profile_id = "codex",
        .kind = "codex",
        .display_name = "Codex",
        .instance_id = "persisted-agent",
    };
    saved_dynamic->agent_session = AgentSessionRef{
        .source = "draxul:codex",
        .agent_kind = "codex",
        .integration_version = 1,
        .sequence = 1,
        .kind = AgentSessionRefKind::Id,
        .value = "persisted-session",
    };
    saved_dynamic->restore_policy
        = AgentRestorePolicy::ShellOnly;
    REQUIRE(save_session_state_to_path(
        *saved, checkpoint, &load_error));
    const auto checkpoint_time
        = std::filesystem::last_write_time(checkpoint);

    {
        ServerKernel server({
            .runtime_directory = temp.path,
            .epoch_override = "persistence-second",
        });
        REQUIRE(server.start().disposition
            == ServerStartDisposition::Started);
        // Wrapped in extra parens so Catch2 does not decompose the expression:
        // file_time_type has a __int128 duration rep that it cannot stringify.
        REQUIRE((std::filesystem::last_write_time(checkpoint)
            == checkpoint_time));
        ServerRunGuard run_guard(server);
        TopologyClient client({
            .runtime_directory = temp.path,
            .client_id = "persistence-reader",
        });
        std::string error;
        REQUIRE(client.refresh(error));
        REQUIRE(client.snapshot().spaces.size() == 2);
        REQUIRE(client.snapshot().spaces.back().name
            == "Restored Space");
        REQUIRE(client.snapshot().spaces.back().root_directory
            == "D:/restored");
        REQUIRE_FALSE(client.snapshot().spaces.front().tabs.front().name_user_set);
        REQUIRE(client.snapshot().spaces.back().tabs.front().panes.front().domain
            == TopologyPaneDomain::ClientLocal);
        REQUIRE(client.snapshot().spaces.back().tabs.front().panes.front().client_host_kind
            == "platform_default");

        const auto& restored_panes
            = client.snapshot().spaces.front().tabs.front().panes;
        const auto dynamic = std::ranges::find(
            restored_panes, dynamic_terminal_id,
            &TopologyPane::terminal_id);
        REQUIRE(dynamic != restored_panes.end());
        restored_dynamic_pane_id = dynamic->pane_id;
        REQUIRE_FALSE(restored_dynamic_pane_id.empty());
        REQUIRE(restored_dynamic_pane_id
            != dynamic_pane_id);
        REQUIRE(dynamic->agent);
        REQUIRE(dynamic->agent->instance_id
            == "persisted-agent");
        REQUIRE(dynamic->agent_session);
        REQUIRE(dynamic->agent_session->value
            == "persisted-session");
        REQUIRE(dynamic->restore_policy
            == AgentRestorePolicy::ShellOnly);

        RemoteTerminalClient terminal({
            .runtime_directory = temp.path,
            .client_id = "persistence-terminal-second",
            .expected_server_epoch = "persistence-second",
            .method_prefix = "terminal",
            .terminal_id = dynamic_terminal_id,
        });
        REQUIRE(terminal.attach(error));
        REQUIRE(terminal.projection().pane().process_id != 0);
        REQUIRE(terminal.projection().pane().process_id
            != original_process_id);
        REQUIRE(terminal.projection().version().generation == 1);
        REQUIRE(terminal.disconnect(error));

        const auto report
            = [&](std::string_view epoch,
                  uint64_t generation,
                  uint64_t sequence) {
                  return ControlClient::request(
                      namespaced_control_id(
                          kServerControlId, temp.path),
                      temp.path,
                      "pane.report_agent_session",
                      {
                          { "session_id", "default" },
                          { "server_epoch", epoch },
                          { "runtime_generation", generation },
                          { "pane_id",
                              restored_dynamic_pane_id },
                          { "agent_instance_id",
                              "persisted-agent" },
                          { "source", "draxul:codex" },
                          { "agent", "codex" },
                          { "integration_version", 1 },
                          { "sequence", sequence },
                          { "ref_kind", "id" },
                          { "ref_value",
                              "persisted-session-2" },
                      });
              };
        const auto old_epoch = report(
            "persistence-first", 1, 2);
        CHECK_FALSE(old_epoch.ok);
        CHECK(old_epoch.error_code == "server_replaced");

        const auto old_runtime = report(
            "persistence-second", 2, 2);
        CHECK_FALSE(old_runtime.ok);
        CHECK(old_runtime.error_code == "agent_replaced");

        const auto reported = report(
            "persistence-second", 1, 2);
        INFO(reported.error_code << ": "
                                 << reported.error_message);
        REQUIRE(reported.ok);
        REQUIRE(reported.result.contains("session_ref"));
        CHECK(reported.result["session_ref"]["value"]
            == "persisted-session-2");
        const auto stale = report(
            "persistence-second", 1, 2);
        CHECK_FALSE(stale.ok);
        CHECK(stale.error_code == "stale_report");

        REQUIRE(client.refresh(error));
        const auto& updated_panes
            = client.snapshot().spaces.front().tabs.front().panes;
        const auto updated = std::ranges::find(
            updated_panes, restored_dynamic_pane_id,
            &TopologyPane::pane_id);
        REQUIRE(updated != updated_panes.end());
        REQUIRE(updated->agent_session);
        CHECK(updated->agent_session->sequence == 2);
        CHECK(updated->agent_session->value
            == "persisted-session-2");
        run_guard.join();
    }

    auto updated = load_session_state_from_path(
        checkpoint, &load_error);
    INFO(load_error);
    REQUIRE(updated);
    const auto& updated_panes
        = updated->spaces.front().tabs.front().pane_layout.panes;
    const auto updated_dynamic = std::ranges::find(
        updated_panes, restored_dynamic_pane_id,
        &SessionPaneSnapshot::pane_id);
    REQUIRE(updated_dynamic != updated_panes.end());
    REQUIRE(updated_dynamic->agent_session);
    CHECK(updated_dynamic->agent_session->sequence == 2);
    CHECK(updated_dynamic->agent_session->value
        == "persisted-session-2");
}

TEST_CASE("server periodically checkpoints topology without a UI",
    "[server][topology][persistence]")
{
    TempDir temp("draxul-server-periodic-persistence");
    ServerKernel server({
        .runtime_directory = temp.path,
        .session_checkpoint_interval = std::chrono::milliseconds(20),
        .epoch_override = "periodic-epoch",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    TopologyClient client({
        .runtime_directory = temp.path,
        .client_id = "periodic-writer",
    });
    std::string error;
    REQUIRE(client.refresh(error));
    TopologyCommand rename{
        .command_id = "periodic-rename",
        .expected_revision = client.snapshot().revision,
        .kind = TopologyCommandKind::RenameSpace,
        .space_id = client.snapshot().spaces.front().space_id,
        .name = "Periodically Saved",
    };
    TopologyCommandResult renamed;
    REQUIRE(client.execute(rename, renamed, error));

    std::optional<ServerStatusSnapshot> checkpoint_status;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const auto status = ServerClient::status(temp.path);
        REQUIRE(status.ok);
        if (status.status->checkpoint_state == "ok")
        {
            checkpoint_status = status.status;
            break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }
    REQUIRE(checkpoint_status);
    REQUIRE(checkpoint_status->last_checkpoint_unix_ms != 0);
    REQUIRE(checkpoint_status->checkpoint_path
        == server_session_state_path(temp.path).string());

    auto saved = load_session_state_from_path(
        server_session_state_path(temp.path), &error);
    INFO(error);
    REQUIRE(saved);
    REQUIRE(saved->spaces.front().name
        == "Periodically Saved");
    run_guard.join();
}

TEST_CASE("server checkpoint writer does not block requests and shutdown captures the final revision",
    "[server][topology][persistence][concurrency]")
{
    TempDir temp("draxul-server-checkpoint-concurrency");
    struct Gate
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool first_started = false;
        bool release_first = false;
        size_t calls = 0;
    };
    auto gate = std::make_shared<Gate>();
    ServerKernel server({
        .runtime_directory = temp.path,
        .session_checkpoint_interval
        = std::chrono::milliseconds(5),
        .epoch_override = "checkpoint-concurrency",
        .checkpoint_shutdown_budget
        = std::chrono::seconds(2),
        .checkpoint_save
        = [gate](const SessionSnapshot& snapshot,
              const std::filesystem::path& path,
              std::string* error) {
              {
                  std::unique_lock lock(gate->mutex);
                  ++gate->calls;
                  if (gate->calls == 1)
                  {
                      gate->first_started = true;
                      gate->changed.notify_all();
                      gate->changed.wait(lock, [&] {
                          return gate->release_first;
                      });
                  }
              }
              return save_session_state_to_path(
                  snapshot, path, error);
          },
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);
    {
        std::unique_lock lock(gate->mutex);
        REQUIRE(gate->changed.wait_for(lock,
            std::chrono::seconds(2), [&] {
                return gate->first_started;
            }));
    }

    const auto status_started
        = std::chrono::steady_clock::now();
    const auto status = ServerClient::status(
        temp.path, std::chrono::milliseconds(500));
    CHECK(std::chrono::steady_clock::now() - status_started
        < std::chrono::milliseconds(250));
    REQUIRE(status.ok);

    TopologyClient client({
        .runtime_directory = temp.path,
        .client_id = "checkpoint-concurrency-ui",
    });
    std::string error;
    REQUIRE(client.refresh(error));
    TopologyCommand rename{
        .command_id = "checkpoint-final-revision",
        .expected_revision = client.snapshot().revision,
        .kind = TopologyCommandKind::RenameSpace,
        .space_id = client.snapshot().spaces.front().space_id,
        .name = "Final Revision",
    };
    TopologyCommandResult renamed;
    REQUIRE(client.execute(rename, renamed, error));
    server.request_stop();
    {
        std::lock_guard lock(gate->mutex);
        gate->release_first = true;
    }
    gate->changed.notify_all();
    run_guard.join();

    CHECK(gate->calls >= 2);
    auto saved = load_session_state_from_path(
        server_session_state_path(temp.path), &error);
    INFO(error);
    REQUIRE(saved);
    CHECK(saved->spaces.front().name
        == "Final Revision");
}

TEST_CASE("server shutdown bounds a stalled checkpoint and the detached task remains safe",
    "[server][topology][persistence][concurrency]")
{
    TempDir temp("draxul-server-checkpoint-shutdown-budget");
    struct Gate
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool started = false;
        bool release = false;
        bool finished = false;
    };
    auto gate = std::make_shared<Gate>();
    ServerKernel server({
        .runtime_directory = temp.path,
        .session_checkpoint_interval
        = std::chrono::milliseconds(5),
        .epoch_override = "checkpoint-budget",
        .checkpoint_shutdown_budget
        = std::chrono::milliseconds(25),
        .checkpoint_save
        = [gate](const SessionSnapshot& snapshot,
              const std::filesystem::path& path,
              std::string* error) {
              {
                  std::unique_lock lock(gate->mutex);
                  gate->started = true;
                  gate->changed.notify_all();
                  gate->changed.wait(lock,
                      [&] { return gate->release; });
              }
              const bool saved = save_session_state_to_path(
                  snapshot, path, error);
              {
                  std::lock_guard lock(gate->mutex);
                  gate->finished = true;
              }
              gate->changed.notify_all();
              return saved;
          },
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);
    {
        std::unique_lock lock(gate->mutex);
        REQUIRE(gate->changed.wait_for(lock,
            std::chrono::seconds(2),
            [&] { return gate->started; }));
    }
    const auto stop_started
        = std::chrono::steady_clock::now();
    run_guard.join();
    CHECK(std::chrono::steady_clock::now() - stop_started
        < std::chrono::milliseconds(250));

    {
        std::lock_guard lock(gate->mutex);
        gate->release = true;
    }
    gate->changed.notify_all();
    {
        std::unique_lock lock(gate->mutex);
        REQUIRE(gate->changed.wait_for(lock,
            std::chrono::seconds(2),
            [&] { return gate->finished; }));
    }
    CHECK(std::filesystem::exists(
        server_session_state_path(temp.path)));
}

TEST_CASE("server reports checkpoint failure and preserves the last good file",
    "[server][topology][persistence]")
{
    TempDir temp("draxul-server-persistence-failure");
    const auto checkpoint = server_session_state_path(temp.path);
    {
        ServerKernel seed({
            .runtime_directory = temp.path,
            .epoch_override = "failure-seed",
        });
        REQUIRE(seed.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard seed_guard(seed);
        seed_guard.join();
    }
    std::ifstream original_file(checkpoint, std::ios::binary);
    REQUIRE(original_file.is_open());
    const std::string original{
        std::istreambuf_iterator<char>(original_file),
        std::istreambuf_iterator<char>()
    };

    ServerKernel server({
        .runtime_directory = temp.path,
        .session_checkpoint_interval = std::chrono::milliseconds(20),
        .epoch_override = "failure-test",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);
    std::filesystem::path blocked = checkpoint;
    blocked += ".tmp";
    REQUIRE(std::filesystem::create_directory(blocked));

    TopologyClient client({
        .runtime_directory = temp.path,
        .client_id = "failure-writer",
    });
    std::string error;
    REQUIRE(client.refresh(error));
    TopologyCommand rename{
        .command_id = "failed-checkpoint-rename",
        .expected_revision = client.snapshot().revision,
        .kind = TopologyCommandKind::RenameSpace,
        .space_id = client.snapshot().spaces.front().space_id,
        .name = "Must Not Replace Last Good",
    };
    TopologyCommandResult renamed;
    REQUIRE(client.execute(rename, renamed, error));

    std::optional<ServerStatusSnapshot> failed_status;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const auto status = ServerClient::status(temp.path);
        REQUIRE(status.ok);
        if (status.status->checkpoint_state == "failed")
        {
            failed_status = status.status;
            break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }
    REQUIRE(failed_status);
    REQUIRE_FALSE(failed_status->checkpoint_error.empty());
    run_guard.join();

    std::ifstream preserved_file(checkpoint, std::ios::binary);
    REQUIRE(preserved_file.is_open());
    const std::string preserved{
        std::istreambuf_iterator<char>(preserved_file),
        std::istreambuf_iterator<char>()
    };
    REQUIRE(preserved == original);
}

TEST_CASE("server archives an unreadable checkpoint and resumes saving",
    "[server][topology][persistence]")
{
    TempDir temp("draxul-server-invalid-persistence");
    const auto checkpoint = server_session_state_path(temp.path);
    REQUIRE(std::filesystem::create_directories(
        checkpoint.parent_path()));
    {
        std::ofstream invalid(checkpoint, std::ios::binary);
        invalid << "{broken";
    }
    ServerKernel server({
        .runtime_directory = temp.path,
        .session_checkpoint_interval = std::chrono::milliseconds(20),
        .epoch_override = "invalid-persistence",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);

    const auto status = ServerClient::status(temp.path);
    REQUIRE(status.ok);
    REQUIRE(status.status->checkpoint_state != "disabled");
    REQUIRE_FALSE(status.status->restore_warnings.empty());
    std::vector<std::filesystem::path> archived;
    for (const auto& entry : std::filesystem::directory_iterator(
             checkpoint.parent_path()))
    {
        if (entry.path().filename().string().starts_with(
                "default.toml.corrupt-"))
        {
            archived.push_back(entry.path());
        }
    }
    REQUIRE(archived.size() == 1);

    TopologyClient client({
        .runtime_directory = temp.path,
        .client_id = "invalid-reader",
    });
    std::string error;
    REQUIRE(client.refresh(error));
    REQUIRE(client.snapshot().spaces.size() == 1);
    std::optional<ServerStatusSnapshot> saved_status;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const auto current = ServerClient::status(temp.path);
        REQUIRE(current.ok);
        if (current.status->checkpoint_state == "ok")
        {
            saved_status = current.status;
            break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }
    REQUIRE(saved_status);
    run_guard.join();

    std::ifstream retained(archived.front(), std::ios::binary);
    REQUIRE(retained.is_open());
    const std::string archived_text{
        std::istreambuf_iterator<char>(retained),
        std::istreambuf_iterator<char>()
    };
    REQUIRE(archived_text == "{broken");
    auto fresh = load_session_state_from_path(checkpoint, &error);
    INFO(error);
    REQUIRE(fresh);
}

TEST_CASE("server restores usable Spaces and checkpoints after partial restore",
    "[server][topology][persistence]")
{
    TempDir temp("draxul-server-partial-persistence");
    const auto checkpoint = server_session_state_path(temp.path);
    {
        ServerKernel seed({
            .runtime_directory = temp.path,
            .epoch_override = "partial-seed",
        });
        REQUIRE(seed.start().disposition
            == ServerStartDisposition::Started);
        ServerRunGuard seed_guard(seed);
        seed_guard.join();
    }
    std::string error;
    auto saved = load_session_state_from_path(
        checkpoint, &error);
    REQUIRE(saved);
    auto encoded = encode_session_state(*saved, &error);
    REQUIRE(encoded);
    auto cloned = decode_session_state(*encoded, &error);
    REQUIRE(cloned);
    SpaceSnapshot broken = std::move(cloned->spaces.front());
    broken.id = saved->next_space_id++;
    broken.name = "Broken Space";
    broken.tabs.front().pane_layout.panes.front().launch.remote_terminal_id.clear();
    saved->spaces.push_back(std::move(broken));
    REQUIRE(save_session_state_to_path(
        *saved, checkpoint, &error));
    ServerKernel server({
        .runtime_directory = temp.path,
        .session_checkpoint_interval = std::chrono::milliseconds(20),
        .epoch_override = "partial-restore",
    });
    REQUIRE(server.start().disposition
        == ServerStartDisposition::Started);
    ServerRunGuard run_guard(server);
    const auto status = ServerClient::status(temp.path);
    REQUIRE(status.ok);
    REQUIRE(status.status->checkpoint_state
        == "restored_with_warnings");
    REQUIRE_FALSE(status.status->restore_warnings.empty());

    TopologyClient client({
        .runtime_directory = temp.path,
        .client_id = "partial-reader",
    });
    REQUIRE(client.refresh(error));
    REQUIRE(client.snapshot().spaces.size() == 1);
    REQUIRE(client.snapshot().spaces.front().name == "Space 1");
    TopologyCommand rename{
        .command_id = "partial-restore-rename",
        .expected_revision = client.snapshot().revision,
        .kind = TopologyCommandKind::RenameSpace,
        .space_id = client.snapshot().spaces.front().space_id,
        .name = "Recovered Space",
    };
    TopologyCommandResult renamed;
    REQUIRE(client.execute(rename, renamed, error));
    bool checkpointed_after_warning = false;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const auto current = ServerClient::status(temp.path);
        REQUIRE(current.ok);
        if (current.status->checkpoint_state == "ok")
        {
            checkpointed_after_warning = true;
            break;
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }
    REQUIRE(checkpointed_after_warning);
    run_guard.join();

    auto recovered = load_session_state_from_path(
        checkpoint, &error);
    INFO(error);
    REQUIRE(recovered);
    REQUIRE(recovered->spaces.size() == 1);
    REQUIRE(recovered->spaces.front().name
        == "Recovered Space");
}
