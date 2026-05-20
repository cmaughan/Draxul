#pragma once

namespace draxul::markdown
{

struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

class MarkdownScrollState
{
public:
    void set_viewport_height(float height);
    void set_content_height(float height);
    void scroll_pixels(float pixels);
    void line_down(float pixels);
    void line_up(float pixels);
    void page_down();
    void page_up();
    void home();
    void end();
    void drag_thumb_to(float track_y, float track_height, float pointer_y, float grab_y);

    float offset() const;
    float max_offset() const;
    bool scrollbar_visible() const;

    Rect scrollbar_track(float x, float y, float width, float height) const;
    Rect scrollbar_thumb(float x, float y, float width, float height) const;

private:
    void clamp_offset();

    float viewport_height_ = 0.0f;
    float content_height_ = 0.0f;
    float offset_ = 0.0f;
};

} // namespace draxul::markdown
