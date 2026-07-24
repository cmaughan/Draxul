#include <draxul/agent_model.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <span>

namespace draxul
{

namespace
{

struct ScreenRule
{
    std::string_view id;
    AgentStatus status;
    std::string_view evidence_category;
    std::string_view needle;
};

// Order is precedence. Blocking prompts deliberately win over progress and
// completion fragments that may remain elsewhere in the visible terminal.
constexpr std::array kCodexRules = {
    ScreenRule{ "approval_prompt", AgentStatus::Blocked, "approval_required", "do you want to proceed" },
    ScreenRule{ "confirmation_prompt", AgentStatus::Blocked, "input_required", "press enter to confirm" },
    ScreenRule{ "allow_command_prompt", AgentStatus::Blocked, "approval_required", "allow command" },
    ScreenRule{ "approval_required", AgentStatus::Blocked, "approval_required", "approval required" },
    ScreenRule{ "task_complete", AgentStatus::Done, "completion_indicator", "task complete" },
    ScreenRule{ "completed_successfully", AgentStatus::Done, "completion_indicator", "completed successfully" },
    ScreenRule{ "prompt_ready", AgentStatus::Idle, "input_prompt", "enter a prompt" },
    ScreenRule{ "message_ready", AgentStatus::Idle, "input_prompt", "type your message" },
};

constexpr std::array kClaudeRules = {
    ScreenRule{ "approval_prompt", AgentStatus::Blocked, "approval_required", "do you want to proceed" },
    ScreenRule{ "allow_command_prompt", AgentStatus::Blocked, "approval_required", "allow this command" },
    ScreenRule{ "persistent_approval_prompt", AgentStatus::Blocked, "approval_required", "yes, and don't ask again" },
    ScreenRule{ "interruptible_work", AgentStatus::Working, "progress_indicator", "esc to interrupt" },
    ScreenRule{ "backgroundable_work", AgentStatus::Working, "progress_indicator", "ctrl+b to run in background" },
    ScreenRule{ "task_complete", AgentStatus::Done, "completion_indicator", "task completed" },
    ScreenRule{ "prompt_ready", AgentStatus::Idle, "input_prompt", "how can i help you today" },
};

std::string normalized_text(std::string_view text)
{
    std::string normalized(text);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return normalized;
}

std::string_view trim(std::string_view text)
{
    while (!text.empty()
        && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty()
        && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

bool codex_title_has_spinner(std::string_view title)
{
    constexpr std::array<std::string_view, 10> spinners = {
        "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"
    };
    while (!title.empty())
    {
        title = trim(title);
        const size_t separator = title.find_first_of(" \t");
        const std::string_view token = title.substr(0, separator);
        if (std::ranges::find(spinners, token) != spinners.end())
            return true;
        if (separator == std::string_view::npos)
            break;
        title.remove_prefix(separator + 1);
    }
    return false;
}

bool codex_working_line(std::string_view line)
{
    line = trim(line);
    constexpr std::string_view solid_bullet = "• ";
    constexpr std::string_view hollow_bullet = "◦ ";
    if (line.starts_with(solid_bullet))
        line.remove_prefix(solid_bullet.size());
    else if (line.starts_with(hollow_bullet))
        line.remove_prefix(hollow_bullet.size());
    else
        return false;

    const std::string normalized = normalized_text(line);
    return normalized.starts_with("working (")
        && normalized.find("esc to interrupt)") != std::string::npos;
}

} // namespace

AgentStatusExplanation evaluate_agent_observation(
    std::string_view agent_kind, const AgentObservation& observation)
{
    AgentStatusExplanation result;
    result.observation_generation = observation.output_generation;
    result.evaluated_at = std::chrono::steady_clock::now();

    std::span<const ScreenRule> rules;
    if (agent_kind == "codex")
    {
        result.manifest_id = "codex-terminal";
        rules = kCodexRules;
    }
    else if (agent_kind == "claude")
    {
        result.manifest_id = "claude-terminal";
        rules = kClaudeRules;
    }
    else
    {
        result.fallback_reason = "no_bundled_manifest";
        return result;
    }

    result.authority = AgentStateAuthority::ScreenManifest;
    result.manifest_version = agent_kind == "codex" ? 2 : 1;

    const auto apply_rules = [&rules, &result](std::string_view evidence) {
        const std::string normalized = normalized_text(evidence);
        for (const auto& rule : rules)
        {
            if (normalized.find(rule.needle) == std::string::npos)
                continue;
            result.status = rule.status;
            result.rule_id = std::string(rule.id);
            result.evidence_category = std::string(rule.evidence_category);
            return true;
        }
        return false;
    };

    if (agent_kind == "codex")
    {
        const std::string title = normalized_text(observation.terminal_title);
        if (title.find("action required") != std::string::npos)
        {
            result.status = AgentStatus::Blocked;
            result.rule_id = "osc_title_blocked";
            result.evidence_category = "approval_required";
            return result;
        }
        if (codex_title_has_spinner(observation.terminal_title))
        {
            result.status = AgentStatus::Working;
            result.rule_id = "osc_title_working";
            result.evidence_category = "progress_indicator";
            return result;
        }
    }

    // Newest visible rows take precedence over stale progress/completion text
    // higher in the terminal. Rule order resolves conflicts within one row.
    for (auto row = observation.bottom_rows.rbegin();
         row != observation.bottom_rows.rend(); ++row)
    {
        if (apply_rules(*row))
            return result;
    }

    if (agent_kind == "codex")
    {
        std::array<std::string_view, 3> recent_non_empty_rows;
        size_t recent_count = 0;
        for (auto row = observation.bottom_rows.rbegin();
             row != observation.bottom_rows.rend() && recent_count < 3; ++row)
        {
            if (trim(*row).empty())
                continue;
            recent_non_empty_rows[recent_count++] = *row;
        }
        const bool interrupted = std::any_of(recent_non_empty_rows.begin(),
            recent_non_empty_rows.begin() + recent_count,
            [](std::string_view row) {
                return normalized_text(row).find("conversation interrupted")
                    != std::string::npos;
            });
        if (!interrupted)
        {
            for (size_t index = 0; index < recent_count; ++index)
            {
                if (codex_working_line(recent_non_empty_rows[index]))
                {
                    result.status = AgentStatus::Working;
                    result.rule_id = "screen_working_fallback";
                    result.evidence_category = "progress_indicator";
                    return result;
                }
            }
        }

        if (!trim(observation.terminal_title).empty())
        {
            result.status = AgentStatus::Idle;
            result.rule_id = "osc_title_idle";
            result.evidence_category = "input_prompt";
            return result;
        }
        for (auto row = observation.bottom_rows.rbegin();
             row != observation.bottom_rows.rend(); ++row)
        {
            const std::string_view prompt = trim(*row);
            if (prompt == "›" || prompt.starts_with("› "))
            {
                result.status = AgentStatus::Idle;
                result.rule_id = "prompt_marker";
                result.evidence_category = "input_prompt";
                return result;
            }
        }
    }
    else if (apply_rules(observation.terminal_title))
    {
        return result;
    }

    result.fallback_reason = observation.bottom_rows.empty()
        ? "no_visible_terminal_evidence"
        : "ambiguous_terminal_evidence";
    return result;
}

} // namespace draxul
