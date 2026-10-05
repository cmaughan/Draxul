#include <draxul/agent_instance_ids.h>

#include <array>
#include <random>

namespace draxul
{
namespace
{
constexpr auto adjectives = std::to_array<std::string_view>({
    "able", "bold", "brave", "bright", "brisk", "calm", "chill", "clever",
    "cosy", "crisp", "curious", "dapper", "deft", "eager", "fair", "fancy",
    "fast", "fiery", "fit", "fluffy", "fond", "fresh", "friendly", "furry",
    "gentle", "glad", "golden", "grand", "happy", "hardy", "jolly", "keen",
    "kind", "lively", "loyal", "lucky", "merry", "mild", "minty", "neat",
    "nifty", "nimble", "noble", "peppy", "plucky", "proud", "quick", "quiet",
    "ready", "rosy", "shiny", "sleepy", "smart", "snug", "soft", "spry",
    "steady", "sunny", "super", "swift", "tidy", "warm", "wise", "zesty",
});
constexpr auto animals = std::to_array<std::string_view>({
    "ant", "ape", "badger", "bat", "bear", "beaver", "bee", "bison",
    "boar", "cat", "clam", "cobra", "cod", "crab", "crane", "crow",
    "deer", "dog", "dove", "duck", "eagle", "elk", "emu", "falcon",
    "fawn", "finch", "fox", "frog", "gecko", "goat", "goose", "gull",
    "hare", "hawk", "heron", "horse", "ibis", "ibex", "jay", "koala",
    "lark", "lemur", "lion", "llama", "lynx", "mole", "moose", "moth",
    "mouse", "newt", "otter", "owl", "panda", "puma", "quail", "raven",
    "robin", "seal", "sloth", "swan", "tiger", "whale", "wolf", "wren",
});
static_assert(adjectives.size() == 64 && animals.size() == 64);
constexpr uint64_t pair_count = adjectives.size() * animals.size();
} // namespace

AgentInstanceIds::AgentInstanceIds()
    : offset_(std::random_device{}() % pair_count)
{
}

void AgentInstanceIds::reserve(std::string_view identity)
{
    issued_.emplace(identity);
}

std::string AgentInstanceIds::next()
{
    for (;;)
    {
        const uint64_t serial = serial_++;
        // An odd stride visits every pair once before using numeric suffixes.
        const auto index = (offset_ + (serial % pair_count) * 4051) % pair_count;
        std::string name = std::string(adjectives[index / animals.size()])
            + "-" + std::string(animals[index % animals.size()]);
        if (serial >= pair_count)
            name += "-" + std::to_string(serial / pair_count + 1);
        if (issued_.insert(name).second)
            return name;
    }
}

} // namespace draxul
