#include <draxul/agent_model.h>

#include <draxul/string_util.h>

#include <algorithm>
#include <array>
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

bool codex_title_has_spinner(std::string_view title)
{
    constexpr std::array<std::string_view, 10> spinners = {
        "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"
    };
    while (!title.empty())
    {
        title = trim_view(title);
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
    line = trim_view(line);
    constexpr std::string_view solid_bullet = "• ";
    constexpr std::string_view hollow_bullet = "◦ ";
    if (line.starts_with(solid_bullet))
        line.remove_prefix(solid_bullet.size());
    else if (line.starts_with(hollow_bullet))
        line.remove_prefix(hollow_bullet.size());
    else
        return false;

    const std::string normalized = ascii_lower(line);
    return normalized.starts_with("working (")
        && normalized.find("esc to interrupt)") != std::string::npos;
}

bool claude_input_border(std::string_view line)
{
    line = trim_view(line);
    size_t count = 0;
    while (line.starts_with("─"))
    {
        line.remove_prefix(std::string_view("─").size());
        ++count;
    }
    return count >= 3 && line.empty();
}

struct ClaudeComposer
{
    size_t top;
    size_t bottom;
};

std::optional<ClaudeComposer> claude_composer(const std::vector<std::string>& rows)
{
    for (size_t index = rows.size(); index-- > 1;)
    {
        auto prompt = trim_view(rows[index]);
        if (!prompt.starts_with("❯"))
            continue;
        prompt.remove_prefix(std::string_view("❯").size());
        if (!prompt.empty() && !prompt.starts_with(' ') && !prompt.starts_with('\t')
            && !prompt.starts_with("\xc2\xa0"))
            continue;
        if (prompt.starts_with("\xc2\xa0"))
            prompt.remove_prefix(2);
        prompt = trim_view(prompt);
        // Approval menus also use an arrow, but their numbered choices are
        // not a text composer.
        if (prompt.size() >= 3 && prompt.front() >= '0' && prompt.front() <= '9'
            && prompt.substr(1).starts_with(". "))
            continue;
        if (!claude_input_border(rows[index - 1]))
            continue;
        for (size_t bottom = index + 1; bottom < rows.size(); ++bottom)
            if (claude_input_border(rows[bottom]))
            {
                // A historical bordered prompt followed by response text is
                // not the current input area. Recognize only current footer UI.
                for (size_t footer = bottom + 1; footer < rows.size(); ++footer)
                {
                    const auto text = ascii_lower(trim_view(rows[footer]));
                    const bool status = std::ranges::any_of(kClaudeRules, [&](const ScreenRule& rule) {
                        return (rule.status == AgentStatus::Working || rule.status == AgentStatus::Blocked)
                            && text.find(rule.needle) != std::string::npos;
                    });
                    if (!text.empty() && text.find("shift+tab") == std::string::npos
                        && text.find("? for shortcuts") == std::string::npos
                        && text.find("context left") == std::string::npos && !status)
                        return std::nullopt;
                }
                return ClaudeComposer{ index - 1, bottom };
            }
    }
    return std::nullopt;
}

bool claude_completion_line(std::string_view line)
{
    line = trim_view(line);
    constexpr std::array<std::string_view, 6> markers = { "✻ ", "✽ ", "✶ ", "✳ ", "✢ ", "· " };
    const auto marker = std::ranges::find_if(markers,
        [line](std::string_view prefix) { return line.starts_with(prefix); });
    if (marker == markers.end())
        return false;
    line.remove_prefix(marker->size());
    const auto duration_start = line.find(" for ");
    if (duration_start == std::string_view::npos || duration_start == 0)
        return false;
    auto duration = line.substr(duration_start + 5);
    const auto detail = duration.find(" · ");
    if (detail != std::string_view::npos)
    {
        if (!duration.substr(detail).starts_with(" · done "))
            return false;
        duration = duration.substr(0, detail);
    }
    duration = trim_view(duration);
    return !duration.empty() && duration.front() >= '0' && duration.front() <= '9'
        && duration.find_first_of("hms") != std::string_view::npos
        && duration.find_first_not_of("0123456789.hms ") == std::string_view::npos;
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
    result.manifest_version = 2;

    const auto apply_rules = [&rules, &result](std::string_view evidence) {
        const std::string normalized = ascii_lower(evidence);
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

    if (agent_kind == "claude")
    {
        if (const auto composer = claude_composer(observation.bottom_rows))
        {
            std::optional<AgentStatusExplanation> working;
            // The bordered composer is current UI, not a historical user turn.
            // Never interpret the user's draft (including wrapped lines) as status.
            for (size_t index = observation.bottom_rows.size(); index-- > composer->bottom + 1;)
            {
                if (apply_rules(observation.bottom_rows[index]))
                {
                    if (result.status == AgentStatus::Blocked)
                        return result;
                    if (result.status == AgentStatus::Working)
                        working = result;
                }
            }
            // An active progress/approval row can sit just above the composer.
            // Older transcript rows beyond the nearest nonempty line are stale.
            for (size_t index = composer->top; index-- > 0;)
            {
                if (trim_view(observation.bottom_rows[index]).empty())
                    continue;
                if (apply_rules(observation.bottom_rows[index]))
                {
                    if (result.status == AgentStatus::Blocked)
                        return result;
                    if (result.status == AgentStatus::Working)
                        working = result;
                }
                break;
            }
            if (working)
                return *working;
            result.status = AgentStatus::Idle;
            result.rule_id = "current_input_prompt";
            result.evidence_category = "input_prompt";
            return result;
        }
    }

    if (agent_kind == "codex")
    {
        const std::string title = ascii_lower(observation.terminal_title);
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
        if (agent_kind == "claude" && claude_completion_line(*row))
        {
            result.status = AgentStatus::Done;
            result.rule_id = "elapsed_completion";
            result.evidence_category = "completion_indicator";
            return result;
        }
    }

    if (agent_kind == "codex")
    {
        std::array<std::string_view, 3> recent_non_empty_rows;
        size_t recent_count = 0;
        for (auto row = observation.bottom_rows.rbegin();
            row != observation.bottom_rows.rend() && recent_count < 3; ++row)
        {
            if (trim_view(*row).empty())
                continue;
            recent_non_empty_rows[recent_count++] = *row;
        }
        const bool interrupted = std::any_of(recent_non_empty_rows.begin(),
            recent_non_empty_rows.begin() + recent_count,
            [](std::string_view row) {
                return ascii_lower(row).find("conversation interrupted")
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

        if (!trim_view(observation.terminal_title).empty())
        {
            result.status = AgentStatus::Idle;
            result.rule_id = "osc_title_idle";
            result.evidence_category = "input_prompt";
            return result;
        }
        for (auto row = observation.bottom_rows.rbegin();
            row != observation.bottom_rows.rend(); ++row)
        {
            const std::string_view prompt = trim_view(*row);
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
