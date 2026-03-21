#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace draxul
{

// Character-by-character VT/ANSI escape-sequence parser.
//
// Call feed() with raw bytes from the process; the parser drives callbacks for
// each decoded action.  No grid or rendering state lives here — the caller
// provides callbacks that perform the actual terminal actions.
//
// Supported sequences:
//   - Printable UTF-8 grapheme clusters  → on_cluster
//   - C0 control characters             → on_control
//   - CSI sequences (ESC [ … final)     → on_csi
//   - OSC sequences (ESC ] … BEL/ST)    → on_osc
//
// Buffer caps: each accumulation buffer is bounded to prevent OOM from
// pathological terminal streams.  When a cap is reached the in-progress
// sequence is dropped and the parser resets to Ground.
class VtParser
{
public:
    static constexpr size_t kMaxPlainTextBuffer = 64 * 1024; // 64 KiB
    static constexpr size_t kMaxCsiBuffer = 4096; // 4 KiB
    static constexpr size_t kMaxOscBuffer = 8192; // 8 KiB

    struct Callbacks
    {
        // A complete UTF-8 grapheme cluster ready to be written.
        std::function<void(const std::string& cluster)> on_cluster;
        // A C0 control character (< 0x20, but not ESC).
        std::function<void(char ch)> on_control;
        // A complete CSI sequence: final byte + body (params + intermediate).
        std::function<void(char final_char, std::string_view body)> on_csi;
        // A complete OSC sequence (everything between ESC ] and BEL/ST).
        std::function<void(std::string_view body)> on_osc;
        // A two-character ESC sequence where the second byte is not '[' or ']'
        // (e.g. ESC 7 = DECSC, ESC 8 = DECRC).  Optional — unhandled if null.
        std::function<void(char ch)> on_esc;
    };

    explicit VtParser(Callbacks cbs);

    // Feed raw bytes from the terminal process into the parser.
    void feed(std::string_view bytes);

    // Reset parser state (clear mid-sequence buffers, return to Ground).
    void reset();

private:
    enum class State
    {
        Ground,
        Escape,
        Csi,
        Osc,
        OscEsc,
    };

    void flush_plain_text();

    Callbacks cbs_;
    State state_ = State::Ground;
    std::string plain_text_; // accumulates printable bytes in Ground state
    std::string csi_buffer_;
    std::string osc_buffer_;
};

} // namespace draxul
