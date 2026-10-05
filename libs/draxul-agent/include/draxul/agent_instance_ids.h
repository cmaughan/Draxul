#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>

namespace draxul
{

// One allocator per Session authority, shared by managed and discovered agents.
// Reserve restored identities before allocating. Issued names are not recycled
// during this allocator's lifetime, so a stale name cannot target a new agent.
class AgentInstanceIds
{
public:
    AgentInstanceIds();
    void reserve(std::string_view identity);
    std::string next();

private:
    uint64_t offset_;
    uint64_t serial_ = 0;
    std::unordered_set<std::string> issued_;
};

} // namespace draxul
