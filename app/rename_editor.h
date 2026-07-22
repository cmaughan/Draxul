#pragma once

#include "split_tree.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace draxul
{

enum class RenameTarget
{
    None,
    Tab,
    Pane,
};

enum class RenameKey
{
    Enter,
    Escape,
    Backspace,
    Delete,
    Left,
    Right,
    Home,
    End,
    Other,
};

struct RenameCommit
{
    RenameTarget target = RenameTarget::None;
    int tab_id = -1;
    LeafId leaf_id = kInvalidLeaf;
    std::string text;
};

struct RenameSnapshot
{
    RenameTarget target = RenameTarget::None;
    int tab_id = -1;
    LeafId leaf_id = kInvalidLeaf;
    std::string_view buffer;
    size_t cursor = 0;
    std::chrono::steady_clock::time_point started_at{};
};

// Renderer-independent state machine for inline tab and pane renaming.
// Callers own lookup, commit side effects, glyph warming, and frame scheduling.
class RenameEditor
{
public:
    void begin_tab(int tab_id, std::string initial_text);
    void begin_pane(LeafId leaf_id, std::string initial_text);

    [[nodiscard]] bool active() const;
    [[nodiscard]] bool editing_tab() const;
    [[nodiscard]] bool editing_pane() const;
    [[nodiscard]] int tab_id() const;
    [[nodiscard]] LeafId leaf_id() const;
    [[nodiscard]] RenameSnapshot snapshot() const;

    bool insert(std::string_view utf8);
    // Returns a commit when Enter was pressed. Escape cancels. Every key is
    // consumed while an edit is active, including RenameKey::Other.
    std::optional<RenameCommit> handle_key(RenameKey key);
    std::optional<RenameCommit> commit();
    void cancel();

private:
    void touch();

    RenameTarget target_ = RenameTarget::None;
    int tab_id_ = -1;
    LeafId leaf_id_ = kInvalidLeaf;
    std::string buffer_;
    size_t cursor_ = 0;
    std::chrono::steady_clock::time_point started_at_{};
};

} // namespace draxul
