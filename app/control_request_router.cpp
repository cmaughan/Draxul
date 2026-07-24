#include "control_request_router.h"

#include "agent_controller.h"
#include "space_controller.h"

#include <algorithm>
#include <chrono>
#include <draxul/agent_model.h>
#include <draxul/host.h>

namespace draxul
{

namespace
{

ControlMethodResult invalid_params(std::string message)
{
    return ControlMethodResult::error("invalid_params", std::move(message));
}

std::optional<int> integer_param(const nlohmann::json& params, const char* name)
{
    if (!params.is_object() || !params.contains(name) || !params[name].is_number_integer())
        return std::nullopt;
    return params[name].get<int>();
}

std::optional<std::string> string_param(const nlohmann::json& params, const char* name)
{
    if (!params.is_object() || !params.contains(name) || !params[name].is_string())
        return std::nullopt;
    return params[name].get<std::string>();
}

int64_t age_ms(std::chrono::steady_clock::time_point value)
{
    if (value == std::chrono::steady_clock::time_point{})
        return 0;
    return std::max<int64_t>(0,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - value)
            .count());
}

nlohmann::json space_json(const Space& space, SpaceId active_space_id)
{
    int pane_count = 0;
    for (const auto& tab : space.tab_controller.tabs())
    {
        if (tab)
            pane_count += tab->pane_manager.host_count();
    }
    return {
        { "id", space.id },
        { "name", space.name },
        { "root_directory", space.root_directory.string() },
        { "active", space.id == active_space_id },
        { "tab_count", space.tab_controller.count() },
        { "pane_count", pane_count },
    };
}

nlohmann::json route_json(const AgentProjection& agent)
{
    return {
        { "space_id", agent.space_id },
        { "tab_id", agent.tab_id },
        { "leaf_id", agent.leaf_id },
        { "pane_id", agent.pane_id },
    };
}

nlohmann::json explanation_json(const AgentStatusExplanation& explanation)
{
    return {
        { "status", to_string(explanation.status) },
        { "authority", to_string(explanation.authority) },
        { "manifest_id", explanation.manifest_id },
        { "manifest_version", explanation.manifest_version },
        { "rule_id", explanation.rule_id },
        { "evidence_category", explanation.evidence_category },
        { "fallback_reason", explanation.fallback_reason },
        { "observation_generation", explanation.observation_generation },
        { "evaluated_age_ms", age_ms(explanation.evaluated_at) },
    };
}

nlohmann::json agent_json(const AgentProjection& agent)
{
    nlohmann::json result{
        { "instance_id", agent.identity.instance_id },
        { "kind", agent.identity.kind },
        { "display_name", agent.identity.display_name },
        { "route", route_json(agent) },
        { "lifecycle", to_string(agent.lifecycle) },
        { "runtime_generation", agent.generation.value },
        { "runtime_age_ms", age_ms(agent.runtime_started_at) },
        { "lifecycle_age_ms", age_ms(agent.lifecycle_transition_at) },
        { "status", to_string(agent.status) },
        { "status_authority", to_string(agent.status_authority) },
        { "status_age_ms", age_ms(agent.last_status_transition_at) },
        { "attention", agent.attention },
        { "running", agent.running },
        { "focused", agent.focused },
    };
    if (agent.exit_code)
        result["exit_code"] = *agent.exit_code;
    else
        result["exit_code"] = nullptr;
    if (agent.session_ref)
    {
        result["native_session"] = {
            { "source", agent.session_ref->source },
            { "agent_kind", agent.session_ref->agent_kind },
            { "integration_version", agent.session_ref->integration_version },
            { "kind", to_string(agent.session_ref->kind) },
        };
    }
    else
        result["native_session"] = nullptr;
    return result;
}

const AgentProjection* find_agent(
    const std::vector<AgentProjection>& agents, std::string_view instance_id)
{
    const auto it = std::find_if(agents.begin(), agents.end(),
        [instance_id](const AgentProjection& agent) {
            return agent.identity.instance_id == instance_id;
        });
    return it == agents.end() ? nullptr : &*it;
}

} // namespace

ControlRequestRouter::ControlRequestRouter(
    SpaceController& spaces, AgentController& agents, std::string session_id)
    : spaces_(spaces)
    , agents_(agents)
    , session_id_(std::move(session_id))
{
}

ControlMethodResult ControlRequestRouter::handle(const ControlRequest& request)
{
    if (request.method == "system.hello")
    {
        return ControlMethodResult::success({
            { "protocol_version", kControlProtocolVersion },
            { "session_id", session_id_ },
            { "capabilities",
                { "system.hello", "space.list", "space.get", "agent.list",
                    "agent.get", "agent.explain", "pane.read", "space.focus",
                    "agent.start", "agent.focus", "agent.restart",
                    "agent.send_text", "agent.send_keys", "agent.wait",
                    "event.subscribe", "pane.report_agent_session" } },
        });
    }

    if (request.method == "space.list")
    {
        nlohmann::json result = nlohmann::json::array();
        for (const auto& space : spaces_.spaces())
        {
            if (space)
                result.push_back(space_json(*space, spaces_.active_space_id()));
        }
        return ControlMethodResult::success(std::move(result));
    }

    if (request.method == "space.get")
    {
        const auto id = integer_param(request.params, "id");
        if (!id)
            return invalid_params("space.get requires an integer 'id'.");
        const Space* space = spaces_.find_space(*id);
        if (!space)
            return ControlMethodResult::error("not_found", "Space not found.");
        return ControlMethodResult::success(
            space_json(*space, spaces_.active_space_id()));
    }

    if (request.method == "agent.list")
    {
        nlohmann::json result = nlohmann::json::array();
        for (const auto& agent : agents_.query(spaces_))
            result.push_back(agent_json(agent));
        return ControlMethodResult::success(std::move(result));
    }

    if (request.method == "agent.get" || request.method == "agent.explain")
    {
        const auto instance_id = string_param(request.params, "instance_id");
        if (!instance_id || instance_id->empty())
            return invalid_params(request.method + " requires a non-empty 'instance_id'.");
        const auto agents = agents_.query(spaces_);
        const AgentProjection* agent = find_agent(agents, *instance_id);
        if (!agent)
            return ControlMethodResult::error("not_found", "Agent not found.");
        if (request.method == "agent.get")
            return ControlMethodResult::success(agent_json(*agent));
        return ControlMethodResult::success({
            { "instance_id", agent->identity.instance_id },
            { "route", route_json(*agent) },
            { "explanation", explanation_json(agent->status_explanation) },
        });
    }

    if (request.method == "pane.read")
    {
        const auto pane_id = string_param(request.params, "pane_id");
        if (!pane_id || pane_id->empty())
            return invalid_params("pane.read requires a non-empty 'pane_id'.");
        int max_lines = 50;
        if (request.params.contains("lines"))
        {
            const auto lines = integer_param(request.params, "lines");
            if (!lines || *lines < 1 || *lines > 200)
                return invalid_params("'lines' must be between 1 and 200.");
            max_lines = *lines;
        }

        for (const auto& space : spaces_.spaces())
        {
            if (!space)
                continue;
            for (const auto& tab : space->tab_controller.tabs())
            {
                if (!tab)
                    continue;
                std::optional<ControlMethodResult> found;
                tab->pane_manager.tree().for_each_leaf(
                    [&](LeafId leaf_id, const PaneDescriptor&) {
                        if (found || tab->pane_manager.pane_id(leaf_id) != *pane_id)
                            return;
                        IHost* host = tab->pane_manager.host_for(leaf_id);
                        const auto observation = host
                            ? host->capture_agent_observation(max_lines, 64 * 1024)
                            : std::nullopt;
                        if (!observation)
                        {
                            found = ControlMethodResult::error(
                                "unsupported", "Pane does not expose readable terminal text.");
                            return;
                        }
                        found = ControlMethodResult::success({
                            { "pane_id", *pane_id },
                            { "space_id", space->id },
                            { "tab_id", tab->id },
                            { "leaf_id", leaf_id },
                            { "lines", observation->bottom_rows },
                            { "output_generation", observation->output_generation },
                        });
                    });
                if (found)
                    return std::move(*found);
            }
        }
        return ControlMethodResult::error("not_found", "Pane not found.");
    }

    return ControlMethodResult::error(
        "method_not_found", "Unknown control method: " + request.method);
}

} // namespace draxul
