#include <draxul/markdown/markdown_scroll.h>

#include <algorithm>

namespace draxul::markdown
{

void MarkdownScrollState::set_viewport_height(float height)
{
    viewport_height_ = std::max(0.0f, height);
    clamp_offset();
}

void MarkdownScrollState::set_content_height(float height)
{
    content_height_ = std::max(0.0f, height);
    clamp_offset();
}

void MarkdownScrollState::scroll_pixels(float pixels)
{
    offset_ += pixels;
    clamp_offset();
}

void MarkdownScrollState::line_down(float pixels)
{
    scroll_pixels(std::max(1.0f, pixels));
}

void MarkdownScrollState::line_up(float pixels)
{
    scroll_pixels(-std::max(1.0f, pixels));
}

void MarkdownScrollState::page_down()
{
    scroll_pixels(viewport_height_);
}

void MarkdownScrollState::page_up()
{
    scroll_pixels(-viewport_height_);
}

void MarkdownScrollState::home()
{
    offset_ = 0.0f;
}

void MarkdownScrollState::end()
{
    offset_ = max_offset();
}

void MarkdownScrollState::drag_thumb_to(float track_y, float track_height, float pointer_y, float grab_y)
{
    const Rect thumb = scrollbar_thumb(0.0f, track_y, 1.0f, track_height);
    const float travel = std::max(0.0f, track_height - thumb.height);
    if (!scrollbar_visible() || travel <= 0.0f)
    {
        offset_ = 0.0f;
        return;
    }

    const float thumb_top = pointer_y - grab_y - track_y;
    const float fraction = std::clamp(thumb_top / travel, 0.0f, 1.0f);
    offset_ = max_offset() * fraction;
    clamp_offset();
}

float MarkdownScrollState::offset() const
{
    return offset_;
}

float MarkdownScrollState::max_offset() const
{
    return std::max(0.0f, content_height_ - viewport_height_);
}

bool MarkdownScrollState::scrollbar_visible() const
{
    return content_height_ > viewport_height_ && viewport_height_ > 0.0f;
}

Rect MarkdownScrollState::scrollbar_track(float x, float y, float width, float height) const
{
    return { x, y, width, std::max(0.0f, height) };
}

Rect MarkdownScrollState::scrollbar_thumb(float x, float y, float width, float height) const
{
    const Rect track = scrollbar_track(x, y, width, height);
    if (!scrollbar_visible() || track.height <= 0.0f)
        return { track.x, track.y, track.width, track.height };

    const float visible_fraction = std::clamp(viewport_height_ / content_height_, 0.0f, 1.0f);
    const float thumb_height = track.height * visible_fraction;
    const float travel = std::max(0.0f, track.height - thumb_height);
    const float position_fraction = max_offset() > 0.0f ? offset_ / max_offset() : 0.0f;

    return {
        track.x,
        track.y + travel * std::clamp(position_fraction, 0.0f, 1.0f),
        track.width,
        thumb_height,
    };
}

void MarkdownScrollState::clamp_offset()
{
    offset_ = std::clamp(offset_, 0.0f, max_offset());
}

} // namespace draxul::markdown
