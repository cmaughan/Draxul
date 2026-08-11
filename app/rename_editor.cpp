#include "rename_editor.h"

namespace draxul
{
namespace
{
size_t utf8_prev(const std::string& text, size_t pos)
{
    if (pos == 0)
        return 0;
    --pos;
    while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80)
        --pos;
    return pos;
}

size_t utf8_next(const std::string& text, size_t pos)
{
    if (pos >= text.size())
        return text.size();
    ++pos;
    while (pos < text.size() && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80)
        ++pos;
    return pos;
}
} // namespace

void RenameEditor::begin_space(int space_id, std::string initial_text)
{
    target_ = RenameTarget::Space;
    space_id_ = space_id;
    tab_id_ = -1;
    leaf_id_ = kInvalidLeaf;
    buffer_ = std::move(initial_text);
    cursor_ = buffer_.size();
    touch();
}

void RenameEditor::begin_tab(int tab_id, std::string initial_text)
{
    target_ = RenameTarget::Tab;
    space_id_ = -1;
    tab_id_ = tab_id;
    leaf_id_ = kInvalidLeaf;
    buffer_ = std::move(initial_text);
    cursor_ = buffer_.size();
    touch();
}

void RenameEditor::begin_pane(LeafId leaf_id, std::string initial_text)
{
    target_ = RenameTarget::Pane;
    space_id_ = -1;
    tab_id_ = -1;
    leaf_id_ = leaf_id;
    buffer_ = std::move(initial_text);
    cursor_ = buffer_.size();
    touch();
}

bool RenameEditor::active() const
{
    return target_ != RenameTarget::None;
}

bool RenameEditor::editing_space() const
{
    return target_ == RenameTarget::Space;
}

bool RenameEditor::editing_tab() const
{
    return target_ == RenameTarget::Tab;
}

bool RenameEditor::editing_pane() const
{
    return target_ == RenameTarget::Pane;
}

int RenameEditor::space_id() const
{
    return editing_space() ? space_id_ : -1;
}

int RenameEditor::tab_id() const
{
    return editing_tab() ? tab_id_ : -1;
}

LeafId RenameEditor::leaf_id() const
{
    return editing_pane() ? leaf_id_ : kInvalidLeaf;
}

RenameSnapshot RenameEditor::snapshot() const
{
    return { target_, space_id_, tab_id_, leaf_id_, buffer_, cursor_, started_at_ };
}

bool RenameEditor::insert(std::string_view utf8)
{
    if (!active())
        return false;
    if (!utf8.empty())
    {
        buffer_.insert(cursor_, utf8);
        cursor_ += utf8.size();
        touch();
    }
    return true;
}

std::optional<RenameCommit> RenameEditor::handle_key(RenameKey key)
{
    if (!active())
        return std::nullopt;

    switch (key)
    {
    case RenameKey::Enter:
        return commit();
    case RenameKey::Escape:
        cancel();
        return std::nullopt;
    case RenameKey::Backspace:
        if (cursor_ > 0)
        {
            const size_t prev = utf8_prev(buffer_, cursor_);
            buffer_.erase(prev, cursor_ - prev);
            cursor_ = prev;
            touch();
        }
        break;
    case RenameKey::Delete:
        if (cursor_ < buffer_.size())
        {
            const size_t next = utf8_next(buffer_, cursor_);
            buffer_.erase(cursor_, next - cursor_);
            touch();
        }
        break;
    case RenameKey::Left:
        cursor_ = utf8_prev(buffer_, cursor_);
        touch();
        break;
    case RenameKey::Right:
        cursor_ = utf8_next(buffer_, cursor_);
        touch();
        break;
    case RenameKey::Home:
        cursor_ = 0;
        touch();
        break;
    case RenameKey::End:
        cursor_ = buffer_.size();
        touch();
        break;
    case RenameKey::Other:
        break;
    }
    return std::nullopt;
}

std::optional<RenameCommit> RenameEditor::commit()
{
    if (!active())
        return std::nullopt;
    RenameCommit result{ target_, space_id_, tab_id_, leaf_id_, std::move(buffer_) };
    cancel();
    return result;
}

void RenameEditor::cancel()
{
    target_ = RenameTarget::None;
    space_id_ = -1;
    tab_id_ = -1;
    leaf_id_ = kInvalidLeaf;
    buffer_.clear();
    cursor_ = 0;
    started_at_ = {};
}

void RenameEditor::touch()
{
    started_at_ = std::chrono::steady_clock::now();
}

} // namespace draxul
