#include "control_event_journal.h"

#include <draxul/agent_model.h>
#include <unordered_set>

namespace draxul
{

namespace
{

constexpr size_t kMaxControlEvents = 256;

} // namespace

void ControlEventJournal::record(std::string type, nlohmann::json payload)
{
    events_.push_back({
        { "sequence", next_sequence_++ },
        { "type", std::move(type) },
        { "payload", std::move(payload) },
    });
    while (events_.size() > kMaxControlEvents)
        events_.pop_front();
}

void ControlEventJournal::observe(
    SpaceId active_space_id, const std::vector<AgentProjection>& agents)
{
    if (observed_once_ && active_space_id != observed_active_space_)
        record("space.focused", { { "space_id", active_space_id } });
    observed_active_space_ = active_space_id;
    observed_once_ = true;

    std::unordered_set<std::string> live;
    for (const auto& agent : agents)
    {
        live.insert(agent.identity.instance_id);
        const AgentState current{
            .generation = agent.generation,
            .lifecycle = agent.lifecycle,
            .status = agent.status,
            .focused = agent.focused,
        };
        const auto previous = agent_states_.find(agent.identity.instance_id);
        if (previous == agent_states_.end())
        {
            record("agent.added", {
                { "instance_id", agent.identity.instance_id },
                { "runtime_generation", agent.generation.value },
                { "lifecycle", to_string(agent.lifecycle) },
                { "status", to_string(agent.status) },
            });
        }
        else if (previous->second != current)
        {
            record("agent.changed", {
                { "instance_id", agent.identity.instance_id },
                { "runtime_generation", agent.generation.value },
                { "lifecycle", to_string(agent.lifecycle) },
                { "status", to_string(agent.status) },
                { "focused", agent.focused },
            });
        }
        agent_states_[agent.identity.instance_id] = current;
    }

    for (auto it = agent_states_.begin(); it != agent_states_.end();)
    {
        if (live.contains(it->first))
        {
            ++it;
            continue;
        }
        record("agent.removed", { { "instance_id", it->first } });
        it = agent_states_.erase(it);
    }
}

nlohmann::json ControlEventJournal::read_after(uint64_t cursor, size_t limit) const
{
    const uint64_t oldest =
        events_.empty() ? next_sequence_ : events_.front().value("sequence", 0ull);
    const bool cursor_expired = cursor != 0 && cursor + 1 < oldest;
    nlohmann::json selected = nlohmann::json::array();
    if (!cursor_expired)
    {
        for (const auto& event : events_)
        {
            if (event.value("sequence", 0ull) <= cursor)
                continue;
            selected.push_back(event);
            if (selected.size() == limit)
                break;
        }
    }
    uint64_t next_cursor = cursor;
    if (!selected.empty())
        next_cursor = selected.back().value("sequence", cursor);
    else if (cursor == 0 && !events_.empty())
        next_cursor = events_.back().value("sequence", 0ull);

    return {
        { "events", std::move(selected) },
        { "next_cursor", next_cursor },
        { "cursor_expired", cursor_expired },
        { "disconnect_reason", cursor_expired ? "cursor_expired" : "" },
    };
}

} // namespace draxul
