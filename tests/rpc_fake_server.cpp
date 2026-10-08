#include <draxul/mpack_codec.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

using namespace draxul;

namespace
{

std::string mode()
{
    if (const char* value = std::getenv("DRAXUL_RPC_FAKE_MODE"))
        return value;
    return "success";
}

void write_marker_file(const char* path, const char* text)
{
    if (!path || path[0] == '\0')
        return;

    if (FILE* out = std::fopen(path, "wb"))
    {
        std::fputs(text, out);
        std::fclose(out);
    }
}

#ifdef _WIN32
std::string utf8(std::wstring_view text)
{
    if (text.empty())
        return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), count, nullptr, nullptr);
    return result;
}

void dump_launch_values(int argc, wchar_t** argv)
{
    const char* target = std::getenv("DRAXUL_RPC_FAKE_LAUNCH_DUMP");
    if (!target)
        return;
    FILE* out = std::fopen(target, "wb");
    if (!out)
        return;
    std::wstring cwd(MAX_PATH, L'\0');
    DWORD length = GetCurrentDirectoryW(static_cast<DWORD>(cwd.size()), cwd.data());
    if (length >= cwd.size())
    {
        cwd.resize(length + 1);
        length = GetCurrentDirectoryW(static_cast<DWORD>(cwd.size()), cwd.data());
    }
    cwd.resize(length);
    const std::string cwd_utf8 = utf8(cwd);
    std::fprintf(out, "cwd=%s\n", cwd_utf8.c_str());
    for (int i = 0; i < argc; ++i)
    {
        const std::string argument = utf8(argv[i]);
        std::fprintf(out, "arg%d=%s\n", i, argument.c_str());
    }
    wchar_t unicode[128]{};
    const DWORD unicode_length = GetEnvironmentVariableW(
        L"DRAXUL_RPC_FAKE_UNICODE", unicode, 128);
    const std::string value = unicode_length < 128
        ? utf8(std::wstring_view(unicode, unicode_length)) : std::string{};
    std::fprintf(out, "env=%s\n", value.c_str());
    std::fclose(out);
}
#endif

bool marker_file_exists(const char* path)
{
    if (!path || path[0] == '\0')
        return false;

    if (FILE* in = std::fopen(path, "rb"))
    {
        std::fclose(in);
        return true;
    }
    return false;
}

void wait_for_release_file()
{
    const char* path = std::getenv("DRAXUL_RPC_FAKE_RELEASE_FILE");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!marker_file_exists(path) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

bool write_all(const std::vector<char>& bytes)
{
    size_t written = 0;
    while (written < bytes.size())
    {
        size_t chunk = std::fwrite(bytes.data() + written, 1, bytes.size() - written, stdout);
        if (chunk == 0)
            return false;
        written += chunk;
    }
    return std::fflush(stdout) == 0;
}

bool wait_for_request_byte()
{
    return std::fgetc(stdin) != EOF;
}

bool send_notification(const std::string& method, const std::vector<MpackValue>& params)
{
    std::vector<char> encoded;
    if (!encode_rpc_notification(method, params, encoded))
        return false;
    return write_all(encoded);
}

bool send_response(uint32_t msgid, const MpackValue& error, const MpackValue& result)
{
    std::vector<char> encoded;
    if (!encode_mpack_value(MpackValue::make_array({
                                MpackValue::make_uint(1),
                                MpackValue::make_uint(msgid),
                                error,
                                result,
                            }),
            encoded))
    {
        return false;
    }
    return write_all(encoded);
}

int read_stdin_chunk(uint8_t* buffer, size_t size)
{
#ifdef _WIN32
    return _read(0, buffer, static_cast<unsigned>(size));
#else
    return static_cast<int>(::read(0, buffer, size));
#endif
}

// Echo every client notification as ["echo", [sequence, payload_bytes]] in
// arrival order until stdin closes. sequence is params[0]; payload_bytes is
// the length of a string params[1], or zero.
int echo_notifications_until_eof()
{
    std::vector<uint8_t> pending;
    std::vector<uint8_t> chunk(64 * 1024);
    while (true)
    {
        const int n = read_stdin_chunk(chunk.data(), chunk.size());
        if (n <= 0)
            return 0;
        pending.insert(pending.end(), chunk.begin(), chunk.begin() + n);
        size_t offset = 0;
        while (offset < pending.size())
        {
            MpackValue message;
            size_t consumed = 0;
            const std::span<const uint8_t> remaining(pending.data() + offset, pending.size() - offset);
            if (!decode_mpack_value(remaining, message, &consumed) || consumed == 0)
                break;
            offset += consumed;
            if (message.type() != MpackValue::Array || message.as_array().size() < 3
                || message.as_array()[0].as_int() != 2)
                continue;
            const auto& params = message.as_array()[2];
            int64_t sequence = -1;
            int64_t payload_bytes = 0;
            if (params.type() == MpackValue::Array && !params.as_array().empty())
            {
                sequence = params.as_array()[0].as_int();
                if (params.as_array().size() > 1 && params.as_array()[1].type() == MpackValue::String)
                    payload_bytes = static_cast<int64_t>(params.as_array()[1].as_str().size());
            }
            if (!send_notification("echo", { MpackValue::make_int(sequence), MpackValue::make_int(payload_bytes) }))
                return 11;
        }
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(offset));
    }
}

bool send_response_with_raw_msgid(const MpackValue& raw_msgid, const MpackValue& error, const MpackValue& result)
{
    std::vector<char> encoded;
    if (!encode_mpack_value(MpackValue::make_array({
                                MpackValue::make_uint(1),
                                raw_msgid,
                                error,
                                result,
                            }),
            encoded))
    {
        return false;
    }
    return write_all(encoded);
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main()
#endif
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    const std::string current_mode = mode();
#ifndef _WIN32
    if (current_mode == "dump_sigpipe_and_exit")
    {
        struct sigaction action{};
        sigset_t mask;
        if (sigaction(SIGPIPE, nullptr, &action) != 0
            || sigprocmask(SIG_SETMASK, nullptr, &mask) != 0)
            return 10;
        write_marker_file(std::getenv("DRAXUL_RPC_FAKE_READY_FILE"),
            action.sa_handler == SIG_DFL && sigismember(&mask, SIGPIPE) == 0
                ? "default unblocked" : "incorrect inherited policy");
        return 0;
    }
#endif
#ifdef _WIN32
    if (current_mode == "dump_launch_and_exit")
    {
        dump_launch_values(argc, argv);
        return 0;
    }
#endif
    if (current_mode == "close_stdin_until_release")
    {
        std::fclose(stdin);
        write_marker_file(std::getenv("DRAXUL_RPC_FAKE_READY_FILE"), "ready");
        wait_for_release_file();
        return 0;
    }

    if (current_mode == "stall_then_echo")
    {
        // A child that stops reading its input: the client's writes fill the
        // pipe. After release, consume everything and echo it back in order.
        write_marker_file(std::getenv("DRAXUL_RPC_FAKE_READY_FILE"), "ready");
        const char* release = std::getenv("DRAXUL_RPC_FAKE_RELEASE_FILE");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!marker_file_exists(release) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return echo_notifications_until_eof();
    }

    if (current_mode == "dump_term_and_exit")
    {
        if (const char* path = std::getenv("DRAXUL_RPC_FAKE_TERM_DUMP"))
        {
            if (FILE* out = std::fopen(path, "wb"))
            {
                const char* term = std::getenv("TERM");
                if (term != nullptr)
                    std::fputs(term, out);
                std::fclose(out);
            }
        }
        return 0;
    }

    if (current_mode == "unresponsive")
    {
        // Ignore pipe closure long enough to force the Windows process owner
        // through its bounded background wait and termination path.
        std::this_thread::sleep_for(std::chrono::seconds(30));
        return 0;
    }

    if (!wait_for_request_byte())
        return 2;
    constexpr uint32_t msgid = 1;

    if (current_mode == "abort_after_read")
        return 0;

    if (current_mode == "hang")
    {
        // Read and discard stdin until the client closes the pipe.
        // Simulates a server that received the request but never responds.
        while (std::fgetc(stdin) != EOF)
        {
        }
        return 0;
    }

    if (current_mode == "malformed_response")
    {
        static const char malformed[] = { char(0x91), char(0xC1) };
        std::fwrite(malformed, 1, sizeof(malformed), stdout);
        std::fflush(stdout);
        return 0;
    }

    if (current_mode == "malformed_dispatch_then_success")
    {
        // WI 05: structurally valid msgpack that crashes the typed dispatcher.
        // [1, "oops", nil, 0] — type=1 means "response" so msg_array[1] should
        // be an integer msgid; we send a string instead. The reader must
        // survive this and still deliver the real response that follows.
        std::vector<char> malformed_encoded;
        if (!encode_mpack_value(
                MpackValue::make_array({
                    MpackValue::make_uint(1),
                    MpackValue::make_str("oops"),
                    MpackValue::make_nil(),
                    MpackValue::make_int(0),
                }),
                malformed_encoded))
        {
            return 7;
        }
        if (!write_all(malformed_encoded))
            return 8;
        return send_response(msgid, MpackValue::make_nil(), MpackValue::make_str("ok")) ? 0 : 9;
    }

    if (current_mode == "notify_then_success")
    {
        if (!send_notification("redraw", { MpackValue::make_array({}) }))
            return 4;
    }

    if (current_mode == "notify_many")
    {
        // Send 100 notifications then a success response.
        // Used by the backpressure stress tests to verify NvimRpc drains all
        // queued items without losing any under concurrent push+drain.
        for (int i = 0; i < 100; ++i)
        {
            if (!send_notification("redraw", { MpackValue::make_int(static_cast<int64_t>(i)) }))
                return 4;
        }
    }

    if (current_mode == "notify_burst")
    {
        // More notifications than the client queue holds, then the response.
        // The client must pause reading rather than discard any of them.
        int count = 5000;
        if (const char* value = std::getenv("DRAXUL_RPC_FAKE_NOTIFY_COUNT"))
            count = std::atoi(value);
        for (int i = 0; i < count; ++i)
        {
            if (!send_notification("redraw", { MpackValue::make_int(static_cast<int64_t>(i)) }))
                return 4;
        }
    }

    if (current_mode == "error")
    {
        return send_response(msgid, MpackValue::make_str("boom"), MpackValue::make_nil()) ? 0 : 5;
    }

    if (current_mode == "out_of_range_msgid_then_success")
    {
        // First emit a response whose msgid is a negative int64; the client
        // must discard this (with a warning) rather than silently truncating
        // it to a uint32_t that could collide with an in-flight request.
        if (!send_response_with_raw_msgid(MpackValue::make_int(-1), MpackValue::make_nil(), MpackValue::make_str("poison")))
            return 7;
        // Then emit a well-formed response for the real msgid so the request
        // completes successfully and the test does not need to wait for the
        // 5-second request timeout.
        return send_response(msgid, MpackValue::make_nil(), MpackValue::make_str("ok")) ? 0 : 8;
    }

    return send_response(msgid, MpackValue::make_nil(), MpackValue::make_str("ok")) ? 0 : 6;
}
