#include "command_palette.h"

#include "fuzzy_match.h"
#include "gui_action_handler.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <draxul/app_config.h>
#include <draxul/events.h>
#include <draxul/keybinding_parser.h>
#include <draxul/perf_timing.h>
#include <imgui.h>

namespace draxul
{

CommandPalette::CommandPalette() = default;

CommandPalette::CommandPalette(Deps deps)
    : deps_(std::move(deps))
{
}

void CommandPalette::open()
{
    if (open_)
        return;
    open_ = true;
    focus_input_ = true;
    query_.clear();
    selected_index_ = 0;

    // Cache action names, excluding the palette's own action
    all_actions_.clear();
    for (auto name : GuiActionHandler::action_names())
    {
        if (name != "command_palette")
            all_actions_.push_back(name);
    }
    refilter();

    if (deps_.request_frame)
        deps_.request_frame();
}

void CommandPalette::close()
{
    if (open_)
        needs_close_frame_ = true;
    open_ = false;
}

bool CommandPalette::needs_render() const
{
    return open_ || needs_close_frame_;
}

bool CommandPalette::is_open() const
{
    return open_;
}

bool CommandPalette::on_key(const KeyEvent& event)
{
    if (!open_)
        return false;

    if (!event.pressed)
        return true; // consume key-up too

    const bool ctrl = (event.mod & kModCtrl) != 0;

    if (event.keycode == SDLK_ESCAPE)
    {
        close();
        return true;
    }
    if (event.keycode == SDLK_RETURN || event.keycode == SDLK_KP_ENTER)
    {
        execute_selected();
        return true;
    }
    if ((ctrl && event.keycode == SDLK_J) || event.keycode == SDLK_DOWN)
    {
        move_selection(1);
        return true;
    }
    if ((ctrl && event.keycode == SDLK_K) || event.keycode == SDLK_UP)
    {
        move_selection(-1);
        return true;
    }
    if (event.keycode == SDLK_BACKSPACE)
    {
        if (!query_.empty())
        {
            query_.pop_back();
            refilter();
            if (deps_.request_frame)
                deps_.request_frame();
        }
        return true;
    }
    // Ctrl+P while open = close (toggle)
    if (ctrl && event.keycode == SDLK_P)
    {
        close();
        return true;
    }

    // Consume all other keys to block host
    return true;
}

bool CommandPalette::on_text_input(const TextInputEvent& event)
{
    if (!open_)
        return false;

    query_ += event.text;
    refilter();
    if (deps_.request_frame)
        deps_.request_frame();
    return true;
}

void CommandPalette::refilter()
{
    filtered_.clear();
    for (auto name : all_actions_)
    {
        if (query_.empty())
        {
            filtered_.push_back({ name, shortcut_for_action(name), 0, {} });
        }
        else
        {
            auto result = fuzzy_match(query_, name);
            if (result.matched)
                filtered_.push_back({ name, shortcut_for_action(name), result.score, std::move(result.positions) });
        }
    }

    if (!query_.empty())
    {
        std::sort(filtered_.begin(), filtered_.end(), [](const FilteredEntry& a, const FilteredEntry& b) {
            if (a.score != b.score)
                return a.score > b.score;
            if (a.action_name.size() != b.action_name.size())
                return a.action_name.size() < b.action_name.size();
            return a.action_name < b.action_name;
        });
    }

    selected_index_ = std::clamp(selected_index_, 0, std::max(0, static_cast<int>(filtered_.size()) - 1));
}

void CommandPalette::execute_selected()
{
    if (selected_index_ >= 0 && selected_index_ < static_cast<int>(filtered_.size()))
    {
        const auto action = filtered_[static_cast<size_t>(selected_index_)].action_name;
        close();
        if (deps_.gui_action_handler)
            deps_.gui_action_handler->execute(action);
    }
    else
    {
        close();
    }
}

void CommandPalette::move_selection(int delta)
{
    if (filtered_.empty())
        return;
    selected_index_ = std::clamp(selected_index_ + delta, 0, static_cast<int>(filtered_.size()) - 1);
    if (deps_.request_frame)
        deps_.request_frame();
}

std::string CommandPalette::shortcut_for_action(std::string_view action) const
{
    if (!deps_.keybindings)
        return {};
    for (const auto& binding : *deps_.keybindings)
    {
        if (binding.action == action)
        {
            if (binding.prefix_key != 0)
            {
                return format_gui_keybinding_combo(binding.prefix_key, binding.prefix_modifiers) + ", "
                    + format_gui_keybinding_combo(binding.key, binding.modifiers);
            }
            return format_gui_keybinding_combo(binding.key, binding.modifiers);
        }
    }
    return {};
}

void CommandPalette::render(int window_width, int window_height)
{
    // Clear close-frame flag — the ImGui frame running now will reset WantCaptureKeyboard.
    needs_close_frame_ = false;

    if (!open_)
        return;

    PERF_MEASURE();

    const float w = static_cast<float>(window_width);
    const float h = static_cast<float>(window_height);

    // Semi-transparent background dim (behind the palette window, not on top)
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, 120));

    // Palette window: 50% width, up to 40% height, centered
    const float palette_w = w * 0.5f;
    const float palette_max_h = h * 0.4f;
    const float palette_x = (w - palette_w) * 0.5f;
    const float palette_y = h * 0.2f;

    ImGui::SetNextWindowPos(ImVec2(palette_x, palette_y));
    ImGui::SetNextWindowSize(ImVec2(palette_w, palette_max_h));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin("##CommandPalette", nullptr, flags))
    {
        ImGui::End();
        return;
    }

    // Results region (fills available space above the input line)
    const float input_line_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const float results_height = ImGui::GetContentRegionAvail().y - input_line_height;

    if (results_height > 0 && ImGui::BeginChild("##Results", ImVec2(0, results_height), ImGuiChildFlags_None))
    {
        for (int i = 0; i < static_cast<int>(filtered_.size()); ++i)
        {
            const auto& entry = filtered_[static_cast<size_t>(i)];
            const bool is_selected = (i == selected_index_);

            ImGui::PushID(i);

            if (is_selected)
            {
                const ImVec4 sel_color(0.25f, 0.45f, 0.85f, 0.6f);
                ImGui::PushStyleColor(ImGuiCol_Header, sel_color);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, sel_color);
            }

            // Render action name with highlighted match characters
            if (ImGui::Selectable("##entry", is_selected, ImGuiSelectableFlags_SpanAllColumns))
            {
                selected_index_ = i;
                execute_selected();
            }

            // Draw the text on top of the selectable
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetCursorPosX(ImGui::GetStyle().ItemSpacing.x);

            const std::string name_str(entry.action_name);
            for (size_t ci = 0; ci < name_str.size(); ++ci)
            {
                if (ci > 0)
                    ImGui::SameLine(0.0f, 0.0f);

                bool is_match = false;
                for (size_t pos : entry.match_positions)
                {
                    if (pos == ci)
                    {
                        is_match = true;
                        break;
                    }
                }

                if (is_match)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.25f, 1.0f));

                char buf[2] = { name_str[ci], '\0' };
                ImGui::TextUnformatted(buf, buf + 1);

                if (is_match)
                    ImGui::PopStyleColor();
            }

            // Right-aligned shortcut hint
            if (!entry.shortcut_hint.empty())
            {
                const float hint_width = ImGui::CalcTextSize(entry.shortcut_hint.c_str()).x;
                const float avail = ImGui::GetContentRegionAvail().x;
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - hint_width);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                ImGui::TextUnformatted(entry.shortcut_hint.c_str());
                ImGui::PopStyleColor();
            }

            if (is_selected)
                ImGui::PopStyleColor(2);

            // Auto-scroll to selected
            if (is_selected)
                ImGui::SetItemDefaultFocus();

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // Separator between results and input
    ImGui::Separator();

    // Input line: "> " prompt + text field
    ImGui::TextUnformatted(">");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

    if (focus_input_)
    {
        ImGui::SetKeyboardFocusHere();
        focus_input_ = false;
    }

    // Display: show query while typing, or selected action name if navigating
    const std::string& display = query_;
    // Use a static buffer for InputText compatibility
    static char input_buf[256] = {};
    const size_t copy_len = std::min(display.size(), sizeof(input_buf) - 1);
    std::memcpy(input_buf, display.data(), copy_len);
    input_buf[copy_len] = '\0';

    // Read-only display — actual input is handled by on_key/on_text_input
    ImGui::InputText("##input", input_buf, sizeof(input_buf), ImGuiInputTextFlags_ReadOnly);

    ImGui::End();
}

} // namespace draxul
