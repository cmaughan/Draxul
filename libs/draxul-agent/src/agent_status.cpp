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
    ScreenRule{ "interruptible_work", AgentStatus::Working, "progress_indicator", "esc to interrupt" },
    ScreenRule{ "running_command", AgentStatus::Working, "progress_indicator", "running command" },
    ScreenRule{ "thinking", AgentStatus::Working, "progress_indicator", "thinking" },
    ScreenRule{ "working", AgentStatus::Working, "progress_indicator", "working" },
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
    result.manifest_version = 1;

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

    // Newest visible rows take precedence over stale progress/completion text
    // higher in the terminal. Rule order resolves conflicts within one row.
    for (auto row = observation.bottom_rows.rbegin();
         row != observation.bottom_rows.rend(); ++row)
    {
        if (apply_rules(*row))
            return result;
    }
    if (apply_rules(observation.terminal_title))
        return result;

    result.fallback_reason = observation.bottom_rows.empty()
        ? "no_visible_terminal_evidence"
        : "ambiguous_terminal_evidence";
    return result;
}

} // namespace draxul
