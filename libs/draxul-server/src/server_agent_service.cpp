#include <draxul/server_agent_service.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace draxul
{

namespace
{

constexpr int kRemovalProbeCount = 6;
constexpr auto kDiscoveredStartupGrace = std::chrono::seconds(3);
constexpr auto kOutputDebounce = std::chrono::milliseconds(100);

} // namespace

ServerAgentService::ServerAgentService(std::string session_id)
    : session_id_(std::move(session_id))
{
    snapshot_.revision = 1;
    snapshot_.session_id = session_id_;
}

void ServerAgentService::update(
    const std::vector<ServerAgentRuntimeView>& runtimes,
    std::chrono::steady_clock::time_point now)
{
    std::vector<ServerAgentProjection> agents;
    std::unordered_set<std::string> live_terminals;
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

        const AgentIdentity* identity = nullptr;
        std::string identity_evidence = "managed_launch";
        bool identity_high_confidence = true;
        if (runtime.declared_identity)
        {
            identity = &*runtime.declared_identity;
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
                    std::ostringstream instance;
                    instance << "server-discovered-"
                             << runtime.terminal_id << '-'
                             << runtime.generation.value << '-'
                             << next_instance_serial_++;
                    state.discovered_identity = AgentIdentity{
                        .kind = discovered->kind,
                        .display_name = discovered->display_name,
                        .instance_id = instance.str(),
                        .origin
                        = AgentIdentityOrigin::Discovered,
                    };
                    state.detected_at = now;
                    state.explanation = {};
                    state.attention = false;
                    state.last_status = AgentStatus::Unknown;
                }
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
            .session_ref = runtime.session_ref,
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
    }
    std::erase_if(runtime_states_,
        [&live_terminals](const auto& entry) {
            return !live_terminals.contains(entry.first);
        });
    if (agents != snapshot_.agents)
    {
        snapshot_.agents = std::move(agents);
        ++snapshot_.revision;
    }
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
    return ControlMethodResult::error(
        "unknown_method", "Unknown server agent method.");
}

} // namespace draxul
