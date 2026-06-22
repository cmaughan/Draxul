# Command Palette Save Session As Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a command-palette action that prompts for a session name, saves the current default/restored shell session under a generated named session id, and switches the running app to that new session.

**Architecture:** Extend the existing command palette into two modes: action search and a small text prompt rendered by the same grid/font pipeline. Put reusable session-id generation in a focused app helper, then make `App::save_session_as()` update the active session id/name, persist state, and rebind live attach metadata/server when needed.

**Tech Stack:** C++20, SDL3 key/text events, Draxul grid GUI renderer, app session TOML persistence, `SessionAttachServer`, Catch2 tests, CMake.

---

## File Structure

- Create `app/session_id.h`: public app-layer helpers for generated session ids and availability checks.
- Create `app/session_id.cpp`: slug/timestamp/candidate generation plus saved/live session collision checks.
- Modify `CMakeLists.txt`: add `app/session_id.cpp` to `draxul-app`.
- Modify `app/main.cpp`: replace file-local generated-session helper functions with `session_id.h`.
- Modify `tests/session_state_tests.cpp`: add unit coverage for session-id slugging, timestamp shape, suffixing, and saved-state collision handling.
- Modify `libs/draxul-gui/include/draxul/gui/palette_renderer.h`: extend palette view state with action/prompt mode metadata.
- Modify `libs/draxul-gui/src/palette_renderer.cpp`: render prompt title, label, input, validation message, and cursor using the same cell pipeline.
- Modify `app/command_palette.h` and `app/command_palette.cpp`: add prompt mode, prompt callbacks, prompt validation, and mode-aware input handling.
- Modify `app/command_palette_host.h` and `app/command_palette_host.cpp`: add `open_prompt()` so app actions can open a text prompt after palette action execution closes the search palette.
- Modify `tests/command_palette_tests.cpp`: cover prompt mode state transitions, submit/cancel behavior, host lifecycle, and prompt rendering.
- Modify `libs/draxul-config/include/draxul/gui_actions.h`: register `save_session_as`.
- Modify `app/gui_action_handler.h` and `app/gui_action_handler.cpp`: dispatch `save_session_as` to an app callback.
- Modify `tests/gui_action_handler_tests.cpp`: cover the new callback and keep registry parity passing.
- Modify `app/app.h` and `app/app.cpp`: add `App::save_session_as()`, prompt wiring, and reusable session-attach server start logic.
- Modify `tests/app_smoke_tests.cpp`: integration-test saving a running app session under a named id and switching future checkpoints to that id.
- Modify `docs/features.md`: document the new palette action and session behavior.

---

### Task 1: Extract Generated Session Id Helpers

**Files:**
- Create: `app/session_id.h`
- Create: `app/session_id.cpp`
- Modify: `CMakeLists.txt`
- Modify: `app/main.cpp`
- Test: `tests/session_state_tests.cpp`

- [ ] **Step 1: Write failing tests for generated session ids**

Add this include to `tests/session_state_tests.cpp`:

```cpp
#include "session_id.h"
```

Append these tests to `tests/session_state_tests.cpp`:

```cpp
TEST_CASE("session id: generated slug is lowercase and separator-normalized", "[session_state][session_id]")
{
    CHECK(make_session_id_slug(" Work Bench!! ") == "work-bench");
    CHECK(make_session_id_slug("...") == "session");
    CHECK(make_session_id_slug("Alpha/Beta_Gamma") == "alpha-beta-gamma");
}

TEST_CASE("session id: timestamp format is stable shape", "[session_state][session_id]")
{
    const std::string stamp = format_session_id_timestamp(0);
    REQUIRE(stamp.size() == 15);
    CHECK(stamp[8] == '-');
    for (size_t i = 0; i < stamp.size(); ++i)
    {
        if (i == 8)
            continue;
        CHECK(stamp[i] >= '0');
        CHECK(stamp[i] <= '9');
    }
}

TEST_CASE("session id: generated candidates append suffixes after the first collision", "[session_state][session_id]")
{
    const std::string base = make_session_id_base("Work Bench", 123456789);
    CHECK(make_session_id_candidate(base, 1) == base);
    CHECK(make_session_id_candidate(base, 2) == base + "-2");
    CHECK(make_session_id_candidate(base, 9) == base + "-9");
}

TEST_CASE("session id: unique generator skips saved session collisions", "[session_state][session_id]")
{
    TempDir temp_dir("session-id-unique");
    HomeDirRedirect redirect(temp_dir.path);

    const int64_t fixed_time = 123456789;
    const std::string base = make_session_id_base("Work Bench", fixed_time);

    AppSessionState existing;
    existing.session_id = base;
    existing.session_name = "Work Bench";
    existing.active_workspace_id = 1;
    existing.next_workspace_id = 2;

    std::string error;
    REQUIRE(save_session_state(existing, &error));
    REQUIRE(error.empty());

    auto generated = make_unique_session_id("Work Bench", fixed_time);
    REQUIRE(generated);
    CHECK(*generated == base + "-2");
}
```

- [ ] **Step 2: Run tests and verify they fail before implementation**

Run:

```bash
cmake --build build --target draxul-tests
./build/tests/draxul-tests "[session_id]"
```

Expected: build fails because `session_id.h` does not exist.

- [ ] **Step 3: Add `app/session_id.h`**

Create `app/session_id.h`:

```cpp
#pragma once

#include <cstdint>
#include <draxul/result.h>
#include <string>
#include <string_view>

namespace draxul
{

std::string make_session_id_slug(std::string_view text);
std::string format_session_id_timestamp(int64_t unix_seconds);
std::string make_session_id_base(std::string_view display_name, int64_t unix_seconds);
std::string make_session_id_candidate(std::string_view base, int suffix);

Result<bool, Error> session_id_exists(std::string_view session_id);
Result<std::string, Error> make_unique_session_id(
    std::string_view display_name,
    int64_t unix_seconds);

} // namespace draxul
```

- [ ] **Step 4: Add `app/session_id.cpp`**

Create `app/session_id.cpp`:

```cpp
#include "session_id.h"

#include "session_state.h"

#include <draxul/session_attach.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace draxul
{

std::string make_session_id_slug(std::string_view text)
{
    std::string slug;
    slug.reserve(text.size());
    bool last_was_separator = false;
    for (unsigned char ch : text)
    {
        if (std::isalnum(ch))
        {
            slug.push_back(static_cast<char>(std::tolower(ch)));
            last_was_separator = false;
        }
        else if (!last_was_separator && !slug.empty())
        {
            slug.push_back('-');
            last_was_separator = true;
        }
    }

    while (!slug.empty() && slug.back() == '-')
        slug.pop_back();
    if (slug.empty())
        slug = "session";
    if (slug.size() > 40)
        slug.resize(40);
    while (!slug.empty() && slug.back() == '-')
        slug.pop_back();
    if (slug.empty())
        slug = "session";
    return slug;
}

namespace
{

std::tm local_time_from_unix(int64_t unix_seconds)
{
    const std::time_t raw = static_cast<std::time_t>(unix_seconds);
    std::tm local = {};
#ifdef _WIN32
    localtime_s(&local, &raw);
#else
    localtime_r(&raw, &local);
#endif
    return local;
}

} // namespace

std::string format_session_id_timestamp(int64_t unix_seconds)
{
    const std::tm local = local_time_from_unix(unix_seconds);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d-%02d%02d%02d",
        local.tm_year + 1900,
        local.tm_mon + 1,
        local.tm_mday,
        local.tm_hour,
        local.tm_min,
        local.tm_sec);
    return buffer;
}

std::string make_session_id_base(std::string_view display_name, int64_t unix_seconds)
{
    return make_session_id_slug(display_name) + "-" + format_session_id_timestamp(unix_seconds);
}

std::string make_session_id_candidate(std::string_view base, int suffix)
{
    if (suffix <= 1)
        return std::string(base);
    return std::string(base) + "-" + std::to_string(suffix);
}

Result<bool, Error> session_id_exists(std::string_view session_id)
{
    std::string probe_error;
    const auto probe_status = SessionAttachServer::probe(session_id, &probe_error);
    if (probe_status == SessionAttachServer::ProbeStatus::Running)
        return true;
    if (probe_status == SessionAttachServer::ProbeStatus::Error)
    {
        return Result<bool, Error>::err(Error::io(
            probe_error.empty() ? "Failed probing for an existing session." : probe_error));
    }

    std::string io_error;
    if (has_saved_session_state(session_id, &io_error))
        return true;
    if (!io_error.empty())
        return Result<bool, Error>::err(Error::io(io_error));

    (void)clear_session_runtime_liveness(session_id);
    return false;
}

Result<std::string, Error> make_unique_session_id(
    std::string_view display_name,
    int64_t unix_seconds)
{
    const std::string base = make_session_id_base(display_name, unix_seconds);
    for (int suffix = 1;; ++suffix)
    {
        const std::string candidate = make_session_id_candidate(base, suffix);
        auto exists = session_id_exists(candidate);
        if (!exists)
            return Result<std::string, Error>::err(exists.error());
        if (!*exists)
            return candidate;
    }
}

} // namespace draxul
```

- [ ] **Step 5: Wire the helper into the build**

In the top-level `CMakeLists.txt`, add `app/session_id.cpp` to the `draxul-app` source list next to `app/session_state.cpp`:

```cmake
    app/session_id.cpp
    app/session_state.cpp
```

- [ ] **Step 6: Replace duplicate helper logic in `app/main.cpp`**

Add this include near the existing session-state include:

```cpp
#include "session_id.h"
```

Delete the file-local `generated_session_slug`, `local_time_from_unix`, and `generated_session_timestamp` functions from `app/main.cpp`.

Replace the file-local `session_exists()` body with this wrapper so existing startup code keeps its bool/error style:

```cpp
bool session_exists(std::string_view session_id, std::string* error)
{
    auto exists = draxul::session_id_exists(session_id);
    if (!exists)
    {
        if (error)
            *error = exists.error().message;
        return false;
    }
    if (error)
        error->clear();
    return *exists;
}
```

In `prepare_new_session_launch()`, replace the generated candidate code with:

```cpp
    const int64_t unix_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
                                     .count();
    auto generated_id = draxul::make_unique_session_id(parsed.session_name, unix_seconds);
    if (!generated_id)
    {
        if (error)
            *error = generated_id.error().message;
        return false;
    }
    parsed.session_id = *generated_id;
    return true;
```

- [ ] **Step 7: Run session-id tests**

Run:

```bash
cmake --build build --target draxul-tests
./build/tests/draxul-tests "[session_id]"
```

Expected: all `[session_id]` tests pass.

- [ ] **Step 8: Commit Task 1**

Run:

```bash
git add app/session_id.h app/session_id.cpp app/main.cpp CMakeLists.txt tests/session_state_tests.cpp
git commit -m "refactor: share generated session id helpers"
```

---

### Task 2: Add Prompt Mode To CommandPalette

**Files:**
- Modify: `libs/draxul-gui/include/draxul/gui/palette_renderer.h`
- Modify: `app/command_palette.h`
- Modify: `app/command_palette.cpp`
- Test: `tests/command_palette_tests.cpp`

- [ ] **Step 1: Write failing prompt state tests**

Append these tests to `tests/command_palette_tests.cpp`:

```cpp
TEST_CASE("CommandPalette prompt: submit trims input and closes", "[palette][prompt]")
{
    std::string submitted;
    int close_count = 0;
    CommandPalette palette(CommandPalette::Deps{
        .on_closed = [&]() { ++close_count; },
    });

    CommandPalette::PromptRequest request;
    request.title = "Save Session As";
    request.prompt = "Name";
    request.initial_value = " Before ";
    request.on_submit = [&](std::string value) {
        submitted = std::move(value);
    };

    palette.open_prompt(std::move(request));
    REQUIRE(palette.is_open());

    KeyEvent enter{ 0, SDLK_RETURN, kModNone, true };
    CHECK(palette.on_key(enter));

    CHECK(submitted == "Before");
    CHECK_FALSE(palette.is_open());
    CHECK(close_count == 1);
}

TEST_CASE("CommandPalette prompt: empty submit keeps prompt open with message", "[palette][prompt]")
{
    bool submitted = false;
    CommandPalette palette;

    CommandPalette::PromptRequest request;
    request.title = "Save Session As";
    request.prompt = "Name";
    request.initial_value = "   ";
    request.on_submit = [&](std::string) { submitted = true; };

    palette.open_prompt(std::move(request));

    KeyEvent enter{ 0, SDLK_RETURN, kModNone, true };
    CHECK(palette.on_key(enter));

    CHECK_FALSE(submitted);
    CHECK(palette.is_open());

    auto state = palette.view_state(40, 8);
    CHECK(state.mode == gui::PaletteMode::Prompt);
    CHECK(state.message == "Enter a session name");
}

TEST_CASE("CommandPalette prompt: Escape cancels without submitting", "[palette][prompt]")
{
    bool submitted = false;
    bool cancelled = false;
    CommandPalette palette;

    CommandPalette::PromptRequest request;
    request.title = "Save Session As";
    request.prompt = "Name";
    request.initial_value = "Draft";
    request.on_submit = [&](std::string) { submitted = true; };
    request.on_cancel = [&]() { cancelled = true; };

    palette.open_prompt(std::move(request));

    KeyEvent escape{ 0, SDLK_ESCAPE, kModNone, true };
    CHECK(palette.on_key(escape));

    CHECK_FALSE(submitted);
    CHECK(cancelled);
    CHECK_FALSE(palette.is_open());
}

TEST_CASE("CommandPalette prompt: text input and backspace edit the prompt value", "[palette][prompt]")
{
    CommandPalette palette;
    CommandPalette::PromptRequest request;
    request.title = "Save Session As";
    request.prompt = "Name";
    request.initial_value = "Al";
    palette.open_prompt(std::move(request));

    TextInputEvent input;
    input.text = "pha";
    CHECK(palette.on_text_input(input));

    auto state = palette.view_state(40, 8);
    CHECK(state.query == "Alpha");

    KeyEvent backspace{ 0, SDLK_BACKSPACE, kModNone, true };
    CHECK(palette.on_key(backspace));

    state = palette.view_state(40, 8);
    CHECK(state.query == "Alph");
}
```

- [ ] **Step 2: Run tests and verify they fail before implementation**

Run:

```bash
cmake --build build --target draxul-tests
./build/tests/draxul-tests "[palette][prompt]"
```

Expected: build fails because `CommandPalette::PromptRequest`, `open_prompt()`, and `gui::PaletteMode` do not exist.

- [ ] **Step 3: Extend palette view-state types**

In `libs/draxul-gui/include/draxul/gui/palette_renderer.h`, add this enum before `PaletteEntry`:

```cpp
enum class PaletteMode
{
    Actions,
    Prompt,
};
```

Extend `PaletteViewState`:

```cpp
struct PaletteViewState
{
    PaletteMode mode = PaletteMode::Actions;
    int grid_cols = 0;
    int grid_rows = 0;
    std::string_view title;
    std::string_view prompt;
    std::string_view query;
    std::string_view message;
    int selected_index = -1;
    std::span<const PaletteEntry> entries;
    float panel_bg_alpha = 1.0f;
};
```

- [ ] **Step 4: Add prompt API and state to `CommandPalette`**

In `app/command_palette.h`, add this public request struct:

```cpp
    struct PromptRequest
    {
        std::string title;
        std::string prompt;
        std::string initial_value;
        std::function<void(std::string)> on_submit;
        std::function<void()> on_cancel;
    };
```

Add this public method next to `open()`:

```cpp
    void open_prompt(PromptRequest request);
```

Add these private declarations:

```cpp
    enum class Mode
    {
        Actions,
        Prompt,
    };

    void submit_prompt();
    void cancel_prompt();
```

Add these members:

```cpp
    Mode mode_ = Mode::Actions;
    PromptRequest prompt_;
    std::string prompt_message_;
```

- [ ] **Step 5: Implement mode-aware prompt behavior**

In `app/command_palette.cpp`, add this helper near the top of the namespace:

```cpp
namespace
{

std::string trim_copy(std::string_view value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return std::string(value.substr(begin, end - begin));
}

} // namespace
```

Add `#include <cctype>` to the includes.

At the start of `CommandPalette::open()`, set action mode:

```cpp
    mode_ = Mode::Actions;
    prompt_ = PromptRequest{};
    prompt_message_.clear();
```

Implement `open_prompt()`:

```cpp
void CommandPalette::open_prompt(PromptRequest request)
{
    open_ = true;
    mode_ = Mode::Prompt;
    prompt_ = std::move(request);
    query_ = prompt_.initial_value;
    prompt_message_.clear();
    selected_index_ = -1;
    filtered_.clear();
    all_actions_.clear();
    if (deps_.request_frame)
        deps_.request_frame();
}
```

Update `on_key()` so Escape/Enter/Backspace behave differently in prompt mode:

```cpp
    if (event.keycode == SDLK_ESCAPE)
    {
        if (mode_ == Mode::Prompt)
            cancel_prompt();
        else
            close();
        return true;
    }
    if (event.keycode == SDLK_RETURN || event.keycode == SDLK_KP_ENTER)
    {
        if (mode_ == Mode::Prompt)
            submit_prompt();
        else
            execute_selected();
        return true;
    }
    if (event.keycode == SDLK_BACKSPACE)
    {
        if (!query_.empty())
        {
            query_.pop_back();
            prompt_message_.clear();
            if (mode_ == Mode::Actions)
                refilter();
            if (deps_.request_frame)
                deps_.request_frame();
        }
        return true;
    }
```

Before arrow-key and Tab handling, guard action-only controls:

```cpp
    if (mode_ == Mode::Prompt)
        return true;
```

Update `on_text_input()`:

```cpp
    query_ += event.text;
    prompt_message_.clear();
    if (mode_ == Mode::Actions)
        refilter();
    if (deps_.request_frame)
        deps_.request_frame();
    return true;
```

Add submit/cancel implementations:

```cpp
void CommandPalette::submit_prompt()
{
    std::string value = trim_copy(query_);
    if (value.empty())
    {
        prompt_message_ = "Enter a session name";
        if (deps_.request_frame)
            deps_.request_frame();
        return;
    }

    auto callback = std::move(prompt_.on_submit);
    close();
    if (callback)
        callback(std::move(value));
}

void CommandPalette::cancel_prompt()
{
    auto callback = std::move(prompt_.on_cancel);
    close();
    if (callback)
        callback();
}
```

Update `view_state()` so prompt mode populates the new fields:

```cpp
    state.mode = mode_ == Mode::Prompt ? gui::PaletteMode::Prompt : gui::PaletteMode::Actions;
    state.title = prompt_.title;
    state.prompt = prompt_.prompt;
    state.query = query_;
    state.message = prompt_message_;
```

Keep existing action entry population only when `mode_ == Mode::Actions`; in prompt mode return an empty entry span.

- [ ] **Step 6: Run prompt state tests**

Run:

```bash
cmake --build build --target draxul-tests
./build/tests/draxul-tests "[palette][prompt]"
```

Expected: prompt state tests pass. Prompt rendering tests may still be absent until Task 3.

- [ ] **Step 7: Commit Task 2**

Run:

```bash
git add app/command_palette.h app/command_palette.cpp libs/draxul-gui/include/draxul/gui/palette_renderer.h tests/command_palette_tests.cpp
git commit -m "feat: add command palette prompt mode"
```

---

### Task 3: Render Prompt Mode And Add Host API

**Files:**
- Modify: `libs/draxul-gui/src/palette_renderer.cpp`
- Modify: `app/command_palette_host.h`
- Modify: `app/command_palette_host.cpp`
- Test: `tests/command_palette_tests.cpp`

- [ ] **Step 1: Write failing renderer and host tests**

Append these tests to `tests/command_palette_tests.cpp`:

```cpp
TEST_CASE("render_palette: prompt mode fills the host grid", "[palette][prompt][render]")
{
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    gui::PaletteViewState state;
    state.mode = gui::PaletteMode::Prompt;
    state.grid_cols = 40;
    state.grid_rows = 8;
    state.title = "Save Session As";
    state.prompt = "Name";
    state.query = "Work Bench";
    state.message = "";

    const auto cells = gui::render_palette(state, text_service);
    REQUIRE(cells.size() == 40 * 8);
}

TEST_CASE("CommandPaletteHost prompt: open_prompt allocates a handle and submits text", "[palette][prompt][host]")
{
    PaletteHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    std::string submitted;
    CommandPalette::PromptRequest request;
    request.title = "Save Session As";
    request.prompt = "Name";
    request.initial_value = "";
    request.on_submit = [&](std::string value) {
        submitted = std::move(value);
    };

    REQUIRE(h.host.open_prompt(std::move(request)));
    REQUIRE(h.host.is_active());
    REQUIRE(h.renderer.create_grid_handle_calls == 1);

    TextInputEvent input;
    input.text = "Work Bench";
    h.host.on_text_input(input);

    KeyEvent enter{ 0, SDLK_RETURN, kModNone, true };
    h.host.on_key(enter);

    CHECK(submitted == "Work Bench");
    CHECK_FALSE(h.host.is_active());
}
```

- [ ] **Step 2: Run tests and verify they fail before implementation**

Run:

```bash
cmake --build build --target draxul-tests
./build/tests/draxul-tests "[palette][prompt]"
```

Expected: build fails because `CommandPaletteHost::open_prompt()` does not exist, or the render test fails because prompt rows are not rendered intentionally.

- [ ] **Step 3: Add prompt rendering branch**

In `libs/draxul-gui/src/palette_renderer.cpp`, add these helpers inside the anonymous namespace:

```cpp
void write_text(
    std::vector<PanelCell>& grid,
    const PanelLayout& layout,
    draxul::TextService& text_service,
    int row,
    int col,
    std::string_view text,
    Color fg)
{
    if (row < 0 || row >= layout.rows)
        return;
    for (int i = 0; i < static_cast<int>(text.size()) && col + i < layout.cols - kPanelPadding; ++i)
    {
        if (col + i < 0)
            continue;
        auto& cell = grid[static_cast<size_t>(row * layout.cols + col + i)];
        cell.glyph = text_service.resolve_cluster(std::string(1, text[static_cast<size_t>(i)]));
        cell.fg = fg;
    }
}

void write_cursor(std::vector<PanelCell>& grid, const PanelLayout& layout, int row, int col)
{
    if (row < 0 || row >= layout.rows || col < 0 || col >= layout.cols - kPanelPadding)
        return;
    auto& cell = grid[static_cast<size_t>(row * layout.cols + col)];
    cell.bg = kCursorBg;
    cell.fg = kCursorBg;
}
```

After the grid background has been populated in `render_palette()`, branch prompt mode before action row rendering:

```cpp
    if (state.mode == PaletteMode::Prompt)
    {
        const int pad = kPanelPadding;
        write_text(grid, layout, text_service, 0, pad, state.title.empty() ? "Prompt" : state.title, kTextFg);

        const int input_row = std::max(2, layout.rows / 2);
        std::string label;
        if (!state.prompt.empty())
        {
            label.assign(state.prompt);
            label += ": ";
        }
        write_text(grid, layout, text_service, input_row, pad, label, kPromptFg);

        const int query_col = pad + static_cast<int>(label.size());
        const int query_max = std::max(0, layout.cols - query_col - pad - 1);
        const int query_len = std::min(static_cast<int>(state.query.size()), query_max);
        write_text(grid, layout, text_service, input_row, query_col,
            std::string_view(state.query.data(), static_cast<size_t>(query_len)), kTextFg);
        write_cursor(grid, layout, input_row, query_col + query_len);

        if (!state.message.empty())
            write_text(grid, layout, text_service, std::min(layout.rows - 1, input_row + 2), pad, state.message, kHighlightFg);

        std::vector<CellUpdate> cells;
        cells.reserve(static_cast<size_t>(total));
        for (int r = 0; r < layout.rows; ++r)
        {
            for (int c = 0; c < layout.cols; ++c)
            {
                const auto& pc = grid[static_cast<size_t>(r * layout.cols + c)];
                CellUpdate cu;
                cu.col = layout.col0 + c;
                cu.row = layout.row0 + r;
                cu.bg = pc.bg;
                cu.fg = pc.fg;
                cu.sp = kTransparent;
                cu.glyph = pc.glyph;
                cu.style_flags = 0;
                cells.push_back(cu);
            }
        }
        return cells;
    }
```

Keep the existing action rendering path unchanged after this branch.

- [ ] **Step 4: Add `CommandPaletteHost::open_prompt()`**

In `app/command_palette_host.h`, add this public method:

```cpp
    bool open_prompt(CommandPalette::PromptRequest request);
```

In `app/command_palette_host.cpp`, add a helper that opens the overlay handle for either action or prompt modes:

```cpp
bool CommandPaletteHost::open_prompt(CommandPalette::PromptRequest request)
{
    if (!renderer_)
        return false;
    if (!handle_)
    {
        handle_ = renderer_->create_grid_handle();
        if (!handle_)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "CommandPaletteHost: create_grid_handle() returned null, cannot open prompt");
            return false;
        }
        handle_->set_viewport(palette_pane_descriptor());
    }
    palette_.open_prompt(std::move(request));
    refresh_open_palette();
    if (callbacks_)
        callbacks_->request_frame();
    return true;
}
```

In `dispatch_action("toggle")`, replace the open-side handle creation with a call to the same handle setup pattern used by `open_prompt()`. Keep toggle close behavior as-is.

- [ ] **Step 5: Run prompt renderer/host tests**

Run:

```bash
cmake --build build --target draxul-tests
./build/tests/draxul-tests "[palette][prompt]"
```

Expected: prompt state, render, and host tests pass.

- [ ] **Step 6: Commit Task 3**

Run:

```bash
git add libs/draxul-gui/src/palette_renderer.cpp app/command_palette_host.h app/command_palette_host.cpp tests/command_palette_tests.cpp
git commit -m "feat: render command palette text prompts"
```

---

### Task 4: Add `save_session_as` GUI Action

**Files:**
- Modify: `libs/draxul-config/include/draxul/gui_actions.h`
- Modify: `app/gui_action_handler.h`
- Modify: `app/gui_action_handler.cpp`
- Modify: `tests/gui_action_handler_tests.cpp`

- [ ] **Step 1: Write failing GUI action test**

Append this test to `tests/gui_action_handler_tests.cpp`:

```cpp
TEST_CASE("gui action handler: save_session_as invokes callback", "[gui_actions]")
{
    TextService ts;
    AppConfig config;
    GuiActionHandler::Deps deps;
    deps.text_service = &ts;
    deps.config = &config;
    int save_as_count = 0;
    deps.on_save_session_as = [&save_as_count]() { ++save_as_count; };
    GuiActionHandler handler(std::move(deps));

    const bool handled = handler.execute("save_session_as");

    REQUIRE(handled);
    CHECK(save_as_count == 1);
}
```

- [ ] **Step 2: Run tests and verify they fail before implementation**

Run:

```bash
cmake --build build --target draxul-tests
./build/tests/draxul-tests "[gui_actions]"
```

Expected: build fails because `on_save_session_as` is not a dependency member.

- [ ] **Step 3: Register the canonical action**

In `libs/draxul-config/include/draxul/gui_actions.h`, change the array size from `40` to `41` and add the action near `command_palette`:

```cpp
    { "command_palette" },
    { "save_session_as" },
    { "edit_config" },
```

- [ ] **Step 4: Dispatch the action**

In `app/gui_action_handler.h`, add this dependency callback after `on_command_palette`:

```cpp
        std::function<void()> on_save_session_as; // prompt for a named saved session
```

In `app/gui_action_handler.cpp`, add this map entry after `command_palette`:

```cpp
        {"save_session_as",    [](auto& h, auto) { if (h.deps_.on_save_session_as) h.deps_.on_save_session_as(); }},
```

- [ ] **Step 5: Run GUI action tests**

Run:

```bash
cmake --build build --target draxul-tests
./build/tests/draxul-tests "[gui_actions]"
```

Expected: callback test and registry parity tests pass.

- [ ] **Step 6: Commit Task 4**

Run:

```bash
git add libs/draxul-config/include/draxul/gui_actions.h app/gui_action_handler.h app/gui_action_handler.cpp tests/gui_action_handler_tests.cpp
git commit -m "feat: add save session as GUI action"
```

---

### Task 5: Implement App Session Save-As And Switch

**Files:**
- Modify: `app/app.h`
- Modify: `app/app.cpp`
- Test: `tests/app_smoke_tests.cpp`

- [ ] **Step 1: Write failing integration test**

Add this include to `tests/app_smoke_tests.cpp`:

```cpp
#include "session_state.h"
```

Append this test to `tests/app_smoke_tests.cpp`:

```cpp
TEST_CASE("app smoke: save_session_as persists a named session and switches active session id",
    "[app_smoke][session]")
{
    TempDir temp("draxul-save-session-as");
    HomeDirRedirect redir(temp.path);

    AppOptions opts = make_smoke_options();
    opts.enable_session_restore = true;
    opts.session_id = "default";
    opts.session_name = "default";

    App app(std::move(opts));
    REQUIRE(app.initialize());

    auto saved = app.save_session_as("Work Bench");
    REQUIRE(saved);
    const std::string new_id = *saved;
    CHECK(new_id.rfind("work-bench-", 0) == 0);

    auto new_state = load_session_state(new_id);
    REQUIRE(new_state);
    CHECK(new_state->session_id == new_id);
    CHECK(new_state->session_name == "Work Bench");

    std::string error;
    auto old_metadata = load_session_runtime_metadata("default", &error);
    if (old_metadata)
        CHECK_FALSE(old_metadata->live);

    REQUIRE(app.run_smoke_test(std::chrono::milliseconds(100)));

    auto checkpointed = load_session_state(new_id);
    REQUIRE(checkpointed);
    CHECK(checkpointed->session_name == "Work Bench");

    app.shutdown();
}
```

- [ ] **Step 2: Run test and verify it fails before implementation**

Run:

```bash
cmake --build build --target draxul-tests
./build/tests/draxul-tests "app smoke: save_session_as persists a named session and switches active session id"
```

Expected: build fails because `App::save_session_as()` is not public.

- [ ] **Step 3: Add public app command and private server starter declarations**

In `app/app.h`, add this public method after `shutdown()`:

```cpp
    Result<std::string, Error> save_session_as(std::string_view name);
```

Add this private helper near `initialize_session_attach()`:

```cpp
    bool start_session_attach_server(std::string* error);
```

- [ ] **Step 4: Refactor session attach start**

In `app/app.cpp`, replace the body that directly calls `session_attach_server_.start(...)` in `initialize_session_attach()` with:

```cpp
    std::string error;
    if (!start_session_attach_server(&error))
    {
        last_init_error_ = error.empty()
            ? "Failed to initialize session attach server."
            : error;
        return false;
    }
    return true;
```

Add this helper immediately after `initialize_session_attach()`:

```cpp
bool App::start_session_attach_server(std::string* error)
{
    return session_attach_server_.start(options_.session_id, [this](SessionAttachServer::Command command) {
        if (command == SessionAttachServer::Command::Activate)
            external_attach_requested_.store(true);
        else if (command == SessionAttachServer::Command::Detach)
            external_detach_requested_.store(true);
        else if (command == SessionAttachServer::Command::Shutdown)
            external_session_shutdown_requested_.store(true);
        wake_window();
    },
        [this]() { return live_session_info(); },
        [this](std::string_view session_name) {
            std::lock_guard lock(external_session_rename_mutex_);
            external_session_rename_requested_ = std::string(session_name);
            wake_window();
        },
        error);
}
```

- [ ] **Step 5: Implement save-as switching**

Add `#include "session_id.h"` to `app/app.cpp`.

Add this helper near other file-local helpers in `app/app.cpp`:

```cpp
std::string trim_session_name(std::string_view value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return std::string(value.substr(begin, end - begin));
}
```

Ensure `<cctype>` is included if it is not already present.

Add this method near `rename_session()`:

```cpp
Result<std::string, Error> App::save_session_as(std::string_view raw_name)
{
    PERF_MEASURE();
    std::string display_name = trim_session_name(raw_name);
    if (display_name.empty())
        return Result<std::string, Error>::err(Error::invalid_argument("Enter a session name."));
    if (!options_.enable_session_restore)
        return Result<std::string, Error>::err(Error::invalid_argument("Session restore is not enabled for this launch."));
    if (!can_snapshot_session_state())
        return Result<std::string, Error>::err(Error::invalid_argument("Current panes cannot be saved as a restorable shell session."));

    const int64_t now = unix_now_seconds();
    auto generated_id = make_unique_session_id(display_name, now);
    if (!generated_id)
        return Result<std::string, Error>::err(generated_id.error());
    const std::string new_id = *generated_id;

    const std::string old_id = options_.session_id;
    const std::string old_session_name = session_name_;
    const std::string old_option_session_name = options_.session_name;

    options_.session_id = new_id;
    options_.session_name = display_name;
    session_name_ = display_name;
    mark_session_attached();

    auto rollback = [&]() {
        options_.session_id = old_id;
        options_.session_name = old_option_session_name;
        session_name_ = old_session_name;
        std::string ignored;
        (void)delete_session_state(new_id, &ignored);
        ignored.clear();
        (void)delete_session_runtime_metadata(new_id, &ignored);
        if (options_.enable_session_attach)
        {
            ignored.clear();
            (void)start_session_attach_server(&ignored);
        }
    };

    if (options_.enable_session_attach)
    {
        std::string attach_error;
        if (!start_session_attach_server(&attach_error))
        {
            rollback();
            return Result<std::string, Error>::err(Error::io(
                attach_error.empty() ? "Failed to switch live session endpoint." : attach_error));
        }
    }

    auto state = snapshot_session_state();
    if (!state)
    {
        rollback();
        return Result<std::string, Error>::err(Error::invalid_argument("Current session could not be snapshotted."));
    }

    std::string error;
    if (!save_session_state(*state, &error))
    {
        rollback();
        return Result<std::string, Error>::err(Error::io(
            error.empty() ? "Failed to save named session." : error));
    }

    error.clear();
    if (!save_session_runtime_metadata(snapshot_session_runtime_metadata(options_.enable_session_attach), &error))
    {
        rollback();
        return Result<std::string, Error>::err(Error::io(
            error.empty() ? "Failed to save named session metadata." : error));
    }

    if (old_id != new_id)
    {
        error.clear();
        if (!clear_session_runtime_liveness(old_id, &error) && !error.empty())
        {
            DRAXUL_LOG_WARN(LogCategory::App,
                "Failed to clear old session liveness for %s: %s",
                old_id.c_str(),
                error.c_str());
        }
    }

    request_frame();
    return new_id;
}
```

- [ ] **Step 6: Run save-as integration test**

Run:

```bash
cmake --build build --target draxul-tests
./build/tests/draxul-tests "app smoke: save_session_as persists a named session and switches active session id"
```

Expected: the integration test passes.

- [ ] **Step 7: Run related app/session tests**

Run:

```bash
./build/tests/draxul-tests "[app_smoke][session]"
./build/tests/draxul-tests "[session_state]"
```

Expected: all selected tests pass.

- [ ] **Step 8: Commit Task 5**

Run:

```bash
git add app/app.h app/app.cpp tests/app_smoke_tests.cpp
git commit -m "feat: save current session under a named id"
```

---

### Task 6: Wire Prompt UI, Document, And Validate End-To-End

**Files:**
- Modify: `app/app.h`
- Modify: `app/app.cpp`
- Modify: `docs/features.md`

- [ ] **Step 1: Wire the GUI action to the prompt**

In `app/app.h`, add this private declaration near `wire_gui_actions()`:

```cpp
    void open_save_session_prompt();
```

In `app/app.cpp`, wire the action in `App::wire_gui_actions()` after `on_command_palette`:

```cpp
    gui_deps.on_save_session_as = [this]() {
        open_save_session_prompt();
    };
```

Add this method near `wire_gui_actions()`:

```cpp
void App::open_save_session_prompt()
{
    if (!palette_host_)
        return;

    CommandPalette::PromptRequest request;
    request.title = "Save Session As";
    request.prompt = "Name";
    request.initial_value = !session_name_.empty()
        ? session_name_
        : (!options_.session_name.empty() ? options_.session_name : options_.session_id);
    request.on_submit = [this](std::string name) {
        auto saved = save_session_as(name);
        if (!saved)
        {
            push_toast(2, saved.error().message);
            return;
        }
        push_toast(0, "Saved session '" + name + "'.");
    };

    if (!palette_host_->open_prompt(std::move(request)))
        push_toast(2, "Unable to open session name prompt.");
}
```

- [ ] **Step 2: Build after prompt wiring**

Run:

```bash
cmake --build build --target draxul-tests
```

Expected: build succeeds.

- [ ] **Step 3: Update feature documentation**

In `docs/features.md`, update the command palette bullet to mention text prompts:

```markdown
- **Command palette**: `Ctrl+P` opens a centered fuzzy-search overlay for all GUI actions with fzf-style scoring, `Ctrl+J/K` navigation, keybinding hints, and palette-rendered text prompts for actions that need a short value.
```

Update the session CLI/UI bullet by appending the UI save-as behavior:

```markdown
- **Session-scoped shell restore CLI**: `--session <id>` selects which saved shell session Draxul should restore, `--new-session` starts a fresh saved shell session (generating a unique id when `--session` is omitted), `--session-name <name>` sets the saved display name for a newly launched or restored session, `--rename-session --session-name <name>` renames a running or saved session, `--list-sessions` prints known sessions with live/detached/saved status and workspace/pane counts (preferring live owner summaries when available), `--persistent-app` enables live detach/reattach for desktop launches, `--attach-session` explicitly activates a running persistent app session, `--detach-session` explicitly detaches a running persistent app session without killing it, `--kill-session` explicitly kills a live session or deletes its saved topology, and the command palette's `save_session_as` action saves the current restorable shell topology under a prompted display name and switches the running app to the generated named session id.
```

Add this default keybinding row near `command_palette`:

```markdown
| `save_session_as` | (unbound) |
```

- [ ] **Step 4: Build app and tests**

Run:

```bash
cmake --build build --target draxul draxul-tests
```

Expected: build succeeds.

- [ ] **Step 5: Run automated validation**

Run:

```bash
ctest --test-dir build -R draxul-tests --output-on-failure
python do.py smoke
```

Expected: `ctest` passes and smoke test passes.

- [ ] **Step 6: Manually verify the UI flow**

Run:

```bash
./build/draxul.app/Contents/MacOS/draxul --host zsh --log-file /tmp/draxul-save-session-as.log --log-level debug
```

Manual checks:

1. Open the command palette with `Ctrl+Shift+P`.
2. Search `save_session_as`.
3. Press Enter.
4. Type `Work Bench`.
5. Press Enter.
6. Confirm a success toast appears.
7. Quit and relaunch with `./build/draxul.app/Contents/MacOS/draxul --list-sessions`.
8. Confirm a saved session with display name `Work Bench` exists and the generated id starts with `work-bench-`.

- [ ] **Step 7: Commit Task 6**

Run:

```bash
git add app/app.h app/app.cpp docs/features.md
git commit -m "feat: wire save session as prompt"
```

---

## Self-Review Checklist

- [ ] The prompt uses the existing command palette grid renderer and `TextService`, not a new UI framework.
- [ ] `save_session_as` saves under a generated id and changes `options_.session_id`, so future checkpoints target the named session.
- [ ] Old session metadata liveness is cleared so the session picker/list does not show the abandoned default session as still live.
- [ ] Live attach mode restarts the attach endpoint on the new id, and rollback restores the old endpoint on failure.
- [ ] Empty prompt submissions stay in the prompt and show `Enter a session name`.
- [ ] Registry parity tests still prove `kGuiActions` and `GuiActionHandler` agree.
- [ ] `docs/features.md` documents the new user-facing action.
