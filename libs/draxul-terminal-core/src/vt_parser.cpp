#include <draxul/vt_parser.h>

#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/unicode.h>

namespace draxul
{

namespace
{

enum class CodepointState
{
    Valid,
    Invalid,
    Incomplete,
};

CodepointState codepoint_state(std::string_view text, size_t offset)
{
    if (offset >= text.size())
        return CodepointState::Incomplete;

    const auto lead = static_cast<uint8_t>(text[offset]);
    const int length = utf8_sequence_length(static_cast<uint8_t>(text[offset]));
    if (length == 1)
        return lead < 0x80 ? CodepointState::Valid : CodepointState::Invalid;

    if (offset + static_cast<size_t>(length) > text.size())
    {
        // Preserve a valid prefix so a codepoint split across PTY reads can be
        // completed by the next feed. A non-continuation byte proves the
        // sequence invalid immediately.
        for (size_t i = offset + 1; i < text.size(); ++i)
        {
            if ((static_cast<uint8_t>(text[i]) & 0xC0) != 0x80)
                return CodepointState::Invalid;
        }
        return CodepointState::Incomplete;
    }

    size_t next_offset = offset;
    return utf8_codepoint_at_is_valid(text, offset, next_offset)
            && next_offset == offset + static_cast<size_t>(length)
        ? CodepointState::Valid
        : CodepointState::Invalid;
}

struct DecodedCodepoint
{
    uint32_t value = 0;
    std::string text;
};

std::optional<DecodedCodepoint> consume_codepoint(
    std::string_view text, size_t& offset)
{
    static constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

    const CodepointState state = codepoint_state(text, offset);
    if (state == CodepointState::Incomplete)
        return std::nullopt;
    if (state == CodepointState::Invalid)
    {
        ++offset;
        return DecodedCodepoint{
            .value = 0xFFFD,
            .text = std::string(kReplacement),
        };
    }

    const size_t start = offset;
    uint32_t cp = 0;
    utf8_decode_next(text, offset, cp);
    return DecodedCodepoint{
        .value = cp,
        .text = std::string(text.substr(start, offset - start)),
    };
}

std::optional<std::string> consume_cluster(std::string_view text, size_t& offset)
{
    PERF_MEASURE();
    auto decoded = consume_codepoint(text, offset);
    if (!decoded)
        return std::nullopt;

    std::string cluster = std::move(decoded->text);
    bool expect_joined = false;

    while (offset < text.size())
    {
        const size_t next_start = offset;
        auto next_codepoint = consume_codepoint(text, offset);
        if (!next_codepoint)
            break;
        const uint32_t next = next_codepoint->value;
        const bool keep = next == 0x200D || next == 0xFE0F || is_width_ignorable(next) || is_emoji_modifier(next) || expect_joined;
        expect_joined = next == 0x200D;
        if (!keep)
        {
            offset = next_start;
            break;
        }
        cluster += next_codepoint->text;
    }

    return cluster;
}

} // namespace

VtParser::VtParser(Callbacks cbs)
    : cbs_(std::move(cbs))
{
}

void VtParser::feed(std::string_view bytes)
{
    PERF_MEASURE();
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
                    flush_plain_text();
                }
                plain_text_.push_back(ch);
            }
            break;
        case State::Escape:
            dispatch_escape_followup(ch);
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
            {
                cbs_.on_osc(osc_buffer_);
                state_ = State::Ground;
            }
            else
            {
                // The ESC we consumed was not the start of a String Terminator
                // (ESC \).  Treat it as an implicit OSC terminator, then
                // re-dispatch the current character as the start of a new
                // escape sequence rather than silently dropping it.
                cbs_.on_osc(osc_buffer_);
                dispatch_escape_followup(ch);
            }
            break;
        case State::Dcs:
            if (ch == '\x1B')
            {
                state_ = State::DcsEsc;
            }
            else if (ch == '\x18' || ch == '\x1A') // CAN / SUB cancel the control string
            {
                dcs_buffer_.clear();
                state_ = State::Ground;
            }
            else if (dcs_buffer_.size() >= kMaxDcsBuffer)
            {
                DRAXUL_LOG_WARN(LogCategory::App,
                    "vt_parser: DCS buffer exceeded cap (%zu bytes); dropping sequence",
                    kMaxDcsBuffer);
                dcs_buffer_.clear();
                state_ = State::DcsIgnore;
            }
            else
            {
                dcs_buffer_.push_back(ch);
            }
            break;
        case State::DcsEsc:
            if (ch == '\\')
            {
                if (cbs_.on_dcs)
                    cbs_.on_dcs(dcs_buffer_);
                dcs_buffer_.clear();
                state_ = State::Ground;
            }
            else
            {
                // ESC not followed by ST aborts the DCS and begins a new
                // escape sequence. Never render the partial DCS payload.
                dcs_buffer_.clear();
                dispatch_escape_followup(ch);
            }
            break;
        case State::DcsIgnore:
            if (ch == '\x1B')
                state_ = State::DcsIgnoreEsc;
            else if (ch == '\x18' || ch == '\x1A')
                state_ = State::Ground;
            break;
        case State::DcsIgnoreEsc:
            if (ch == '\\')
                state_ = State::Ground;
            else if (ch != '\x1B')
                state_ = State::DcsIgnore;
            break;
        }
    }

    // Flush any remaining plain text accumulated during this feed() call.
    // This ensures we emit clusters at end-of-input rather than waiting for
    // the next control/escape character — and avoids the O(K^2) cost of
    // flushing after every single byte in the Ground state.
    flush_plain_text();
}

void VtParser::reset()
{
    PERF_MEASURE();
    state_ = State::Ground;
    plain_text_.clear();
    csi_buffer_.clear();
    osc_buffer_.clear();
    dcs_buffer_.clear();
}

void VtParser::dispatch_escape_followup(char ch)
{
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
    else if (ch == 'P')
    {
        dcs_buffer_.clear();
        state_ = State::Dcs;
    }
    else
    {
        if (cbs_.on_esc)
            cbs_.on_esc(ch);
        state_ = State::Ground;
    }
}

void VtParser::flush_plain_text()
{
    PERF_MEASURE();
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
