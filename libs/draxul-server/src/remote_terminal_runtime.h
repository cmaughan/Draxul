#pragma once

#include <draxul/remote_terminal_protocol.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace draxul
{

enum class RemoteTerminalInputResult
{
    Accepted,
    Backpressure,
    Failed,
};

class IRemoteTerminalRuntime
{
public:
    virtual ~IRemoteTerminalRuntime() = default;

    virtual bool ensure_started(std::string& error) = 0;
    virtual bool restart(std::string& error) = 0;
    virtual bool pump() = 0;
    // Must be non-blocking. Implementations may only admit bytes to a bounded
    // queue here; PTY/ConPTY writes belong on a per-terminal writer thread.
    // Backpressure is an ordinary result and must never stall the server state
    // loop or another terminal.
    virtual RemoteTerminalInputResult send_input(
        std::string_view bytes) = 0;
    virtual bool resize(int cols, int rows) = 0;
    virtual bool is_running() const = 0;
    virtual uint64_t process_id() const = 0;
    virtual std::optional<int> exit_code() const = 0;
    virtual uint64_t scrollback_rows() const = 0;
    virtual std::optional<TerminalSemanticSnapshot> scrollback_page(
        uint64_t offset_from_live, size_t max_rows) const = 0;
    virtual std::optional<std::string> take_clipboard_write() = 0;
    virtual TerminalSemanticSnapshot snapshot() const = 0;
    virtual TerminalDirtySnapshot take_delta() = 0;
};

} // namespace draxul
