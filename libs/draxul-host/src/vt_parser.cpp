#include <draxul/vt_parser.h>

#include <draxul/log.h>
#include <draxul/unicode.h>

namespace draxul
{

namespace
{

bool has_complete_codepoint(std::string_view text, size_t offset)
{
    if (offset >= text.size())
        return false;

    const int length = utf8_sequence_length(static_cast<uint8_t>(text[offset]));
    if (offset + static_cast<size_t>(length) > text.size())
        return false;

    for (int i = 1; i < length; ++i)
    {
        const auto byte = static_cast<uint8_t>(text[offset + static_cast<size_t>(i)]);
        if ((byte & 0xC0) != 0x80)
            return false;
    }
    return true;
}

std::optional<std::string> consume_cluster(std::string_view text, size_t& offset)
{
    if (!has_complete_codepoint(text, offset))
        return std::nullopt;

    const size_t start = offset;
    uint32_t cp = 0;
    utf8_decode_next(text, offset, cp);
    bool expect_joined = false;

    while (offset < text.size())
    {
        if (!has_complete_codepoint(text, offset))
            break;

        const size_t next_start = offset;
        uint32_t next = 0;
        utf8_decode_next(text, offset, next);
        const bool keep = next == 0x200D || next == 0xFE0F || is_width_ignorable(next) || is_emoji_modifier(next) || expect_joined;
        expect_joined = next == 0x200D;
        if (!keep)
        {
            offset = next_start;
            break;
        }
    }

    return std::string(text.substr(start, offset - start));
}

} // namespace

VtParser::VtParser(Callbacks cbs)
    : cbs_(std::move(cbs))
{
}

void VtParser::feed(std::string_view bytes)
{
    for (char ch : bytes)
    {
        switch (state_)
        {
        case State::Ground:
            if (ch == '\x1B')
            {
                flush_plain_text();
                state_ = State::Escape;
            }
            else if (static_cast<unsigned char>(ch) < 0x20)
            {
                flush_plain_text();
                cbs_.on_control(ch);
            }
            else
            {
                if (plain_text_.size() >= kMaxPlainTextBuffer)
                {
                    DRAXUL_LOG_WARN(LogCategory::App,
                        "vt_parser: plain_text buffer exceeded cap (%zu bytes); flushing",
                        kMaxPlainTextBuffer);
                    plain_text_.clear();
                }
                plain_text_.push_back(ch);
                flush_plain_text();
            }
            break;
        case State::Escape:
            if (ch == '[')
            {
                csi_buffer_.clear();
                state_ = State::Csi;
            }
            else if (ch == ']')
            {
                osc_buffer_.clear();
                state_ = State::Osc;
            }
            else
            {
                if (cbs_.on_esc)
                    cbs_.on_esc(ch);
                state_ = State::Ground;
            }
            break;
        case State::Csi:
            if (ch >= 0x40 && ch <= 0x7E)
            {
                cbs_.on_csi(ch, csi_buffer_);
                state_ = State::Ground;
            }
            else
            {
                if (csi_buffer_.size() >= kMaxCsiBuffer)
                {
                    DRAXUL_LOG_WARN(LogCategory::App,
                        "vt_parser: CSI buffer exceeded cap (%zu bytes); dropping sequence",
                        kMaxCsiBuffer);
                    csi_buffer_.clear();
                    state_ = State::Ground;
                }
                else
                {
                    csi_buffer_.push_back(ch);
                }
            }
            break;
        case State::Osc:
            if (ch == '\a')
            {
                cbs_.on_osc(osc_buffer_);
                state_ = State::Ground;
            }
            else if (ch == '\x1B')
            {
                state_ = State::OscEsc;
            }
            else
            {
                if (osc_buffer_.size() >= kMaxOscBuffer)
                {
                    DRAXUL_LOG_WARN(LogCategory::App,
                        "vt_parser: OSC buffer exceeded cap (%zu bytes); dropping sequence",
                        kMaxOscBuffer);
                    osc_buffer_.clear();
                    state_ = State::Ground;
                }
                else
                {
                    osc_buffer_.push_back(ch);
                }
            }
            break;
        case State::OscEsc:
            if (ch == '\\')
                cbs_.on_osc(osc_buffer_);
            state_ = State::Ground;
            break;
        }
    }
}

void VtParser::reset()
{
    state_ = State::Ground;
    plain_text_.clear();
    csi_buffer_.clear();
    osc_buffer_.clear();
}

void VtParser::flush_plain_text()
{
    size_t offset = 0;
    while (offset < plain_text_.size())
    {
        const size_t before = offset;
        auto cluster = consume_cluster(plain_text_, offset);
        if (!cluster)
            break;
        cbs_.on_cluster(*cluster);
        if (offset == before)
            break;
    }
    if (offset > 0)
        plain_text_.erase(0, offset);
}

} // namespace draxul
