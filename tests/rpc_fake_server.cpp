#include <draxul/mpack_codec.h>

#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
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
    if (!encode_mpack_value(NvimRpc::make_array({
                                NvimRpc::make_uint(1),
                                NvimRpc::make_uint(msgid),
                                error,
                                result,
                            }),
            encoded))
    {
        return false;
    }
    return write_all(encoded);
}

bool send_response_with_raw_msgid(const MpackValue& raw_msgid, const MpackValue& error, const MpackValue& result)
{
    std::vector<char> encoded;
    if (!encode_mpack_value(NvimRpc::make_array({
                                NvimRpc::make_uint(1),
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

int main()
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    const std::string current_mode = mode();
    if (current_mode == "close_stdin_until_release")
    {
        std::fclose(stdin);
        write_marker_file(std::getenv("DRAXUL_RPC_FAKE_READY_FILE"), "ready");
        wait_for_release_file();
        return 0;
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
                NvimRpc::make_array({
                    NvimRpc::make_uint(1),
                    NvimRpc::make_str("oops"),
                    NvimRpc::make_nil(),
                    NvimRpc::make_int(0),
                }),
                malformed_encoded))
        {
            return 7;
        }
        if (!write_all(malformed_encoded))
            return 8;
        return send_response(msgid, NvimRpc::make_nil(), NvimRpc::make_str("ok")) ? 0 : 9;
    }

    if (current_mode == "notify_then_success")
    {
        if (!send_notification("redraw", { NvimRpc::make_array({}) }))
            return 4;
    }

    if (current_mode == "notify_many")
    {
        // Send 100 notifications then a success response.
        // Used by the backpressure stress tests to verify NvimRpc drains all
        // queued items without losing any under concurrent push+drain.
        for (int i = 0; i < 100; ++i)
        {
            if (!send_notification("redraw", { NvimRpc::make_int(static_cast<int64_t>(i)) }))
                return 4;
        }
    }

    if (current_mode == "error")
    {
        return send_response(msgid, NvimRpc::make_str("boom"), NvimRpc::make_nil()) ? 0 : 5;
    }

    if (current_mode == "out_of_range_msgid_then_success")
    {
        // First emit a response whose msgid is a negative int64; the client
        // must discard this (with a warning) rather than silently truncating
        // it to a uint32_t that could collide with an in-flight request.
        if (!send_response_with_raw_msgid(NvimRpc::make_int(-1), NvimRpc::make_nil(), NvimRpc::make_str("poison")))
            return 7;
        // Then emit a well-formed response for the real msgid so the request
        // completes successfully and the test does not need to wait for the
        // 5-second request timeout.
        return send_response(msgid, NvimRpc::make_nil(), NvimRpc::make_str("ok")) ? 0 : 8;
    }

    return send_response(msgid, NvimRpc::make_nil(), NvimRpc::make_str("ok")) ? 0 : 6;
}
