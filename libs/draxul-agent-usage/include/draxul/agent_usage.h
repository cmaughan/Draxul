#pragma once

// Per-agent token activity from native Codex/Claude session files. The parsing,
// rate window and rolloff follow TokenFu (src/local_usage.cpp,
// src/claude_usage.cpp, App::observe_usage_refresh) but are keyed by one
// agent's session instead of aggregated per provider.

#include <draxul/agent_model.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace draxul
{

using UsageTimestamp = std::chrono::sys_time<std::chrono::milliseconds>;

struct SessionTokenDelta
{
    UsageTimestamp timestamp{};
    uint64_t tokens = 0;
};

// Incremental JSONL reader for one native session file. Feed it appended
// bytes in order; complete lines emit token deltas. Partial trailing lines are
// buffered (bounded) until the rest arrives.
class AgentSessionTail
{
public:
    explicit AgentSessionTail(std::string_view kind);

    void consume(std::string_view bytes, std::vector<SessionTokenDelta>& deltas);

    uint64_t session_tokens() const noexcept { return session_tokens_; }
    // From Codex session_meta or Claude record fields; empty until seen.
    const std::string& session_id() const noexcept { return session_id_; }
    const std::string& working_directory() const noexcept { return working_directory_; }

private:
    struct CodexTotals
    {
        uint64_t input = 0;
        uint64_t output = 0;
        uint64_t cached = 0;
        uint64_t reasoning = 0;
        bool operator==(const CodexTotals&) const = default;
    };

    void process_line(std::string_view line, std::vector<SessionTokenDelta>& deltas);
    void process_codex(std::string_view line, std::vector<SessionTokenDelta>& deltas);
    void process_claude(std::string_view line, std::vector<SessionTokenDelta>& deltas);
    void emit(UsageTimestamp timestamp, uint64_t tokens, std::vector<SessionTokenDelta>& deltas);

    bool codex_ = false;
    std::string pending_;
    bool discarding_oversized_line_ = false;
    uint64_t session_tokens_ = 0;
    std::string session_id_;
    std::string working_directory_;
    // Codex repeats cumulative totals; remembered totals suppress duplicates.
    std::optional<CodexTotals> previous_total_;
    std::deque<CodexTotals> seen_totals_;
    // Claude repeats one message's usage across streamed lines; only growth
    // beyond the largest published value for that message id counts.
    std::unordered_map<std::string, uint64_t> published_message_tokens_;
    std::deque<std::string> published_message_order_;
};

// TokenFu's rolling rate: tokens over the last ten intervals between
// timestamped measurements.
class TokenRateWindow
{
public:
    // Measurements must be fed in timestamp order. Returns true when the
    // measured rate or its timestamp changed.
    bool observe(UsageTimestamp timestamp, uint64_t tokens);

    double tokens_per_second() const noexcept { return tokens_per_second_; }
    std::optional<UsageTimestamp> measured_at() const noexcept { return measured_at_; }

private:
    struct Interval
    {
        uint64_t tokens = 0;
        double seconds = 0.0;
    };

    std::optional<UsageTimestamp> previous_;
    std::deque<Interval> intervals_;
    double tokens_per_second_ = 0.0;
    std::optional<UsageTimestamp> measured_at_;
};

struct AgentUsageRequest
{
    std::string instance_id;
    std::string kind;
    std::optional<AgentSessionRef> session_ref;
    // Agent process (or managed launch) directory; locates sessions when no
    // native session reference was reported.
    std::string working_directory;
    UsageTimestamp started_at{};
};

struct AgentUsageRoots
{
    std::filesystem::path claude_projects;
    std::filesystem::path codex_sessions;

    // CLAUDE_CONFIG_DIR/projects (default ~/.claude/projects) and
    // CODEX_HOME/sessions (default ~/.codex/sessions).
    static AgentUsageRoots from_environment();
};

struct AgentUsageMonitorOptions
{
    // Use native directory notifications. When false (tests) every update
    // re-examines bound files, which is still cheap: only bound files are read.
    bool native_watch = true;
    // Safety net for missed notifications while agents are bound.
    std::chrono::seconds reconcile_interval{ 30 };
    // Per-file read budget per update so binding a large existing session
    // cannot stall the caller; the rest is read on following updates.
    size_t read_budget_bytes = 4 * 1024 * 1024;
};

// Claude's project directory name for a working directory: every character
// that is not an ASCII letter or digit becomes '-'.
std::string claude_project_directory_name(std::string_view working_directory);

// Owns session resolution, file tailing and rate state for the agents passed to
// update(). Designed for the server's agent refresh cadence (about 1 Hz): file
// IO happens only after a native change notification, while a newly bound file
// is being caught up, or on the slow reconcile interval. With no agents it
// drops every watcher and file state.
class AgentUsageMonitor
{
public:
    explicit AgentUsageMonitor(AgentUsageRoots roots, AgentUsageMonitorOptions options = {});
    ~AgentUsageMonitor();
    AgentUsageMonitor(const AgentUsageMonitor&) = delete;
    AgentUsageMonitor& operator=(const AgentUsageMonitor&) = delete;

    // Returns activity for each request bound to a session file. Agents whose
    // session cannot be attributed unambiguously are absent.
    std::unordered_map<std::string, AgentActivity> update(
        const std::vector<AgentUsageRequest>& requests, UsageTimestamp now);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace draxul
