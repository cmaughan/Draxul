#include <draxul/agent_usage.h>

#include "usage_time.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace draxul
{
namespace
{

using Json = nlohmann::json;

// A single session record can embed large tool output; anything beyond this is
// not a usage record worth buffering.
constexpr size_t kMaxPendingLineBytes = 16 * 1024 * 1024;
constexpr size_t kSeenCodexTotalsLimit = 64;
constexpr size_t kPublishedClaudeMessageLimit = 512;

uint64_t json_count(const Json& object, const char* key)
{
    const auto value = object.find(key);
    if (value == object.end())
        return 0;
    if (value->is_number_unsigned())
        return value->get<uint64_t>();
    if (value->is_number_integer())
    {
        const auto signed_value = value->get<int64_t>();
        return signed_value > 0 ? static_cast<uint64_t>(signed_value) : 0;
    }
    return 0;
}

std::string json_text(const Json& object, const char* key)
{
    const auto value = object.find(key);
    return value != object.end() && value->is_string() ? value->get<std::string>() : std::string{};
}

std::optional<UsageTimestamp> record_timestamp(const Json& record)
{
    const auto value = record.find("timestamp");
    if (value == record.end() || !value->is_string())
        return std::nullopt;
    return parse_usage_timestamp(value->get_ref<const std::string&>());
}

} // namespace

AgentSessionTail::AgentSessionTail(std::string_view kind)
    : codex_(kind == "codex")
{
}

void AgentSessionTail::consume(std::string_view bytes, std::vector<SessionTokenDelta>& deltas)
{
    while (!bytes.empty())
    {
        const size_t newline = bytes.find('\n');
        if (newline == std::string_view::npos)
        {
            if (!discarding_oversized_line_)
            {
                pending_.append(bytes);
                if (pending_.size() > kMaxPendingLineBytes)
                {
                    pending_.clear();
                    discarding_oversized_line_ = true;
                }
            }
            return;
        }
        const std::string_view head = bytes.substr(0, newline);
        bytes.remove_prefix(newline + 1);
        if (discarding_oversized_line_)
        {
            discarding_oversized_line_ = false;
            continue;
        }
        if (pending_.empty())
        {
            process_line(head, deltas);
        }
        else
        {
            pending_.append(head);
            process_line(pending_, deltas);
            pending_.clear();
        }
    }
}

void AgentSessionTail::process_line(std::string_view line, std::vector<SessionTokenDelta>& deltas)
{
    if (line.ends_with('\r'))
        line.remove_suffix(1);
    if (codex_)
        process_codex(line, deltas);
    else
        process_claude(line, deltas);
}

void AgentSessionTail::emit(UsageTimestamp timestamp, uint64_t tokens, std::vector<SessionTokenDelta>& deltas)
{
    if (tokens == 0)
        return;
    session_tokens_ += tokens;
    deltas.push_back({ timestamp, tokens });
}

void AgentSessionTail::process_codex(std::string_view line, std::vector<SessionTokenDelta>& deltas)
{
    const bool might_be_meta = line.find("session_meta") != std::string_view::npos;
    const bool might_be_tokens = line.find("token_count") != std::string_view::npos;
    if (!might_be_meta && !might_be_tokens)
        return;
    const Json record = Json::parse(line.begin(), line.end(), nullptr, false);
    if (record.is_discarded() || !record.is_object())
        return;
    const auto payload = record.find("payload");
    if (payload == record.end() || !payload->is_object())
        return;
    const std::string type = json_text(record, "type");

    if (type == "session_meta")
    {
        // A forked rollout replays its parent's metadata later; the first
        // session_meta names this file's own session.
        if (session_id_.empty())
        {
            session_id_ = json_text(*payload, "id");
            if (session_id_.empty())
                session_id_ = json_text(*payload, "session_id");
            working_directory_ = json_text(*payload, "cwd");
        }
        return;
    }
    if (type != "event_msg" || json_text(*payload, "type") != "token_count")
        return;
    const auto info = payload->find("info");
    if (info == payload->end() || !info->is_object())
        return;

    const auto read_totals = [](const Json& value) -> std::optional<CodexTotals> {
        if (!value.is_object())
            return std::nullopt;
        return CodexTotals{
            json_count(value, "input_tokens"),
            json_count(value, "output_tokens"),
            std::max(json_count(value, "cached_input_tokens"), json_count(value, "cache_read_input_tokens")),
            json_count(value, "reasoning_output_tokens"),
        };
    };

    std::optional<CodexTotals> total;
    if (const auto value = info->find("total_token_usage"); value != info->end())
        total = read_totals(*value);
    if (total && std::find(seen_totals_.begin(), seen_totals_.end(), *total) != seen_totals_.end())
        return;

    std::optional<CodexTotals> delta;
    if (const auto value = info->find("last_token_usage"); value != info->end())
        delta = read_totals(*value);
    if (!delta && total)
    {
        if (previous_total_ && total->input >= previous_total_->input
            && total->output >= previous_total_->output)
        {
            delta = CodexTotals{ total->input - previous_total_->input,
                total->output - previous_total_->output, 0, 0 };
        }
        else
        {
            delta = total;
        }
    }
    if (total)
    {
        previous_total_ = total;
        seen_totals_.push_back(*total);
        if (seen_totals_.size() > kSeenCodexTotalsLimit)
            seen_totals_.pop_front();
    }
    const auto timestamp = record_timestamp(record);
    if (!delta || !timestamp)
        return;
    // TokenFu's Codex rate counts input (which includes cached input) plus output.
    emit(*timestamp, delta->input + delta->output, deltas);
}

void AgentSessionTail::process_claude(std::string_view line, std::vector<SessionTokenDelta>& deltas)
{
    if (line.find("\"usage\"") == std::string_view::npos
        || line.find("assistant") == std::string_view::npos)
        return;
    const Json record = Json::parse(line.begin(), line.end(), nullptr, false);
    if (record.is_discarded() || !record.is_object() || json_text(record, "type") != "assistant")
        return;
    const auto message = record.find("message");
    if (message == record.end() || !message->is_object())
        return;
    const auto usage = message->find("usage");
    if (usage == message->end() || !usage->is_object())
        return;
    const auto timestamp = record_timestamp(record);
    if (!timestamp)
        return;

    if (session_id_.empty())
        session_id_ = json_text(record, "sessionId");
    if (working_directory_.empty())
        working_directory_ = json_text(record, "cwd");

    const uint64_t tokens = json_count(*usage, "input_tokens")
        + json_count(*usage, "cache_creation_input_tokens")
        + json_count(*usage, "cache_read_input_tokens")
        + json_count(*usage, "output_tokens");
    std::string id = json_text(*message, "id");
    if (id.empty())
        id = json_text(record, "uuid");
    if (id.empty())
    {
        emit(*timestamp, tokens, deltas);
        return;
    }

    // Streaming writes one line per content block, each repeating the
    // message's usage; count only growth over what this message already
    // contributed.
    auto [published, inserted] = published_message_tokens_.try_emplace(id, 0);
    if (inserted)
    {
        published_message_order_.push_back(id);
        if (published_message_order_.size() > kPublishedClaudeMessageLimit)
        {
            published_message_tokens_.erase(published_message_order_.front());
            published_message_order_.pop_front();
        }
    }
    if (tokens <= published->second)
        return;
    const uint64_t delta = tokens - published->second;
    published->second = tokens;
    emit(*timestamp, delta, deltas);
}

bool TokenRateWindow::observe(UsageTimestamp timestamp, uint64_t tokens)
{
    constexpr size_t kIntervalLimit = 10;
    if (tokens == 0)
        return false;
    if (!previous_)
    {
        previous_ = timestamp;
        return false;
    }
    if (timestamp < *previous_)
        return false;
    if (timestamp == *previous_)
    {
        // Same record time as the previous measurement: fold into its interval.
        if (intervals_.empty())
            return false;
        intervals_.back().tokens += tokens;
    }
    else
    {
        const double seconds = std::chrono::duration<double>(timestamp - *previous_).count();
        previous_ = timestamp;
        intervals_.push_back({ tokens, seconds });
        while (intervals_.size() > kIntervalLimit)
            intervals_.pop_front();
    }

    uint64_t token_sum = 0;
    double seconds_sum = 0.0;
    for (const Interval& interval : intervals_)
    {
        token_sum += interval.tokens;
        seconds_sum += interval.seconds;
    }
    if (seconds_sum <= 0.0)
        return false;
    tokens_per_second_ = static_cast<double>(token_sum) / seconds_sum;
    measured_at_ = timestamp;
    return true;
}

} // namespace draxul
