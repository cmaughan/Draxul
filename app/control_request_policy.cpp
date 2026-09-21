#include "control_request_policy.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace draxul
{

namespace
{

ControlMethodResult invalid_params(std::string message)
{
    return ControlMethodResult::error("invalid_params", std::move(message));
}

std::optional<std::string> required_nonempty_string(
    const nlohmann::json& params, const char* name)
{
    if (!params.is_object() || !params.contains(name)
        || !params[name].is_string()
        || params[name].get_ref<const std::string&>().empty())
    {
        return std::nullopt;
    }
    return params[name].get<std::string>();
}

} // namespace

ControlRequestPolicy::ControlRequestPolicy(Operations operations)
    : operations_(std::move(operations))
{
}

ControlMethodResult ControlRequestPolicy::read_agent(
    const ControlRequest& request, std::string_view instance_id) const
{
    return operations_.read(
        { request.id, "agent.get", { { "instance_id", instance_id } } });
}

ControlMethodResult ControlRequestPolicy::handle(const ControlRequest& request) const
{
    if (request.method == "pane.focus" || request.method == "pane.action")
    {
        const auto pane_id = required_nonempty_string(request.params, "pane_id");
        if (!pane_id)
        {
            return invalid_params(
                request.method + " requires a non-empty string 'pane_id'.");
        }

        // Preserve the existing error precedence: pane lookup happens before
        // pane.action validates its action argument.
        const auto target = operations_.find_pane(*pane_id);
        if (!target)
        {
            return ControlMethodResult::error(
                "not_found", "The pane is not attached to this Draxul UI.");
        }

        if (request.method == "pane.action")
        {
            const auto action = required_nonempty_string(request.params, "action");
            if (!action)
            {
                return invalid_params(
                    "pane.action requires a non-empty string 'action'.");
            }
            if (!target->has_host)
            {
                return ControlMethodResult::error(
                    "not_running", "The pane has no live host.");
            }
            if (operations_.dispatch_pane_action(*target, *action)
                != ActionResult::Dispatched)
            {
                return ControlMethodResult::error(
                    "action_rejected", "The pane host rejected the action.");
            }
            return ControlMethodResult::success({
                { "pane_id", *pane_id },
                { "action", *action },
                { "dispatched", true },
            });
        }

        if (operations_.focus_pane(*target) != FocusResult::Focused)
        {
            return ControlMethodResult::error(
                "focus_failed", "The pane's Space or tab could not be activated.");
        }
        return ControlMethodResult::success({
            { "pane_id", *pane_id },
            { "space_id", target->space_id },
            { "tab_id", target->tab_id },
            { "active", true },
        });
    }

    if (request.method == "plugin.reload")
    {
        const auto plugin_id = required_nonempty_string(request.params, "plugin_id");
        if (!plugin_id)
        {
            return invalid_params(
                "plugin.reload requires a non-empty string 'plugin_id'.");
        }
        const auto reloaded = operations_.reload_plugin(*plugin_id);
        if (!reloaded.error.empty())
        {
            return ControlMethodResult::error(
                reloaded.rolled_back ? "reload_rolled_back" : "reload_failed",
                reloaded.error);
        }
        return ControlMethodResult::success({
            { "plugin_id", *plugin_id },
            { "generation", reloaded.generation },
            { "matched", reloaded.matched },
            { "reloaded", reloaded.reloaded },
            { "warning", reloaded.warning },
        });
    }

    if (request.method == "pane.report_agent_session")
    {
        const auto pane_id = required_nonempty_string(request.params, "pane_id");
        const auto instance_id
            = required_nonempty_string(request.params, "agent_instance_id");
        const auto source = required_nonempty_string(request.params, "source");
        const auto agent_kind = required_nonempty_string(request.params, "agent");
        const auto ref_kind_text
            = required_nonempty_string(request.params, "ref_kind");
        const auto ref_value
            = required_nonempty_string(request.params, "ref_value");
        if (!pane_id || !instance_id || !source || !agent_kind
            || !ref_kind_text || !ref_value
            || !request.params.contains("integration_version")
            || !request.params["integration_version"].is_number_unsigned()
            || !request.params.contains("sequence")
            || !request.params["sequence"].is_number_unsigned())
        {
            return invalid_params(
                "pane.report_agent_session requires complete routing, source, "
                "version, sequence, and reference fields.");
        }
        const auto ref_kind = parse_agent_session_ref_kind(*ref_kind_text);
        if (!ref_kind)
        {
            return invalid_params("Unknown native session reference kind.");
        }

        // Route identity is intentionally checked before semantic session-ref
        // validation to preserve the endpoint's established error precedence.
        if (operations_.inspect_native_route(*pane_id, *instance_id, *agent_kind)
            != NativeRouteResult::Matched)
        {
            return ControlMethodResult::error(
                "routing_mismatch", "Agent routing identity does not match the pane.");
        }

        AgentSessionRef session_ref{
            .source = *source,
            .agent_kind = *agent_kind,
            .integration_version
            = request.params["integration_version"].get<uint32_t>(),
            .sequence = request.params["sequence"].get<uint64_t>(),
            .kind = *ref_kind,
            .value = *ref_value,
        };
        std::string validation_error;
        if (!validate_agent_session_ref(session_ref, &validation_error))
        {
            return ControlMethodResult::error(
                "invalid_session_ref", std::move(validation_error));
        }

        switch (operations_.accept_native_session(
            *pane_id, *instance_id, std::move(session_ref)))
        {
        case NativeSessionResult::Duplicate:
            return ControlMethodResult::error("duplicate_session_ref",
                "Native agent session is already owned by another pane.");
        case NativeSessionResult::Stale:
            return ControlMethodResult::error("stale_report",
                "Native session report is stale or was rejected.");
        case NativeSessionResult::Accepted:
            return read_agent(request, *instance_id);
        }
    }

    if (request.method == "space.focus")
    {
        if (!request.params.is_object() || !request.params.contains("id")
            || !request.params["id"].is_number_integer())
        {
            return invalid_params("space.focus requires an integer 'id'.");
        }
        const SpaceId id = request.params["id"].get<SpaceId>();
        const auto activated = operations_.focus_space(id);
        if (!activated.ok)
        {
            return ControlMethodResult::error("not_found", activated.error);
        }
        return ControlMethodResult::success({
            { "space_id", id },
            { "active", true },
        });
    }

    if (request.method == "agent.start")
    {
        if (!request.params.is_object()
            || !request.params.contains("profile_id")
            || !request.params["profile_id"].is_string())
        {
            return invalid_params("agent.start requires a string 'profile_id'.");
        }

        // Space activation is an observable mutation and deliberately remains
        // ahead of later args/cwd validation for compatibility.
        if (request.params.contains("space_id"))
        {
            if (!request.params["space_id"].is_number_integer())
                return invalid_params("'space_id' must be an integer.");
            const auto activated = operations_.focus_space(
                request.params["space_id"].get<SpaceId>());
            if (!activated.ok)
                return ControlMethodResult::error("not_found", activated.error);
        }

        AgentLaunchRequest launch{
            .profile_id = request.params["profile_id"].get<std::string>(),
        };
        if (request.params.contains("args"))
        {
            if (!request.params["args"].is_array()
                || request.params["args"].size() > 64)
            {
                return invalid_params(
                    "'args' must be an array of at most 64 strings.");
            }
            for (const auto& arg : request.params["args"])
            {
                if (!arg.is_string()
                    || arg.get_ref<const std::string&>().size() > 4096)
                {
                    return invalid_params(
                        "Every agent argument must be a bounded string.");
                }
                launch.additional_args.push_back(arg.get<std::string>());
            }
        }
        if (request.params.contains("cwd"))
        {
            if (!request.params["cwd"].is_string())
                return invalid_params("'cwd' must be a string.");
            launch.working_directory = request.params["cwd"].get<std::string>();
        }

        auto started = operations_.launch_agent(std::move(launch));
        if (!started.instance_id)
        {
            return ControlMethodResult::error(
                "start_failed", std::move(started.error));
        }
        return read_agent(request, *started.instance_id);
    }

    if (request.method == "agent.focus" || request.method == "agent.restart"
        || request.method == "agent.send_text"
        || request.method == "agent.send_keys"
        || request.method == "agent.wait")
    {
        if (!request.params.is_object()
            || !request.params.contains("instance_id")
            || !request.params["instance_id"].is_string())
        {
            return invalid_params(request.method + " requires 'instance_id'.");
        }
        const std::string instance_id
            = request.params["instance_id"].get<std::string>();
        const auto agent = operations_.find_agent(instance_id);
        if (!agent)
            return ControlMethodResult::error("not_found", "Agent not found.");

        if (request.method == "agent.focus")
        {
            const auto focused = operations_.focus_agent(instance_id);
            if (!focused.ok)
            {
                return ControlMethodResult::error(
                    "focus_failed", "Unable to focus agent.");
            }
            return read_agent(request, instance_id);
        }

        if (request.method == "agent.restart")
        {
            const auto restarted = operations_.restart_agent(*agent);
            if (!restarted.ok)
            {
                return ControlMethodResult::error(
                    "restart_failed", restarted.error);
            }
            return read_agent(request, instance_id);
        }

        // The old App path resolved the pane route before wait arguments and
        // input payloads. Keep that ordering so replaced/no-host responses win
        // over malformed method-specific fields in the same cases.
        const AgentRouteResult route = operations_.inspect_agent_route(*agent);
        if (route == AgentRouteResult::Missing)
        {
            return ControlMethodResult::error(
                "agent_replaced", "The agent pane no longer exists.");
        }

        if (request.method == "agent.wait")
        {
            if (request.params.contains("runtime_generation"))
            {
                if (!request.params["runtime_generation"].is_number_unsigned())
                    return invalid_params("'runtime_generation' must be unsigned.");
                if (request.params["runtime_generation"].get<uint64_t>()
                    != agent->generation.value)
                {
                    return ControlMethodResult::success({
                        { "complete", true },
                        { "outcome", "agent_replaced" },
                        { "agent", read_agent(request, instance_id).value },
                    });
                }
            }

            std::vector<std::string> desired;
            if (request.params.contains("until"))
            {
                if (!request.params["until"].is_array())
                    return invalid_params("'until' must be an array.");
                for (const auto& value : request.params["until"])
                {
                    if (!value.is_string())
                        return invalid_params("'until' values must be strings.");
                    desired.push_back(value.get<std::string>());
                }
            }
            if (desired.empty())
                desired = { "blocked", "done", "exited", "failed" };
            const auto matches = [&](std::string_view value) {
                return std::find(desired.begin(), desired.end(), value)
                    != desired.end();
            };
            const std::string lifecycle(to_string(agent->lifecycle));
            const std::string status(to_string(agent->status));
            const bool complete = matches(lifecycle) || matches(status);
            return ControlMethodResult::success({
                { "complete", complete },
                { "outcome", complete ? (matches(status) ? status : lifecycle) : "" },
                { "agent", read_agent(request, instance_id).value },
            });
        }

        if (route == AgentRouteResult::NoLiveHost)
        {
            return ControlMethodResult::error(
                "not_running", "The agent pane has no live host.");
        }

        std::string bytes;
        if (request.method == "agent.send_text")
        {
            if (!request.params.contains("text")
                || !request.params["text"].is_string())
            {
                return invalid_params(
                    "agent.send_text requires string 'text'.");
            }
            bytes = request.params["text"].get<std::string>();
            if (bytes.size() > 64 * 1024)
                return invalid_params("Agent text exceeds 64 KiB.");
        }
        else
        {
            if (!request.params.contains("keys")
                || !request.params["keys"].is_array()
                || request.params["keys"].size() > 64)
            {
                return invalid_params(
                    "agent.send_keys requires at most 64 keys.");
            }
            std::vector<std::string> keys;
            keys.reserve(request.params["keys"].size());
            for (const auto& value : request.params["keys"])
            {
                if (!value.is_string())
                    return invalid_params("Every key must be a string.");
                keys.push_back(value.get<std::string>());
            }
            std::string key_error;
            auto encoded = encode_agent_keys(keys, key_error);
            if (!encoded)
                return invalid_params(std::move(key_error));
            bytes = std::move(*encoded);
        }

        if (!operations_.send_agent_input(*agent, bytes))
        {
            return ControlMethodResult::error(
                "input_failed", "The agent host rejected input.");
        }
        return read_agent(request, instance_id);
    }

    if (request.method == "event.subscribe")
    {
        uint64_t cursor = 0;
        size_t limit = 64;
        if (request.params.contains("cursor"))
        {
            if (!request.params["cursor"].is_number_unsigned())
                return invalid_params("'cursor' must be unsigned.");
            cursor = request.params["cursor"].get<uint64_t>();
        }
        if (request.params.contains("limit"))
        {
            if (!request.params["limit"].is_number_unsigned())
                return invalid_params("'limit' must be unsigned.");
            limit = std::clamp<size_t>(
                request.params["limit"].get<size_t>(), 1, 128);
        }
        return ControlMethodResult::success(
            operations_.read_events(cursor, limit));
    }

    return operations_.read(request);
}

} // namespace draxul
