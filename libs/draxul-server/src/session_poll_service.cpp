#include "session_poll_service.h"

#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>

namespace draxul
{

namespace
{

size_t encoded_size(const nlohmann::json& value)
{
    return value.dump(-1, ' ', false,
        nlohmann::detail::error_handler_t::replace)
        .size();
}

enum class PollChannel
{
    Terminals,
    Topology,
    Agents,
};

constexpr std::array<PollChannel, 4> kWeightedChannels{
    PollChannel::Terminals,
    PollChannel::Topology,
    PollChannel::Terminals,
    PollChannel::Agents,
};

} // namespace

SessionPollService::SessionPollService(std::string server_epoch)
    : server_epoch_(std::move(server_epoch))
{
}

ControlMethodResult SessionPollService::handle(
    const nlohmann::json& params,
    std::string_view authenticated_client_id,
    const TopologySnapshot& topology,
    const ServerAgentSnapshot& agents,
    std::span<const SessionPollTerminalView> terminals,
    size_t payload_budget)
{
    if (authenticated_client_id.empty())
    {
        return ControlMethodResult::error(
            "invalid_client",
            "Session polling requires an authenticated client identity.");
    }
    std::string parse_error;
    auto request = session_poll_request_from_json(params, parse_error);
    if (!request)
    {
        return ControlMethodResult::error(
            "invalid_session_poll", std::move(parse_error));
    }
    if (request->server_epoch != server_epoch_)
    {
        return ControlMethodResult::error(
            "stale_epoch", "The Session server epoch has changed.");
    }

    SessionPollResponse response{
        .request_serial = request->request_serial,
        .server_epoch = server_epoch_,
        .topology = {
            .revision = topology.revision,
            .resync
            = request->topology_after_revision > topology.revision,
        },
        .agents = {
            .revision = agents.revision,
            .resync
            = request->agent_after_revision > agents.revision,
        },
    };
    const size_t total_budget = std::clamp<size_t>(
        payload_budget, 1, kSessionPollPayloadBudget);
    size_t remaining = total_budget;
    auto& schedule = schedules_[std::string(authenticated_client_id)];

    const auto process_topology = [&] {
        if (request->topology_after_revision == topology.revision)
            return;
        const auto encoded = topology_snapshot_to_json(topology);
        const size_t bytes = encoded_size(encoded);
        if (bytes > total_budget)
        {
            response.topology.error_code = "frame_too_large";
            response.topology.error_message
                = "The topology snapshot exceeds the Session poll budget.";
            return;
        }
        if (bytes > remaining)
        {
            response.topology.deferred = true;
            response.more = true;
            return;
        }
        response.topology.snapshot = topology;
        remaining -= bytes;
    };

    const auto process_agents = [&] {
        if (request->agent_after_revision == agents.revision)
            return;
        const auto encoded = server_agent_snapshot_to_json(agents);
        const size_t bytes = encoded_size(encoded);
        if (bytes > total_budget)
        {
            response.agents.error_code = "frame_too_large";
            response.agents.error_message
                = "The agent snapshot exceeds the Session poll budget.";
            return;
        }
        if (bytes > remaining)
        {
            response.agents.deferred = true;
            response.more = true;
            return;
        }
        response.agents.snapshot = agents;
        remaining -= bytes;
    };

    const auto process_terminals = [&] {
        if (request->terminals.empty())
            return;
        std::vector<const SessionTerminalSubscription*> ordered;
        ordered.reserve(request->terminals.size());
        for (const auto& subscription : request->terminals)
            ordered.push_back(&subscription);
        std::ranges::sort(ordered, {},
            &SessionTerminalSubscription::subscription_id);
        size_t start = 0;
        if (schedule.next_subscription_id != 0)
        {
            const auto found = std::ranges::lower_bound(ordered,
                schedule.next_subscription_id, {},
                &SessionTerminalSubscription::subscription_id);
            if (found != ordered.end())
                start = static_cast<size_t>(found - ordered.begin());
        }
        std::unordered_map<std::string_view, RemoteTerminalService*>
            lookup;
        lookup.reserve(terminals.size());
        for (const auto& terminal : terminals)
            lookup.emplace(terminal.terminal_id, terminal.service);

        size_t considered = 0;
        for (; considered < ordered.size(); ++considered)
        {
            const size_t index = (start + considered) % ordered.size();
            const auto& subscription = *ordered[index];
            SessionTerminalPollBatch batch{
                .subscription_id = subscription.subscription_id,
                .terminal_id = subscription.terminal_id,
                .visibility_generation
                = subscription.visibility_generation,
            };
            const auto service = lookup.find(subscription.terminal_id);
            if (service == lookup.end() || !service->second)
            {
                batch.error_code = "terminal_not_found";
                batch.error_message
                    = "The requested server terminal does not exist.";
                response.terminals.push_back(std::move(batch));
                continue;
            }
            const size_t soft_budget = std::min(
                remaining, kSessionPollTerminalQuantum);
            auto slice = service->second->poll_subscription(
                authenticated_client_id, subscription,
                soft_budget, remaining,
                kSessionPollTerminalEventLimit);
            batch.attach = std::move(slice.attach);
            batch.events = std::move(slice.events);
            batch.suspended = slice.suspended;
            batch.resync = slice.resync;
            batch.more = slice.more;
            batch.error_code = std::move(slice.error_code);
            batch.error_message = std::move(slice.error_message);
            remaining -= std::min(remaining, slice.payload_bytes);
            if (batch.attach || !batch.events.empty()
                || batch.suspended || batch.more
                || !batch.error_code.empty())
            {
                response.terminals.push_back(std::move(batch));
            }
            if (slice.more)
                response.more = true;
            if (slice.oversized || remaining == 0)
            {
                ++considered;
                response.more = response.more
                    || considered < ordered.size();
                break;
            }
        }
        const size_t next = (start
            + std::max<size_t>(1, considered))
            % ordered.size();
        schedule.next_subscription_id
            = ordered[next]->subscription_id;
    };

    std::array<bool, 3> processed{};
    const size_t weighted_start
        = schedule.next_channel++ % kWeightedChannels.size();
    for (size_t offset = 0; offset < kWeightedChannels.size(); ++offset)
    {
        const PollChannel channel = kWeightedChannels[
            (weighted_start + offset) % kWeightedChannels.size()];
        const size_t slot = static_cast<size_t>(channel);
        if (processed[slot])
            continue;
        processed[slot] = true;
        switch (channel)
        {
        case PollChannel::Terminals:
            process_terminals();
            break;
        case PollChannel::Topology:
            process_topology();
            break;
        case PollChannel::Agents:
            process_agents();
            break;
        }
    }

    auto encoded = session_poll_response_to_json(response);
    if (encoded_size(encoded) > kControlMaxMessageBytes)
    {
        return ControlMethodResult::error(
            "frame_too_large",
            "The Session poll response exceeds the control frame budget.");
    }
    return ControlMethodResult::success(std::move(encoded));
}

void SessionPollService::disconnect_client(std::string_view client_id)
{
    schedules_.erase(std::string(client_id));
}

} // namespace draxul
