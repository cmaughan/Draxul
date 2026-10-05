#include "server_agent_service.h"

#include <draxul/agent_usage.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <utility>

namespace draxul
{

namespace
{

constexpr int kRemovalProbeCount = 6;
constexpr auto kDiscoveredStartupGrace = std::chrono::seconds(3);
constexpr auto kOutputDebounce = std::chrono::milliseconds(100);
// A hook can report before the next process probe discovers its agent.
constexpr auto kPendingSessionRefLifetime = std::chrono::seconds(30);

nlohmann::json route_json(
    const ServerAgentProjection& agent)
{
    return {
        { "space_id", agent.space_id },
        { "tab_id", agent.tab_id },
        { "pane_id", agent.pane_id },
        { "terminal_id", agent.terminal_id },
    };
}

nlohmann::json explanation_json(
    const ServerAgentProjection& agent)
{
    return {
        { "status", to_string(agent.status) },
        { "authority", to_string(agent.status_authority) },
        { "manifest_id", agent.manifest_id },
        { "manifest_version", agent.manifest_version },
        { "rule_id", agent.rule_id },
        { "evidence_category",
            agent.status_evidence_category },
        { "fallback_reason", agent.fallback_reason },
        { "observation_generation",
            agent.observation_generation },
    };
}

nlohmann::json agent_json(
    const ServerAgentProjection& agent)
{
    nlohmann::json result{
        { "instance_id", agent.identity.instance_id },
        { "profile_id", agent.identity.profile_id },
        { "kind", agent.identity.kind },
        { "display_name", agent.identity.display_name },
        { "origin", to_string(agent.identity.origin) },
        { "identity_evidence_category",
            agent.identity_evidence_category },
        { "identity_high_confidence",
            agent.identity_high_confidence },
        { "lifecycle", to_string(agent.lifecycle) },
        { "runtime_generation", agent.generation.value },
        { "status", to_string(agent.status) },
        { "status_authority",
            to_string(agent.status_authority) },
        { "attention", agent.attention },
        { "running", agent.running },
        { "route", route_json(agent) },
        { "explanation", explanation_json(agent) },
    };
    if (agent.exit_code)
        result["exit_code"] = *agent.exit_code;
    if (agent.session_ref)
    {
        result["session_ref"] = {
            { "source", agent.session_ref->source },
            { "agent_kind", agent.session_ref->agent_kind },
            { "integration_version",
                agent.session_ref->integration_version },
            { "sequence", agent.session_ref->sequence },
            { "kind", to_string(agent.session_ref->kind) },
            { "value", agent.session_ref->value },
        };
    }
    if (agent.activity)
    {
        result["activity"] = {
            { "tokens_per_second", agent.activity->tokens_per_second },
            { "measured_at_ms", agent.activity->measured_at_ms },
            { "session_tokens", agent.activity->session_tokens },
        };
    }
    return result;
}

const ServerAgentProjection* find_agent(
    const ServerAgentSnapshot& snapshot,
    std::string_view instance_id)
{
    const auto found = std::ranges::find_if(
        snapshot.agents,
        [instance_id](const ServerAgentProjection& agent) {
            return agent.identity.instance_id == instance_id;
        });
    return found == snapshot.agents.end()
        ? nullptr
        : &*found;
}

} // namespace

ServerAgentService::ServerAgentService(std::string session_id)
    : ServerAgentService(std::move(session_id), nullptr)
{
}

ServerAgentService::ServerAgentService(std::string session_id,
    std::unique_ptr<AgentUsageMonitor> usage_monitor)
    : session_id_(std::move(session_id))
    , usage_monitor_(std::move(usage_monitor))
{
    snapshot_.revision = 1;
    snapshot_.session_id = session_id_;
}

ServerAgentService::~ServerAgentService() = default;

void ServerAgentService::update(
    const std::vector<ServerAgentRuntimeView>& runtimes,
    std::chrono::steady_clock::time_point now)
{
    std::vector<ServerAgentProjection> agents;
    std::vector<AgentUsageRequest> usage_requests;
    const auto system_now = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now());
    std::unordered_set<std::string> live_terminals;
    // Reserve all managed identities before discovery, regardless of pane order.
    for (const auto& runtime : runtimes)
        if (runtime.declared_identity)
            reserve_instance_id(runtime.declared_identity->instance_id);
    for (const auto& runtime : runtimes)
    {
        if (runtime.terminal_id.empty())
            continue;
        live_terminals.insert(runtime.terminal_id);
        RuntimeState& state = runtime_states_[runtime.terminal_id];
        if (state.generation != runtime.generation)
        {
            state = {};
            state.generation = runtime.generation;
        }
        if (state.first_seen_at == std::chrono::steady_clock::time_point{})
            state.first_seen_at = now;

        const AgentIdentity* identity = nullptr;
        std::string identity_evidence = "managed_launch";
        bool identity_high_confidence = true;
        if (runtime.declared_identity)
        {
            identity = &*runtime.declared_identity;
            state.working_directory = runtime.launch_working_directory;
            state.discovered_identity.reset();
            state.process_present = false;
            state.failed_probes = 0;
        }
        else
        {
            const auto discovered = runtime.process_observation
                ? discover_agent_process(
                      *runtime.process_observation)
                : std::nullopt;
            if (discovered)
            {
                state.failed_probes = 0;
                state.process_present = true;
                const bool changed
                    = !state.discovered_identity
                    || state.discovered_identity->kind
                        != discovered->kind;
                if (changed)
                {
                    state.discovered_identity = AgentIdentity{
                        .kind = discovered->kind,
                        .display_name = discovered->display_name,
                        .instance_id = allocate_instance_id(),
                        .origin
                        = AgentIdentityOrigin::Discovered,
                    };
                    state.detected_at = now;
                    state.first_seen_at = now;
                    state.session_ref.reset();
                    state.explanation = {};
                    state.attention = false;
                    state.last_status = AgentStatus::Unknown;
                }
                if (!discovered->working_directory.empty())
                    state.working_directory = discovered->working_directory;
                state.identity_evidence_category
                    = discovered->evidence_category;
                state.identity_high_confidence
                    = discovered->high_confidence;
            }
            else if (state.discovered_identity)
            {
                state.process_present = false;
                if (++state.failed_probes >= kRemovalProbeCount)
                {
                    state = {};
                    state.generation = runtime.generation;
                }
            }
            if (state.discovered_identity)
            {
                const auto pending = pending_session_refs_.find(runtime.terminal_id);
                if (pending != pending_session_refs_.end()
                    && pending->second.generation == runtime.generation
                    && pending->second.session_ref.agent_kind == state.discovered_identity->kind)
                {
                    if (!state.session_ref
                        || pending->second.session_ref.sequence > state.session_ref->sequence)
                        state.session_ref = pending->second.session_ref;
                    pending_session_refs_.erase(pending);
                }
            }
            identity = state.discovered_identity
                ? &*state.discovered_identity
                : nullptr;
            identity_evidence
                = state.identity_evidence_category;
            identity_high_confidence
                = state.identity_high_confidence;
        }

        if (!identity)
            continue;
        const bool discovered
            = identity->origin == AgentIdentityOrigin::Discovered;
        const std::optional<AgentSessionRef>& session_ref
            = discovered ? state.session_ref : runtime.session_ref;
        const bool running = discovered
            ? runtime.runtime_running && state.process_present
            : runtime.runtime_running;
        const AgentLifecycle lifecycle = running
            ? AgentLifecycle::Running
            : (runtime.exit_code && *runtime.exit_code != 0
                      ? AgentLifecycle::Failed
                      : AgentLifecycle::Exited);

        const bool startup_grace_elapsed
            = !discovered
            || state.detected_at
                == std::chrono::steady_clock::time_point{}
            || now - state.detected_at
                >= kDiscoveredStartupGrace;
        if (runtime.terminal_observation
            && startup_grace_elapsed)
        {
            const AgentObservation& observation
                = *runtime.terminal_observation;
            const auto captured_at = observation.captured_at
                    == std::chrono::steady_clock::time_point{}
                ? now
                : observation.captured_at;
            const bool stable = !observation.last_output_at
                || !observation.process_running
                || captured_at - *observation.last_output_at
                    >= kOutputDebounce;
            const bool changed
                = state.explanation.evaluated_at
                    == std::chrono::steady_clock::time_point{}
                || observation.output_generation
                    > state.explanation.observation_generation;
            if (stable && changed)
            {
                auto explanation = evaluate_agent_observation(
                    identity->kind, observation);
                if (explanation.status != state.last_status)
                {
                    state.last_status = explanation.status;
                    if (explanation.status
                            == AgentStatus::Blocked
                        || explanation.status
                            == AgentStatus::Done)
                    {
                        state.attention = true;
                    }
                }
                state.explanation = std::move(explanation);
            }
        }

        agents.push_back({
            .space_id = runtime.space_id,
            .tab_id = runtime.tab_id,
            .pane_id = runtime.pane_id,
            .terminal_id = runtime.terminal_id,
            .identity = *identity,
            .identity_evidence_category
            = std::move(identity_evidence),
            .identity_high_confidence
            = identity_high_confidence,
            .session_ref = session_ref,
            .lifecycle = lifecycle,
            .generation = runtime.generation,
            .exit_code = runtime.exit_code,
            .status = state.explanation.status,
            .status_authority
            = state.explanation.authority,
            .manifest_id = state.explanation.manifest_id,
            .manifest_version
            = state.explanation.manifest_version,
            .rule_id = state.explanation.rule_id,
            .status_evidence_category
            = state.explanation.evidence_category,
            .fallback_reason
            = state.explanation.fallback_reason,
            .observation_generation
            = state.explanation.observation_generation,
            .attention = state.attention,
            .running = running,
        });
        if (usage_monitor_ && running)
        {
            usage_requests.push_back({
                .instance_id = identity->instance_id,
                .kind = identity->kind,
                .session_ref = session_ref,
                .working_directory = state.working_directory,
                .started_at = system_now
                    - std::chrono::duration_cast<std::chrono::milliseconds>(now - state.first_seen_at),
            });
        }
    }
    if (usage_monitor_)
    {
        const auto activity = usage_monitor_->update(usage_requests, system_now);
        for (ServerAgentProjection& agent : agents)
        {
            if (const auto found = activity.find(agent.identity.instance_id); found != activity.end())
                agent.activity = found->second;
        }
    }
    std::erase_if(runtime_states_,
        [&live_terminals](const auto& entry) {
            return !live_terminals.contains(entry.first);
        });
    std::erase_if(pending_session_refs_,
        [&](const auto& entry) {
            return !live_terminals.contains(entry.first)
                || now - entry.second.reported_at > kPendingSessionRefLifetime;
        });
    if (agents != snapshot_.agents)
    {
        snapshot_.agents = std::move(agents);
        ++snapshot_.revision;
    }
}

ControlMethodResult ServerAgentService::report_discovered_session(
    std::string_view terminal_id, AgentRuntimeGeneration generation,
    const AgentSessionRef& session_ref, std::chrono::steady_clock::time_point now)
{
    std::string validation_error;
    if (!validate_agent_session_ref(session_ref, &validation_error))
    {
        return ControlMethodResult::error(
            "invalid_session_ref", std::move(validation_error));
    }
    const auto state = runtime_states_.find(std::string(terminal_id));
    if (state != runtime_states_.end() && state->second.generation == generation
        && state->second.discovered_identity
        && state->second.discovered_identity->kind == session_ref.agent_kind)
    {
        if (state->second.session_ref
            && session_ref.sequence <= state->second.session_ref->sequence)
        {
            return ControlMethodResult::error(
                "stale_report", "Native session report is stale or was rejected.");
        }
        state->second.session_ref = session_ref;
        pending_session_refs_.erase(std::string(terminal_id));
        return ControlMethodResult::success({
            { "agent_instance_id", state->second.discovered_identity->instance_id },
            { "pending", false },
        });
    }
    auto& pending = pending_session_refs_[std::string(terminal_id)];
    if (pending.reported_at != std::chrono::steady_clock::time_point{}
        && pending.generation == generation
        && session_ref.sequence <= pending.session_ref.sequence)
    {
        return ControlMethodResult::error(
            "stale_report", "Native session report is stale or was rejected.");
    }
    pending = { generation, session_ref, now };
    return ControlMethodResult::success({ { "pending", true } });
}

ControlMethodResult ServerAgentService::handle(
    std::string_view method, const nlohmann::json& params) const
{
    if (method == "agent.snapshot")
    {
        return ControlMethodResult::success(
            server_agent_snapshot_to_json(snapshot_));
    }
    if (method == "agent.poll")
    {
        if (!params.is_object()
            || !params.contains("after_revision")
            || !params["after_revision"].is_number_unsigned())
        {
            return ControlMethodResult::error(
                "invalid_agent_poll",
                "Agent poll requires an unsigned revision.");
        }
        const uint64_t revision
            = params["after_revision"].get<uint64_t>();
        if (revision > snapshot_.revision)
        {
            return ControlMethodResult::error(
                "stale_agent_revision",
                "Client agent revision is ahead of the server.");
        }
        const bool changed = revision != snapshot_.revision;
        nlohmann::json result{
            { "changed", changed },
            { "revision", snapshot_.revision },
        };
        if (changed)
        {
            result["snapshot"]
                = server_agent_snapshot_to_json(snapshot_);
        }
        return ControlMethodResult::success(std::move(result));
    }
    if (method == "agent.list")
    {
        nlohmann::json result = nlohmann::json::array();
        for (const auto& agent : snapshot_.agents)
            result.push_back(agent_json(agent));
        return ControlMethodResult::success(std::move(result));
    }
    if (method == "agent.get"
        || method == "agent.explain"
        || method == "agent.wait")
    {
        if (!params.is_object()
            || !params.contains("instance_id")
            || !params["instance_id"].is_string()
            || params["instance_id"]
                   .get_ref<const std::string&>()
                   .empty())
        {
            return ControlMethodResult::error(
                "invalid_params",
                std::string(method)
                    + " requires a non-empty 'instance_id'.");
        }
        const ServerAgentProjection* agent = find_agent(
            snapshot_,
            params["instance_id"].get_ref<
                const std::string&>());
        if (!agent)
        {
            return ControlMethodResult::error(
                "not_found", "Agent not found.");
        }
        if (method == "agent.get")
            return ControlMethodResult::success(agent_json(*agent));
        if (method == "agent.explain")
        {
            return ControlMethodResult::success({
                { "instance_id",
                    agent->identity.instance_id },
                { "route", route_json(*agent) },
                { "identity_explanation",
                    {
                        { "origin",
                            to_string(
                                agent->identity.origin) },
                        { "evidence_category",
                            agent
                                ->identity_evidence_category },
                        { "high_confidence",
                            agent
                                ->identity_high_confidence },
                    } },
                { "explanation", explanation_json(*agent) },
            });
        }

        if (params.contains("runtime_generation"))
        {
            const auto& generation_value
                = params["runtime_generation"];
            uint64_t requested_generation = 0;
            if (generation_value.is_number_unsigned())
            {
                requested_generation
                    = generation_value.get<uint64_t>();
            }
            else if (generation_value.is_number_integer()
                && generation_value.get<int64_t>() >= 0)
            {
                requested_generation = static_cast<uint64_t>(
                    generation_value.get<int64_t>());
            }
            else
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "'runtime_generation' must be a non-negative integer.");
            }
            if (requested_generation != agent->generation.value)
            {
                return ControlMethodResult::success({
                    { "complete", true },
                    { "outcome", "agent_replaced" },
                    { "agent", agent_json(*agent) },
                });
            }
        }
        std::vector<std::string> desired;
        if (params.contains("until"))
        {
            if (!params["until"].is_array()
                || params["until"].size()
                    > kServerAgentMaxWaitStates)
            {
                return ControlMethodResult::error(
                    "invalid_params",
                    "'until' must be an array of at most 16 states.");
            }
            for (const auto& value : params["until"])
            {
                if (!value.is_string()
                    || value.get_ref<const std::string&>().size()
                        > kServerAgentMaxWaitStateBytes)
                {
                    return ControlMethodResult::error(
                        "invalid_params",
                        "'until' values must be strings of at most 64 bytes.");
                }
                desired.push_back(value.get<std::string>());
            }
        }
        if (desired.empty())
        {
            desired = {
                "blocked", "done", "exited", "failed"
            };
        }
        const auto matches
            = [&desired](std::string_view value) {
                  return std::ranges::find(desired, value)
                      != desired.end();
              };
        const std::string lifecycle(
            to_string(agent->lifecycle));
        const std::string status(to_string(agent->status));
        const bool complete
            = matches(lifecycle) || matches(status);
        return ControlMethodResult::success({
            { "complete", complete },
            { "outcome",
                complete
                    ? (matches(status) ? status : lifecycle)
                    : "" },
            { "agent", agent_json(*agent) },
        });
    }
    return ControlMethodResult::error(
        "unknown_method", "Unknown server agent method.");
}

} // namespace draxul
