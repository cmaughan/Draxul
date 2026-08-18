#include "server_kernel_impl.h"

#include <draxul/server_protocol.h>

#include <algorithm>
#include <unordered_set>

namespace draxul
{

namespace
{

constexpr size_t kCompletedAgentMutationLimit = 1024;

bool is_session_scoped_method(std::string_view method)
{
    return method.starts_with("terminal.")
        || method.starts_with("session.")
        || method.starts_with("topology.")
        || method.starts_with("agent.")
        || method.starts_with("pane.");
}

const std::vector<std::string>& server_capabilities()
{
    static const std::vector<std::string> capabilities{
        "client-registration",
        "controller-lease",
        "agent-control-v1",
        "agent-projection-v1",
        std::string(kServerClientTokenCapability),
        "fake-remote-terminal",
        "graceful-shutdown",
        "managed-agent-v1",
        "managed-agent-v2",
        "multi-terminal-v1",
        "named-sessions-v1",
        "ordered-terminal-events",
        "real-remote-terminal",
        "session-delete-v1",
        "session-persistence-v1",
        "session-poll-v1",
        "session-stream-v1",
        "session-rename-v1",
        "status",
        "terminal-metrics-v1",
        "terminal-presentation-suspend-v1",
        "terminal-scrollback-v1",
        "terminal-uncompressed-v1",
        "topology-v1",
        "topology-control-v2",
        "client-plugin-pane-v1",
    };
    return capabilities;
}

std::vector<std::string> negotiate_capabilities(
    const std::vector<std::string>& requested)
{
    const std::unordered_set<std::string> supported(
        server_capabilities().begin(), server_capabilities().end());
    std::vector<std::string> result;
    for (const std::string& capability : requested)
    {
        if (supported.contains(capability))
            result.push_back(capability);
    }
    return result;
}

} // namespace

ControlMethodResult ServerKernel::Impl::poll_session(
    std::string_view session_id, std::string_view client_id,
    const SessionPollRequest& request, size_t payload_budget)
{
    {
        std::lock_guard guard(mutex);
        const auto client = clients.find(std::string(client_id));
        if (client == clients.end())
        {
            return ControlMethodResult::error(
                "invalid_client",
                "The Session stream client is no longer registered.");
        }
        client->second.last_activity = std::chrono::steady_clock::now();
    }
    const auto found = sessions.find(std::string(session_id));
    if (found == sessions.end() || !found->second->topology_service
        || !found->second->agent_service || !found->second->poll_service)
    {
        return ControlMethodResult::error(
            "session_unavailable",
            "Server Session projections are unavailable.");
    }
    ServerSession& session = *found->second;
    std::vector<SessionPollTerminalView> terminals;
    terminals.reserve(session.terminals.size());
    for (auto& [terminal_id, endpoint] : session.terminals)
    {
        terminals.push_back({
            .terminal_id = terminal_id,
            .service = endpoint.service.get(),
        });
    }
    nlohmann::json params = session_poll_request_to_json(request);
    params["session_id"] = session.session_id;
    return session.poll_service->handle(params, client_id,
        session.topology_service->snapshot(),
        session.agent_service->snapshot(), terminals,
        payload_budget);
}

ControlMethodResult ServerKernel::Impl::handle_request(
    const ControlRequest& request)
{
    if (request.method == "server.hello")
    {
        std::string parse_error;
        auto hello = server_hello_from_json(request.params, parse_error);
        if (!hello)
            return ControlMethodResult::error("invalid_hello", std::move(parse_error));
        if (hello->protocol_major != options.protocol_major)
        {
            return ControlMethodResult::error("incompatible_protocol",
                "Client/server protocol major versions do not match.");
        }

        const bool token_capable = std::ranges::find(
                                       hello->capabilities, kServerClientTokenCapability)
            != hello->capabilities.end();
        std::string connection_token;
        const ClientAccessResult registration
            = register_client_hello(
                *hello, token_capable, connection_token);
        if (registration == ClientAccessResult::LimitReached)
        {
            return ControlMethodResult::error(
                "client_limit_reached",
                "The Draxul server client limit has been reached.");
        }
        if (registration == ClientAccessResult::InvalidToken)
        {
            return ControlMethodResult::error(
                "invalid_connection_token",
                "The client identity is already bound to another connection.");
        }
        ServerWelcome welcome{
            .protocol_major = options.protocol_major,
            .protocol_minor = std::min(
                options.protocol_minor, hello->protocol_minor),
            .server_pid = pid,
            .server_epoch = epoch_value,
            .build_version = options.build_version,
            .connection_token = std::move(connection_token),
            .capabilities = negotiate_capabilities(hello->capabilities),
        };
        return ControlMethodResult::success(server_welcome_to_json(welcome));
    }
    std::string request_client_id;
    if (request.params.is_object())
    {
        const auto client_id = request.params.find("client_id");
        if (client_id != request.params.end())
        {
            if (!client_id->is_string()
                || !valid_server_client_id(
                    client_id->get_ref<const std::string&>()))
            {
                return ControlMethodResult::error(
                    "invalid_client",
                    "A valid client_id is required.");
            }
            request_client_id
                = client_id->get<std::string>();
            std::string connection_token;
            if (const auto token
                = request.params.find("connection_token");
                token != request.params.end())
            {
                if (!token->is_string()
                    || token->get_ref<const std::string&>().size()
                        > kServerMaxConnectionTokenBytes)
                {
                    return ControlMethodResult::error(
                        "invalid_connection_token",
                        "A valid connection token is required.");
                }
                connection_token = token->get<std::string>();
            }
            const ClientAccessResult access
                = authenticate_or_touch_client(
                    request_client_id, connection_token);
            if (access == ClientAccessResult::LimitReached)
            {
                return ControlMethodResult::error(
                    "client_limit_reached",
                    "The Draxul server client limit has been reached.");
            }
            if (access == ClientAccessResult::InvalidToken)
            {
                return ControlMethodResult::error(
                    "invalid_connection_token",
                    "The connection token does not match this client identity.");
            }
        }
    }
    if (request.method == "server.goodbye")
    {
        if (request_client_id.empty())
        {
            return ControlMethodResult::error(
                "invalid_client",
                "A valid client_id is required.");
        }
        disconnect_client(request_client_id);
        return ControlMethodResult::success({
            { "disconnected", true },
        });
    }
    if (!request_client_id.empty()
        && is_session_scoped_method(request.method))
    {
        std::string session_id;
        std::string session_error;
        if (read_session_id(
                request.params, session_id, session_error))
        {
            remember_client_session(
                request_client_id, session_id);
        }
    }
    if (request.method == "server.status")
        return ControlMethodResult::success(server_status_to_json(status_snapshot()));
    if (request.method == "server.delete_session")
        return delete_session(request.params);
    if (request.method == "server.delete_all_sessions")
        return delete_all_sessions(request.params);
    if (request.method == "server.rename_session")
        return rename_session(request.params);
    if (request.method.starts_with("fake."))
    {
        if (!fake_terminal_service)
        {
            return ControlMethodResult::error(
                "terminal_unavailable",
                "The fake remote terminal is unavailable.");
        }
        return fake_terminal_service->handle(
            request.method, request.params);
    }
    if (request.method.starts_with("terminal."))
    {
        ControlMethodResult failure;
        ServerSession* session = resolve_session(request.params,
            SessionServiceNeed::None, {}, failure);
        if (!session)
            return failure;
        std::string terminal_id
            = std::string(kServerShellTerminalId);
        if (request.params.is_object()
            && request.params.contains("terminal_id")
            && request.params["terminal_id"].is_string())
        {
            terminal_id
                = request.params["terminal_id"].get<std::string>();
        }
        const auto terminal = session->terminals.find(terminal_id);
        if (terminal == session->terminals.end())
        {
            return ControlMethodResult::error(
                "terminal_not_found",
                "The requested server terminal does not exist.");
        }
        return terminal->second.service->handle(
            request.method, request.params);
    }
    if (request.method == "session.stream.open")
    {
        if (request_client_id.empty())
        {
            return ControlMethodResult::error(
                "invalid_client",
                "Session streaming requires an authenticated client identity.");
        }
        ControlMethodResult failure;
        ServerSession* session = resolve_session(request.params,
            SessionServiceNeed::TopologyAndAgent,
            "Server Session projections are unavailable.", failure);
        if (!session)
            return failure;
        if (!session_stream)
        {
            return ControlMethodResult::error(
                "session_unavailable",
                "Server Session streaming is unavailable.");
        }
        return session_stream->open(request.params, request_client_id);
    }
    if (request.method == "session.poll")
    {
        if (request_client_id.empty())
        {
            return ControlMethodResult::error(
                "invalid_client",
                "Session polling requires an authenticated client identity.");
        }
        ControlMethodResult failure;
        ServerSession* session = resolve_session(request.params,
            SessionServiceNeed::TopologyAndAgent,
            "Server Session projections are unavailable.", failure);
        if (!session)
            return failure;
        if (!session->poll_service)
        {
            return ControlMethodResult::error(
                "session_unavailable",
                "Server Session polling is unavailable.");
        }
        std::string parse_error;
        auto poll = session_poll_request_from_json(
            request.params, parse_error);
        if (!poll)
            return ControlMethodResult::error("invalid_params", parse_error);
        return poll_session(
            session->session_id, request_client_id, *poll);
    }
    if (request.method == "topology.snapshot"
        || request.method == "topology.poll"
        || request.method == "topology.command"
        || request.method == "topology.layout_apply")
    {
        ControlMethodResult failure;
        ServerSession* session = resolve_session(request.params,
            SessionServiceNeed::Topology,
            "Server Session topology is unavailable.", failure);
        if (!session)
            return failure;
        return session->topology_service->handle(
            request.method, request.params);
    }
    if (request.method == "agent.snapshot"
        || request.method == "agent.poll"
        || request.method == "agent.list"
        || request.method == "agent.get"
        || request.method == "agent.explain"
        || request.method == "agent.wait"
        || request.method == "agent.start"
        || request.method == "agent.restart"
        || request.method == "agent.send_text"
        || request.method == "agent.send_keys")
    {
        ControlMethodResult failure;
        ServerSession* session = resolve_session(request.params,
            SessionServiceNeed::Agent,
            "Server Session agents are unavailable.", failure);
        if (!session)
            return failure;
        const bool mutating_agent_request
            = request.method == "agent.start"
            || request.method == "agent.restart"
            || request.method == "agent.send_text"
            || request.method == "agent.send_keys";
        std::string agent_mutation_key;
        if (mutating_agent_request
            && request.params.contains("request_id"))
        {
            const auto& request_id
                = request.params["request_id"];
            if (request_id.is_number_unsigned())
            {
                const uint64_t value
                    = request_id.get<uint64_t>();
                if (value == 0)
                {
                    return ControlMethodResult::error(
                        "invalid_request_id",
                        "Agent request_id must be non-zero.");
                }
                agent_mutation_key
                    = request.method + ":"
                    + std::to_string(value);
            }
            else if (request_id.is_string())
            {
                const auto& value
                    = request_id.get_ref<const std::string&>();
                if (value.empty() || value.size() > 256)
                {
                    return ControlMethodResult::error(
                        "invalid_request_id",
                        "Agent request_id must be a non-empty bounded string.");
                }
                agent_mutation_key
                    = request.method + ":" + value;
            }
            else
            {
                return ControlMethodResult::error(
                    "invalid_request_id",
                    "Agent request_id must be an unsigned integer or string.");
            }
            const auto cached
                = session->completed_agent_mutations.find(
                    agent_mutation_key);
            if (cached
                != session->completed_agent_mutations.end())
            {
                return cached->second;
            }
        }
        const auto remember_agent_mutation
            = [&](ControlMethodResult result) {
                  if (agent_mutation_key.empty()
                      || !result.ok)
                  {
                      return result;
                  }
                  session->completed_agent_mutation_order
                      .push_back(agent_mutation_key);
                  session->completed_agent_mutations[agent_mutation_key]
                      = result;
                  while (session
                             ->completed_agent_mutation_order
                             .size()
                      > kCompletedAgentMutationLimit)
                  {
                      session->completed_agent_mutations
                          .erase(session
                                  ->completed_agent_mutation_order
                                  .front());
                      session->completed_agent_mutation_order
                          .pop_front();
                  }
                  return result;
              };
        if (request.method == "agent.start")
        {
            if (!request.params.is_object()
                || !request.params.contains("profile_id")
                || !request.params["profile_id"].is_string())
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "agent.start requires a string 'profile_id'.");
            }
            const std::string profile_id
                = request.params["profile_id"]
                      .get<std::string>();
            const AgentDefinition* definition
                = agent_definitions.find(profile_id);
            if (!definition)
            {
                return ControlMethodResult::error(
                    "unknown_profile",
                    "Managed agent profile is unavailable in the server.");
            }

            ManagedAgentTopologyLaunch launch{
                .restore_policy
                = definition->restore_policy,
            };
            if (request.params.contains("replace_pane"))
            {
                if (!request.params["replace_pane"].is_boolean())
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "'replace_pane' must be a boolean.");
                }
                launch.replace_target_pane
                    = request.params["replace_pane"].get<bool>();
            }
            if (request.params.contains("client_id"))
            {
                if (!request.params["client_id"].is_string()
                    || request.params["client_id"]
                        .get_ref<const std::string&>()
                        .empty()
                    || request.params["client_id"]
                            .get_ref<const std::string&>()
                            .size()
                        > 128)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "'client_id' must be a non-empty bounded string.");
                }
                launch.preferred_controller_client_id
                    = request.params["client_id"]
                          .get<std::string>();
            }
            if (request.params.contains("args"))
            {
                if (!request.params["args"].is_array()
                    || request.params["args"].size() > 64)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "'args' must be an array of at most 64 strings.");
                }
                if (!request.params["args"].empty())
                {
                    return ControlMethodResult::error(
                        "unsupported",
                        "Additional arguments for remote managed agents "
                        "are not durable yet; put stable arguments in "
                        "the agent profile.");
                }
                for (const auto& value : request.params["args"])
                {
                    if (!value.is_string()
                        || value
                                .get_ref<const std::string&>()
                                .size()
                            > 4096)
                    {
                        return ControlMethodResult::error(
                            "invalid_params",
                            "Every agent argument must be a bounded string.");
                    }
                    launch.additional_args.push_back(
                        value.get<std::string>());
                }
            }
            if (request.params.contains("cwd"))
            {
                if (!request.params["cwd"].is_string()
                    || request.params["cwd"]
                            .get_ref<const std::string&>()
                            .size()
                        > kTopologyMaxTextBytes)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "'cwd' must be a bounded string.");
                }
                launch.working_directory
                    = request.params["cwd"]
                          .get<std::string>();
            }

            const TopologySnapshot& topology
                = session->topology_service->snapshot();
            if (topology.spaces.empty())
            {
                return ControlMethodResult::error(
                    "topology_unavailable",
                    "Server Session has no Space.");
            }
            const auto read_route_id
                = [&](const char* name,
                      std::string_view prefix)
                -> std::optional<std::string> {
                if (!request.params.contains(name))
                    return std::nullopt;
                const auto& value
                    = request.params[name];
                if (value.is_string()
                    && !value
                        .get_ref<const std::string&>()
                        .empty())
                {
                    return value.get<std::string>();
                }
                if (value.is_number_integer()
                    && value.get<int64_t>() >= 0)
                {
                    return std::string(prefix)
                        + std::to_string(
                            value.get<int64_t>());
                }
                return std::string{};
            };
            auto space_id = read_route_id(
                "space_id", "space-");
            if (space_id && space_id->empty())
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "'space_id' must be a route id.");
            }
            if (!space_id)
                space_id = topology.spaces.front().space_id;
            const auto space = std::ranges::find(
                topology.spaces, *space_id,
                &TopologySpace::space_id);
            if (space == topology.spaces.end()
                || space->tabs.empty())
            {
                return ControlMethodResult::error(
                    "space_not_found",
                    "Topology Space was not found.");
            }

            auto tab_id = read_route_id(
                "tab_id", "tab-");
            if (tab_id && tab_id->empty())
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "'tab_id' must be a route id.");
            }
            if (!tab_id)
                tab_id = space->tabs.front().tab_id;
            const auto tab = std::ranges::find(
                space->tabs, *tab_id,
                &TopologyTab::tab_id);
            if (tab == space->tabs.end()
                || tab->panes.empty())
            {
                return ControlMethodResult::error(
                    "tab_not_found",
                    "Topology tab was not found.");
            }

            auto pane_id = read_route_id(
                "pane_id", "pane-");
            if (pane_id && pane_id->empty())
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "'pane_id' must be a route id.");
            }
            if (!pane_id)
                pane_id = tab->panes.front().pane_id;

            std::string instance_id;
            for (;;)
            {
                instance_id = "server-agent-"
                    + session->session_id + "-"
                    + std::to_string(
                        session->next_agent_serial++);
                bool used = false;
                for (const auto& candidate_space : topology.spaces)
                {
                    for (const auto& candidate_tab : candidate_space.tabs)
                    {
                        used = used
                            || std::ranges::any_of(
                                candidate_tab.panes,
                                [&](const TopologyPane& pane) {
                                    return pane.agent
                                        && pane.agent
                                               ->instance_id
                                        == instance_id;
                                });
                    }
                }
                if (!used)
                    break;
            }
            launch.identity = {
                .profile_id = definition->profile_id,
                .kind = definition->kind,
                .display_name = definition->display_name,
                .instance_id = instance_id,
                .origin = AgentIdentityOrigin::Managed,
            };
            if (launch.working_directory.empty())
            {
                launch.working_directory
                    = space->root_directory;
            }
            auto started
                = session->topology_service->launch_agent(
                    *space_id, *tab_id, *pane_id,
                    definition->display_name, launch);
            if (!started.ok)
                return started;
            refresh_agents(
                *session,
                std::chrono::steady_clock::now());
            return remember_agent_mutation(
                session->agent_service->handle(
                    "agent.get",
                    { { "instance_id", instance_id } }));
        }
        if (request.method == "agent.restart"
            || request.method == "agent.send_text"
            || request.method == "agent.send_keys")
        {
            if (!request.params.is_object()
                || !request.params.contains("instance_id")
                || !request.params["instance_id"].is_string())
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    request.method
                        + " requires 'instance_id'.");
            }
            const std::string instance_id
                = request.params["instance_id"]
                      .get<std::string>();
            const auto& agents
                = session->agent_service->snapshot().agents;
            const auto agent = std::ranges::find_if(
                agents,
                [&instance_id](
                    const ServerAgentProjection& value) {
                    return value.identity.instance_id
                        == instance_id;
                });
            if (agent == agents.end())
            {
                return ControlMethodResult::error(
                    "not_found", "Agent not found.");
            }
            const std::string terminal_id
                = agent->terminal_id;
            const auto terminal
                = session->terminals.find(terminal_id);
            if (terminal == session->terminals.end())
            {
                return ControlMethodResult::error(
                    "agent_replaced",
                    "The agent terminal no longer exists.");
            }
            if (request.method == "agent.restart")
            {
                std::string restart_error;
                if (!terminal->second.service
                        ->restart_runtime(restart_error))
                {
                    return ControlMethodResult::error(
                        "restart_failed",
                        std::move(restart_error));
                }
                refresh_agents(*session,
                    std::chrono::steady_clock::now());
                return remember_agent_mutation(
                    ControlMethodResult::success({
                        { "accepted", true },
                        { "terminal_id", terminal_id },
                        { "runtime_generation",
                            terminal->second.service
                                ->generation() },
                    }));
            }

            std::string bytes;
            if (request.method == "agent.send_text")
            {
                if (!request.params.contains("text")
                    || !request.params["text"].is_string())
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "agent.send_text requires string 'text'.");
                }
                bytes = request.params["text"].get<std::string>();
                if (bytes.size() > 64 * 1024)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "Agent text exceeds 64 KiB.");
                }
            }
            else
            {
                if (!request.params.contains("keys")
                    || !request.params["keys"].is_array()
                    || request.params["keys"].size() > 64)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "agent.send_keys requires at most 64 keys.");
                }
                std::vector<std::string> keys;
                keys.reserve(
                    request.params["keys"].size());
                for (const auto& value : request.params["keys"])
                {
                    if (!value.is_string())
                    {
                        return ControlMethodResult::error(
                            "invalid_params",
                            "Every key must be a string.");
                    }
                    keys.push_back(value.get<std::string>());
                }
                std::string key_error;
                auto encoded
                    = encode_agent_keys(keys, key_error);
                if (!encoded)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        std::move(key_error));
                }
                bytes = std::move(*encoded);
            }
            const RemoteTerminalInputResult input_result
                = terminal->second.runtime->send_input(bytes);
            if (input_result
                != RemoteTerminalInputResult::Accepted)
            {
                return ControlMethodResult::error(
                    input_result
                            == RemoteTerminalInputResult::Backpressure
                        ? "backpressure"
                        : "input_failed",
                    input_result
                            == RemoteTerminalInputResult::Backpressure
                        ? "The agent terminal input queue is full."
                        : "The agent terminal rejected input.");
            }
            return remember_agent_mutation(
                session->agent_service->handle(
                    "agent.get",
                    { { "instance_id", instance_id } }));
        }
        return session->agent_service->handle(
            request.method, request.params);
    }
    if (request.method == "pane.report_agent_session")
    {
        ControlMethodResult failure;
        ServerSession* session = resolve_session(request.params,
            SessionServiceNeed::TopologyAndAgent,
            "Server Session is unavailable.", failure);
        if (!session)
            return failure;
        const auto required_string
            = [&](const char* name)
            -> std::optional<std::string> {
            if (!request.params.is_object()
                || !request.params.contains(name)
                || !request.params[name].is_string()
                || request.params[name]
                    .get_ref<const std::string&>()
                    .empty())
            {
                return std::nullopt;
            }
            return request.params[name].get<std::string>();
        };
        const auto read_unsigned
            = [&](const char* name)
            -> std::optional<uint64_t> {
            if (!request.params.contains(name))
                return std::nullopt;
            const auto& value = request.params[name];
            if (value.is_number_unsigned())
                return value.get<uint64_t>();
            if (value.is_number_integer()
                && value.get<int64_t>() >= 0)
            {
                return static_cast<uint64_t>(
                    value.get<int64_t>());
            }
            return std::nullopt;
        };
        const auto server_epoch
            = required_string("server_epoch");
        const auto pane_id = required_string("pane_id");
        const auto instance_id
            = required_string("agent_instance_id");
        const auto source = required_string("source");
        const auto agent_kind = required_string("agent");
        const auto ref_kind_text
            = required_string("ref_kind");
        const auto ref_value = required_string("ref_value");
        const auto integration_version
            = read_unsigned("integration_version");
        const auto sequence = read_unsigned("sequence");
        const auto runtime_generation
            = read_unsigned("runtime_generation");
        if (!server_epoch || !pane_id || !instance_id
            || !source || !agent_kind || !ref_kind_text
            || !ref_value || !integration_version
            || *integration_version == 0
            || *integration_version > UINT32_MAX
            || !sequence || *sequence == 0
            || !runtime_generation
            || *runtime_generation == 0)
        {
            return ControlMethodResult::error(
                "invalid_params",
                "pane.report_agent_session requires server epoch, "
                "runtime generation, complete routing, source, "
                "version, sequence, and reference fields.");
        }
        if (*server_epoch != epoch_value)
        {
            return ControlMethodResult::error(
                "server_replaced",
                "The session report targets an old server epoch.");
        }
        const auto ref_kind
            = parse_agent_session_ref_kind(*ref_kind_text);
        if (!ref_kind)
        {
            return ControlMethodResult::error(
                "invalid_params",
                "Unknown native session reference kind.");
        }

        refresh_agents(
            *session, std::chrono::steady_clock::now());
        const auto& agents
            = session->agent_service->snapshot().agents;
        const auto agent = std::ranges::find_if(
            agents,
            [&](const ServerAgentProjection& value) {
                return value.identity.instance_id
                    == *instance_id;
            });
        if (agent == agents.end()
            || agent->pane_id != *pane_id
            || agent->identity.kind != *agent_kind)
        {
            return ControlMethodResult::error(
                "routing_mismatch",
                "Agent routing identity does not match the pane.");
        }
        if (agent->generation.value
            != *runtime_generation)
        {
            return ControlMethodResult::error(
                "agent_replaced",
                "The session report targets an old agent runtime generation.");
        }

        AgentSessionRef session_ref{
            .source = *source,
            .agent_kind = *agent_kind,
            .integration_version
            = static_cast<uint32_t>(*integration_version),
            .sequence = *sequence,
            .kind = *ref_kind,
            .value = *ref_value,
        };
        auto reported
            = session->topology_service
                  ->report_agent_session(
                      *pane_id, *instance_id,
                      session_ref);
        if (!reported.ok)
            return reported;
        refresh_agents(
            *session, std::chrono::steady_clock::now());
        return session->agent_service->handle(
            "agent.get",
            { { "instance_id", *instance_id } });
    }
    if (request.method == "pane.read")
    {
        ControlMethodResult failure;
        ServerSession* session = resolve_session(request.params,
            SessionServiceNeed::Topology,
            "Server Session is unavailable.", failure);
        if (!session)
            return failure;
        if (!request.params.contains("pane_id")
            || !request.params["pane_id"].is_string())
        {
            return ControlMethodResult::error(
                "invalid_params",
                "pane.read requires a non-empty 'pane_id'.");
        }
        int max_lines = 50;
        if (request.params.contains("lines"))
        {
            const auto& lines = request.params["lines"];
            if (!lines.is_number_integer())
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "'lines' must be between 1 and 200.");
            }
            const bool valid_lines = lines.is_number_unsigned()
                ? lines.get<uint64_t>() >= 1
                    && lines.get<uint64_t>() <= 200
                : lines.get<int64_t>() >= 1
                    && lines.get<int64_t>() <= 200;
            if (!valid_lines)
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "'lines' must be between 1 and 200.");
            }
            max_lines = lines.is_number_unsigned()
                ? static_cast<int>(lines.get<uint64_t>())
                : static_cast<int>(lines.get<int64_t>());
        }
        if (max_lines < 1 || max_lines > 200)
        {
            return ControlMethodResult::error(
                "invalid_params",
                "'lines' must be between 1 and 200.");
        }
        const std::string pane_id
            = request.params["pane_id"].get<std::string>();
        for (const auto& [terminal_id, endpoint] : session->terminals)
        {
            const TopologySnapshot& topology
                = session->topology_service->snapshot();
            for (const auto& space : topology.spaces)
            {
                for (const auto& tab : space.tabs)
                {
                    const auto pane = std::ranges::find_if(
                        tab.panes,
                        [&](const TopologyPane& value) {
                            return value.pane_id == pane_id
                                && value.terminal_id
                                == terminal_id;
                        });
                    if (pane == tab.panes.end())
                        continue;
                    const auto observation
                        = endpoint.runtime
                              ->capture_agent_observation(
                                  max_lines, 64 * 1024);
                    if (!observation)
                    {
                        return ControlMethodResult::error(
                            "unsupported",
                            "Pane does not expose readable terminal text.");
                    }
                    return ControlMethodResult::success({
                        { "pane_id", pane_id },
                        { "space_id", space.space_id },
                        { "tab_id", tab.tab_id },
                        { "lines",
                            observation->bottom_rows },
                        { "output_generation",
                            observation
                                ->output_generation },
                    });
                }
            }
        }
        return ControlMethodResult::error(
            "not_found", "Pane not found.");
    }
    if (request.method == "server.shutdown")
    {
        if (!request.params.is_object())
        {
            return ControlMethodResult::error(
                "invalid_params",
                "Server shutdown parameters must be an object.");
        }
        const auto confirmation
            = request.params.find("confirm_live_terminals");
        if (confirmation != request.params.end()
            && !confirmation->is_boolean())
        {
            return ControlMethodResult::error(
                "invalid_params",
                "confirm_live_terminals must be a boolean.");
        }
        const bool confirmed
            = confirmation != request.params.end()
            && confirmation->get<bool>();
        size_t live_terminals = 0;
        for (const auto& [session_id, session] : sessions)
        {
            (void)session_id;
            live_terminals += static_cast<size_t>(
                std::ranges::count_if(
                    session->terminals,
                    [](const auto& item) {
                        return item.second.runtime
                            ->is_running();
                    }));
        }
        if (live_terminals > 0 && !confirmed)
        {
            return ControlMethodResult::error(
                "confirmation_required",
                "The Draxul server has "
                    + std::to_string(live_terminals)
                    + " live terminal"
                    + (live_terminals == 1 ? "" : "s")
                    + ". Confirm shutdown to stop them.");
        }
        request_stop();
        return ControlMethodResult::success({
            { "stopping", true },
            { "server_pid", pid },
            { "server_epoch", epoch_value },
        });
    }
    return ControlMethodResult::error(
        "unknown_method", "Unknown Draxul server method.");
}

} // namespace draxul
