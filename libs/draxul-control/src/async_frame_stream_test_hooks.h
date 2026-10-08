#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace draxul::control_detail
{

#ifdef _WIN32
// Private deterministic seam for Windows Session stream accept tests.
// Production code installs no hooks, so accept() calls the OS directly.
struct AsyncFrameStreamAcceptTestHooks
{
    // Runs after the pipe instance exists and before ConnectNamedPipe, so a
    // test can connect and disconnect a real client in that window.
    std::function<void(const std::string& endpoint)> before_connect;
    // Replaces a ConnectNamedPipe call with this synchronous failure code.
    std::function<std::optional<uint32_t>()> fail_connect;
};

// Installs process-wide hooks; pass nullptr to remove them. The caller owns
// the hooks and must keep them alive until they are removed.
void set_async_frame_stream_accept_test_hooks(
    const AsyncFrameStreamAcceptTestHooks* hooks);
#endif

} // namespace draxul::control_detail
