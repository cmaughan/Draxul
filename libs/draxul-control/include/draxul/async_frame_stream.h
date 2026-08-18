#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>

namespace draxul
{

struct AsyncFrameStreamError
{
    std::string code;
    std::string message;
    uint32_t native_code = 0;
};

// A length-prefixed local stream. Exactly one reader and one writer may use a
// connection concurrently. close() is safe from a third thread and promptly
// interrupts both directions; multiple simultaneous readers or writers are
// not supported.
class AsyncFrameStreamConnection
{
public:
    ~AsyncFrameStreamConnection();
    AsyncFrameStreamConnection(AsyncFrameStreamConnection&&) noexcept;
    AsyncFrameStreamConnection& operator=(
        AsyncFrameStreamConnection&&) noexcept;
    AsyncFrameStreamConnection(const AsyncFrameStreamConnection&) = delete;
    AsyncFrameStreamConnection& operator=(
        const AsyncFrameStreamConnection&) = delete;

    bool read_frame(std::string& bytes, std::stop_token stop_token,
        AsyncFrameStreamError& error);
    bool write_frame(std::string_view bytes, std::stop_token stop_token,
        AsyncFrameStreamError& error);
    void close();
    bool connected() const;

private:
    class Impl;
    explicit AsyncFrameStreamConnection(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;

    friend class AsyncFrameStreamClient;
    friend class AsyncFrameStreamListener;
};

class AsyncFrameStreamClient
{
public:
    static std::unique_ptr<AsyncFrameStreamConnection> connect(
        std::string_view endpoint, std::chrono::milliseconds timeout,
        AsyncFrameStreamError& error);
};

// One acceptor per owning service. Accepted connections are independent and
// may be handed to their own reader/writer workers.
class AsyncFrameStreamListener
{
public:
    AsyncFrameStreamListener();
    ~AsyncFrameStreamListener();
    AsyncFrameStreamListener(const AsyncFrameStreamListener&) = delete;
    AsyncFrameStreamListener& operator=(const AsyncFrameStreamListener&) = delete;

    bool start(std::string_view stream_id,
        const std::filesystem::path& runtime_directory,
        AsyncFrameStreamError& error);
    std::unique_ptr<AsyncFrameStreamConnection> accept(
        std::stop_token stop_token, AsyncFrameStreamError& error);
    const std::string& endpoint() const;
    void stop();
    bool running() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace draxul
