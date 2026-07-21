#pragma once

#include <draxul/window.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace draxul::tests
{

// Shared fake IWindow implementation for use across all test files.
// Provides the union of all capabilities observed across per-file fake
// declarations. Call reset() between test cases to clear recorded state.
class FakeWindow final : public IWindow
{
public:
    bool initialize(const std::string&, int, int) override
    {
        return true;
    }
    void shutdown() override {}
    bool poll_events() override
    {
        if (queued_resize_)
        {
            pixel_w_ = queued_resize_->first;
            pixel_h_ = queued_resize_->second;
            logical_w_ = queued_resize_->first;
            logical_h_ = queued_resize_->second;
            if (on_resize)
                on_resize(WindowResizeEvent{ { pixel_w_, pixel_h_ } });
            queued_resize_.reset();
        }
        if (queued_close_request_)
        {
            queued_close_request_ = false;
            if (on_close_requested)
                on_close_requested();
        }
        return true;
    }
    void* native_handle() override
    {
        return nullptr;
    }
    std::pair<int, int> size_logical() const override
    {
        return { logical_w_, logical_h_ };
    }
    std::pair<int, int> size_pixels() const override
    {
        return { pixel_w_, pixel_h_ };
    }
    float display_ppi() const override
    {
        return display_ppi_;
    }
    void set_title(const std::string& title) override
    {
        last_title_ = title;
    }
    std::string clipboard_text() const override
    {
        return clipboard_;
    }
    bool set_clipboard_text(const std::string& text) override
    {
        clipboard_ = text;
        return true;
    }
    bool open_url(std::string_view url) override
    {
        opened_urls_.emplace_back(url);
        return true;
    }
    void set_text_input_area(int, int, int, int) override {}
    void activate() override {}

    // Configurable state — set by tests to simulate different display configurations.
    int pixel_w_ = 800;
    int pixel_h_ = 600;
    int logical_w_ = 800;
    int logical_h_ = 600;
    float display_ppi_ = 96.0f;

    // Recorded state — read by tests.
    std::string clipboard_;
    std::string last_title_;
    std::vector<std::string> opened_urls_;
    bool queued_close_request_ = false;
    std::optional<std::pair<int, int>> queued_resize_;

    void reset()
    {
        clipboard_.clear();
        last_title_.clear();
        opened_urls_.clear();
        queued_close_request_ = false;
        queued_resize_.reset();
    }

    void queue_close_request()
    {
        queued_close_request_ = true;
    }

    void queue_resize(int pixel_w, int pixel_h)
    {
        queued_resize_ = std::make_pair(pixel_w, pixel_h);
    }
};

} // namespace draxul::tests
