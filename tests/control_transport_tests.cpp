#include <catch2/catch_all.hpp>

#include "support/control_test_support.h"

#include "control_codec.h"
#include "control_deadline.h"
#include "control_transport.h"

#include <draxul/control_plane.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <aclapi.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif

using namespace draxul;
using namespace draxul::tests;

namespace
{

#ifdef _WIN32
bool current_user_only_windows_dacl(
    const std::filesystem::path& path,
    bool require_child_inheritance = false)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    DWORD token_bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &token_bytes);
    std::vector<BYTE> token_buffer(token_bytes);
    const bool token_read = token_bytes != 0
        && GetTokenInformation(token, TokenUser, token_buffer.data(),
            token_bytes, &token_bytes);
    CloseHandle(token);
    if (!token_read)
        return false;
    const auto* token_user
        = reinterpret_cast<const TOKEN_USER*>(token_buffer.data());

    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD result = GetNamedSecurityInfoW(
        const_cast<wchar_t*>(path.c_str()),
        SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
        nullptr, nullptr, &dacl, nullptr, &descriptor);
    if (result != ERROR_SUCCESS
        || descriptor == nullptr || dacl == nullptr)
    {
        if (descriptor)
            LocalFree(descriptor);
        return false;
    }
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    bool secure = GetSecurityDescriptorControl(
                      descriptor, &control, &revision)
        && (control & SE_DACL_PROTECTED) != 0;
    bool current_user_allowed = false;
    bool current_user_inherits = false;
    ACL_SIZE_INFORMATION information{};
    secure = secure && GetAclInformation(
                           dacl, &information, sizeof(information),
                           AclSizeInformation);
    for (DWORD index = 0;
        secure && index < information.AceCount; ++index)
    {
        void* encoded = nullptr;
        if (!GetAce(dacl, index, &encoded))
        {
            secure = false;
            break;
        }
        auto* header = static_cast<ACE_HEADER*>(encoded);
        PSID sid = nullptr;
        if (header->AceType == ACCESS_ALLOWED_ACE_TYPE)
        {
            sid = &reinterpret_cast<ACCESS_ALLOWED_ACE*>(encoded)->SidStart;
        }
        if (!sid)
            continue;
        if (EqualSid(sid, token_user->User.Sid))
        {
            current_user_allowed = true;
            current_user_inherits = current_user_inherits
                || ((header->AceFlags & OBJECT_INHERIT_ACE) != 0
                    && (header->AceFlags & CONTAINER_INHERIT_ACE) != 0);
        }
        else if (!IsWellKnownSid(sid, WinLocalSystemSid))
        {
            secure = false;
        }
    }
    secure = secure && current_user_allowed
        && (!require_child_inheritance || current_user_inherits);
    LocalFree(descriptor);
    return secure;
}
#endif

} // namespace

TEST_CASE("control codec preserves frame byte order and structural limits",
    "[control][transport][codec]")
{
    using namespace draxul::control_detail;

    const std::array<uint8_t, 4> encoded{
        0x78, 0x56, 0x34, 0x12
    };
    CHECK(frame_prefix(0x12345678) == encoded);
    CHECK(frame_size(encoded) == 0x12345678);

    const std::string depth_limit(kControlMaxJsonDepth, '[');
    CHECK(depth_within_limit(
        depth_limit + std::string(kControlMaxJsonDepth, ']')));
    CHECK_FALSE(depth_within_limit(
        depth_limit + "[" + std::string(kControlMaxJsonDepth + 1, ']')));
    CHECK(depth_within_limit(R"json({"text":"[{}]"})json"));
    CHECK_FALSE(depth_within_limit("]"));
    CHECK_FALSE(depth_within_limit(R"({"unterminated})"));
}

TEST_CASE("control request and response codecs retain public validation",
    "[control][transport][codec]")
{
    using namespace draxul::control_detail;

    const std::string token(64, 'a');
    ControlRequest request;
    const auto valid = parse_request(
        encode_request("request-1", token, "system.hello",
            { { "probe", true } }, std::chrono::milliseconds(25)),
        token, request);
    REQUIRE(valid.ok);
    CHECK(request.id == "request-1");
    CHECK(request.method == "system.hello");
    CHECK(request.params["probe"] == true);

    ControlRequest correlated;
    const auto unauthenticated = parse_request(
        encode_request("correlate-me", std::string(64, 'b'),
            "system.hello", nlohmann::json::object(),
            std::chrono::milliseconds(25)),
        token, correlated);
    CHECK_FALSE(unauthenticated.ok);
    CHECK(unauthenticated.error_code == "authentication_failed");
    CHECK(correlated.id == "correlate-me");

    const auto malformed = parse_response("{broken", "request-1");
    CHECK_FALSE(malformed.ok);
    CHECK(malformed.error_code == "invalid_response");
    const auto mismatched = parse_response(
        response_json("another-request",
            ControlMethodResult::success(true))
            .dump(),
        "request-1");
    CHECK_FALSE(mismatched.ok);
    CHECK(mismatched.error_code == "invalid_response");
}

TEST_CASE("control frame codec preserves typed failure stages",
    "[control][transport][codec][staged-error]")
{
    using namespace draxul::control_detail;

    std::vector<uint8_t> wire;
    std::vector<TransportStage> write_stages;
    const auto written = write_control_frame(
        [&](const void* data, size_t size, TransportStage stage) {
            write_stages.push_back(stage);
            const auto* bytes = static_cast<const uint8_t*>(data);
            wire.insert(wire.end(), bytes, bytes + size);
            return TransportStatus::success();
        },
        "payload");
    REQUIRE(written.ok);
    REQUIRE(write_stages.size() == 2);
    CHECK(write_stages[0] == TransportStage::WritePrefix);
    CHECK(write_stages[1] == TransportStage::WritePayload);

    size_t offset = 0;
    std::vector<TransportStage> read_stages;
    std::string decoded;
    const auto read = read_control_frame(
        [&](void* data, size_t size, TransportStage stage) {
            read_stages.push_back(stage);
            REQUIRE(offset + size <= wire.size());
            std::memcpy(data, wire.data() + offset, size);
            offset += size;
            return TransportStatus::success();
        },
        decoded);
    REQUIRE(read.ok);
    CHECK(decoded == "payload");
    CHECK(offset == wire.size());
    REQUIRE(read_stages.size() == 2);
    CHECK(read_stages[0] == TransportStage::ReadPrefix);
    CHECK(read_stages[1] == TransportStage::ReadPayload);

    int calls = 0;
    const auto staged_failure = write_control_frame(
        [&](const void*, size_t, TransportStage stage) {
            ++calls;
            return TransportStatus::failure({
                .stage = stage,
                .domain = NativeDomain::Win32,
                .native_code = 995,
                .classification = FailureClass::DeadlineExceeded,
                .message = "cancelled",
            });
        },
        "payload");
    CHECK_FALSE(staged_failure.ok);
    CHECK(calls == 1);
    CHECK(staged_failure.error.stage == TransportStage::WritePrefix);
    CHECK(staged_failure.error.domain == NativeDomain::Win32);
    CHECK(staged_failure.error.native_code == 995);
    CHECK(staged_failure.error.classification
        == FailureClass::DeadlineExceeded);

    int oversized_reads = 0;
    std::string oversized;
    const auto oversized_status = read_control_frame(
        [&](void* data, size_t size, TransportStage stage) {
            ++oversized_reads;
            REQUIRE(stage == TransportStage::ReadPrefix);
            REQUIRE(size == 4);
            const auto prefix = frame_prefix(kControlMaxMessageBytes + 1);
            std::memcpy(data, prefix.data(), prefix.size());
            return TransportStatus::success();
        },
        oversized);
    CHECK_FALSE(oversized_status.ok);
    CHECK(oversized_reads == 1);
    CHECK(oversized_status.error.stage == TransportStage::ReadPrefix);
}

TEST_CASE("expired control deadlines have no renewed I/O budget",
    "[control][transport][deadline]")
{
    using namespace draxul::control_detail;
    CHECK(remaining_time(std::chrono::steady_clock::time_point::min())
        == std::chrono::milliseconds::zero());
}

TEST_CASE("control metadata atomically replaces a pre-existing file with current-user-only permissions",
    "[control][transport][security]")
{
    const auto runtime = unique_control_runtime_directory();
    REQUIRE(std::filesystem::create_directories(runtime));
    const auto metadata = control_metadata_path(
        runtime, "current-user-only-metadata");
    {
        std::ofstream previous(metadata, std::ios::binary);
        previous << "stale and permissive";
    }
#ifndef _WIN32
    REQUIRE(::chmod(metadata.c_str(), 0666) == 0);
#endif

    ControlServer server;
    std::string error;
    REQUIRE(server.start("current-user-only-metadata", runtime, [] {}, &error));
    INFO(error);
    REQUIRE(std::filesystem::exists(metadata));
#ifdef _WIN32
    CHECK(current_user_only_windows_dacl(runtime, true));
    CHECK(current_user_only_windows_dacl(metadata));
#else
    struct stat metadata_stat{};
    REQUIRE(::stat(metadata.c_str(), &metadata_stat) == 0);
    CHECK((metadata_stat.st_mode & 0777) == 0600);
#endif
    CHECK(std::filesystem::directory_iterator(runtime)
        != std::filesystem::directory_iterator());

    server.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

TEST_CASE("a second server refuses a live endpoint and leaves the incumbent intact",
    "[control][transport]")
{
    const auto runtime = unique_control_runtime_directory();
    ControlServer first;
    std::string first_error;
    REQUIRE(first.start("dup-session", runtime, [] {}, &first_error));
    REQUIRE(first.running());
    const auto metadata = first.metadata_path();
    REQUIRE(std::filesystem::exists(metadata));

    std::ifstream metadata_input(metadata);
    const auto original = nlohmann::json::parse(metadata_input);

    ControlServer second;
    std::string second_error;
    CHECK_FALSE(second.start("dup-session", runtime, [] {}, &second_error));
    CHECK(second.endpoint_in_use());
    CHECK_FALSE(second.running());
    CHECK_FALSE(second_error.empty());

    CHECK(first.running());
    REQUIRE(std::filesystem::exists(metadata));
    std::ifstream after_input(metadata);
    const auto after = nlohmann::json::parse(after_input);
    CHECK(after.at("token") == original.at("token"));
    metadata_input.close();
    after_input.close();
#ifndef _WIN32
    CHECK(std::filesystem::exists(first.endpoint()));
#endif

    first.stop();
    CHECK_FALSE(std::filesystem::exists(metadata));
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

#ifndef _WIN32
TEST_CASE("a stale endpoint from a dead owner is reclaimed", "[control][transport]")
{
    const auto runtime = unique_control_runtime_directory();
    std::string endpoint_path;
    {
        ControlServer previous;
        std::string error;
        REQUIRE(previous.start("stale-session", runtime, [] {}, &error));
        endpoint_path = previous.endpoint();
        previous.stop();
    }
    {
        std::ofstream stale(endpoint_path);
        stale << "stale";
    }
    REQUIRE(std::filesystem::exists(endpoint_path));

    ControlServer fresh;
    std::string error;
    CHECK(fresh.start("stale-session", runtime, [] {}, &error));
    CHECK(fresh.running());
    CHECK_FALSE(fresh.endpoint_in_use());
    fresh.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}
#endif

TEST_CASE("control transport authenticates and dispatches on the caller thread", "[control][transport]")
{
    const auto runtime = unique_control_runtime_directory();
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start("transport-test", runtime, [] {}, &start_error));
    REQUIRE(std::filesystem::exists(server.metadata_path()));

    auto client = std::async(std::launch::async, [&] {
        return ControlClient::request(
            "transport-test", runtime, "system.hello", { { "probe", true } });
    });

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (client.wait_for(std::chrono::milliseconds(1))
            != std::future_status::ready
        && std::chrono::steady_clock::now() < deadline)
    {
        server.process_pending([](const ControlRequest& request) {
            return ControlMethodResult::success({
                { "method", request.method },
                { "probe", request.params.value("probe", false) },
            });
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE(client.wait_for(std::chrono::milliseconds(0))
        == std::future_status::ready);
    const auto response = client.get();
    REQUIRE(response.ok);
    CHECK(response.result["method"] == "system.hello");
    CHECK(response.result["probe"] == true);

    const auto metadata = server.metadata_path();
    server.stop();
    CHECK_FALSE(std::filesystem::exists(metadata));
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

TEST_CASE("control requests obey one absolute deadline",
    "[control][transport][deadline]")
{
    const auto runtime = unique_control_runtime_directory();
    ControlServer server;
    std::atomic<bool> request_queued = false;
    std::string start_error;
    REQUIRE(server.start("deadline-test", runtime,
        [&] { request_queued = true; }, &start_error));

    const auto started_at = std::chrono::steady_clock::now();
    auto client = std::async(std::launch::async, [&] {
        return ControlClient::request("deadline-test", runtime,
            "test.stalled", nlohmann::json::object(),
            { .timeout = std::chrono::milliseconds(75) });
    });
    const auto queue_deadline
        = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!request_queued
        && std::chrono::steady_clock::now() < queue_deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(request_queued);
    REQUIRE(client.wait_for(std::chrono::milliseconds(500))
        == std::future_status::ready);
    const auto response = client.get();
    CHECK_FALSE(response.ok);
    CHECK(response.error_code == "deadline_exceeded");
    CHECK(std::chrono::steady_clock::now() - started_at
        < std::chrono::milliseconds(500));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::atomic<int> handler_calls = 0;
    server.process_pending([&](const ControlRequest&) {
        ++handler_calls;
        return ControlMethodResult::success(true);
    });
    CHECK(handler_calls == 0);

    server.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

TEST_CASE("control clients reuse recently read endpoint metadata",
    "[control][transport][metadata]")
{
    const auto runtime = unique_control_runtime_directory();
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        "metadata-cache-test", runtime, [] {}, &start_error));

    auto request = [&] {
        auto client = std::async(std::launch::async, [&] {
            return ControlClient::request("metadata-cache-test",
                runtime, "test.cached");
        });
        const auto deadline
            = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (client.wait_for(std::chrono::milliseconds(0))
                != std::future_status::ready
            && std::chrono::steady_clock::now() < deadline)
        {
            server.process_pending([](const ControlRequest&) {
                return ControlMethodResult::success(true);
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(client.wait_for(std::chrono::milliseconds(0))
            == std::future_status::ready);
        return client.get();
    };

    REQUIRE(request().ok);
    std::error_code remove_error;
    REQUIRE(std::filesystem::remove(
        server.metadata_path(), remove_error));
    REQUIRE_FALSE(remove_error);
    CHECK(request().ok);

    server.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

TEST_CASE("control server stop fails queued dispatch before listener join",
    "[control][transport][shutdown]")
{
    const auto runtime = unique_control_runtime_directory();
    ControlServer server;
    std::atomic<bool> request_queued = false;
    std::string start_error;
    REQUIRE(server.start("stop-pending-test", runtime,
        [&] { request_queued = true; }, &start_error));

    auto client = std::async(std::launch::async, [&] {
        return ControlClient::request("stop-pending-test", runtime,
            "test.never_processed", nlohmann::json::object());
    });
    const auto queue_deadline
        = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!request_queued
        && std::chrono::steady_clock::now() < queue_deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(request_queued);

    const auto started_at = std::chrono::steady_clock::now();
    server.stop();
    const auto elapsed = std::chrono::steady_clock::now() - started_at;
    CHECK(elapsed < std::chrono::seconds(2));
    REQUIRE(client.wait_for(std::chrono::milliseconds(0))
        == std::future_status::ready);
    const auto response = client.get();
    CHECK_FALSE(response.ok);
    CHECK(response.error_code == "server_stopping");

    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

TEST_CASE("control transport replaces invalid UTF-8 in response payloads",
    "[control][transport][unicode]")
{
    const auto runtime = unique_control_runtime_directory();
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        "invalid-utf8-response", runtime, [] {}, &start_error));

    auto client = std::async(std::launch::async, [&] {
        return ControlClient::request(
            "invalid-utf8-response", runtime, "test.invalid_utf8",
            nlohmann::json::object());
    });
    const auto deadline
        = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (client.wait_for(std::chrono::milliseconds(1))
            != std::future_status::ready
        && std::chrono::steady_clock::now() < deadline)
    {
        server.process_pending([](const ControlRequest&) {
            return ControlMethodResult::success({
                { "text", std::string(1, static_cast<char>(0xFF)) },
            });
        });
    }

    REQUIRE(client.wait_for(std::chrono::milliseconds(0))
        == std::future_status::ready);
    const auto response = client.get();
    INFO(response.error_code << ": " << response.error_message);
    REQUIRE(response.ok);
    CHECK(response.result["text"] == "\xEF\xBF\xBD");

    server.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

#ifdef _WIN32
TEST_CASE("a stalled Windows control client does not starve another client",
    "[control][transport][windows]")
{
    const auto runtime = unique_control_runtime_directory();
    ControlServer server;
    std::string start_error;
    REQUIRE(server.start(
        "concurrent-transport-test", runtime, [] {}, &start_error));

    const std::string endpoint = server.endpoint();
    const std::wstring pipe_name(endpoint.begin(), endpoint.end());
    HANDLE stalled = CreateFileW(pipe_name.c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, nullptr);
    REQUIRE(stalled != INVALID_HANDLE_VALUE);

    auto client = std::async(std::launch::async, [&] {
        return ControlClient::request("concurrent-transport-test", runtime,
            "system.hello", { { "probe", true } });
    });
    const auto deadline
        = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (client.wait_for(std::chrono::milliseconds(1))
            != std::future_status::ready
        && std::chrono::steady_clock::now() < deadline)
    {
        server.process_pending([](const ControlRequest& request) {
            return ControlMethodResult::success({
                { "method", request.method },
                { "probe", request.params.value("probe", false) },
            });
        });
    }

    REQUIRE(client.wait_for(std::chrono::milliseconds(0))
        == std::future_status::ready);
    const auto response = client.get();
    REQUIRE(response.ok);
    CHECK(response.result["method"] == "system.hello");
    CloseHandle(stalled);
    server.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}
#endif

#ifndef _WIN32
TEST_CASE("concurrent launchers serialize stale endpoint recovery", "[control][transport]")
{
    const auto runtime = unique_control_runtime_directory();
    std::string endpoint_path;
    {
        ControlServer previous;
        std::string error;
        REQUIRE(previous.start(
            "racing-stale-session", runtime, [] {}, &error));
        endpoint_path = previous.endpoint();
        previous.stop();
    }
    {
        std::ofstream stale(endpoint_path);
        stale << "stale";
    }
    REQUIRE(std::filesystem::exists(endpoint_path));

    ControlServer first;
    ControlServer second;
    std::string first_error;
    std::string second_error;
    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    int ready = 0;
    bool launch = false;
    auto start = [&](ControlServer& server, std::string& error) {
        {
            std::unique_lock lock(gate_mutex);
            ++ready;
            gate_changed.notify_all();
            gate_changed.wait(lock, [&] { return launch; });
        }
        return server.start(
            "racing-stale-session", runtime, [] {}, &error);
    };

    auto first_result = std::async(
        std::launch::async, [&] { return start(first, first_error); });
    auto second_result = std::async(
        std::launch::async, [&] { return start(second, second_error); });
    {
        std::unique_lock lock(gate_mutex);
        gate_changed.wait(lock, [&] { return ready == 2; });
        launch = true;
    }
    gate_changed.notify_all();

    const bool first_started = first_result.get();
    const bool second_started = second_result.get();
    CHECK(first_started != second_started);
    ControlServer& winner = first_started ? first : second;
    ControlServer& loser = first_started ? second : first;
    CHECK(winner.running());
    CHECK_FALSE(loser.running());
    CHECK(loser.endpoint_in_use());
    REQUIRE(std::filesystem::exists(winner.metadata_path()));
    REQUIRE(std::filesystem::exists(winner.endpoint()));

    auto client = std::async(std::launch::async, [&] {
        return ControlClient::request("racing-stale-session", runtime,
            "system.hello", { { "probe", true } });
    });
    const auto deadline
        = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (client.wait_for(std::chrono::milliseconds(1))
            != std::future_status::ready
        && std::chrono::steady_clock::now() < deadline)
    {
        winner.process_pending([](const ControlRequest& request) {
            return ControlMethodResult::success({
                { "method", request.method },
                { "probe", request.params.value("probe", false) },
            });
        });
    }
    REQUIRE(client.wait_for(std::chrono::milliseconds(0))
        == std::future_status::ready);
    const auto response = client.get();
    REQUIRE(response.ok);
    CHECK(response.result["method"] == "system.hello");

    winner.stop();
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}

TEST_CASE("symlinked runtime directories share one control endpoint", "[control][transport]")
{
    const auto runtime = unique_control_runtime_directory();
    auto alias = runtime;
    alias += "-alias";
    std::error_code path_error;
    std::filesystem::create_directories(runtime, path_error);
    REQUIRE_FALSE(path_error);
    std::filesystem::create_directory_symlink(runtime, alias, path_error);
    REQUIRE_FALSE(path_error);

    CHECK(namespaced_control_id("draxul-server", runtime)
        == namespaced_control_id("draxul-server", alias));

    ControlServer first;
    std::string first_error;
    REQUIRE(first.start(
        "symlink-session", runtime, [] {}, &first_error));
    std::ifstream metadata_input(first.metadata_path());
    const auto original = nlohmann::json::parse(metadata_input);
    metadata_input.close();

    ControlServer second;
    std::string second_error;
    CHECK_FALSE(second.start(
        "symlink-session", alias, [] {}, &second_error));
    CHECK(second.endpoint_in_use());
    REQUIRE(std::filesystem::exists(first.metadata_path()));
    std::ifstream after_input(first.metadata_path());
    const auto after = nlohmann::json::parse(after_input);
    CHECK(after.at("token") == original.at("token"));

    first.stop();
    std::filesystem::remove(alias, path_error);
    std::filesystem::remove_all(runtime, path_error);
}

TEST_CASE("abandoned POSIX control endpoint leaves a successor path intact",
    "[control][transport][posix][shutdown]")
{
    const auto runtime = unique_control_runtime_directory();
    ControlServer server;
    std::string error;
    REQUIRE(server.start("abandoned-endpoint", runtime, [] {}, &error));
    const std::filesystem::path endpoint = server.endpoint();
    server.abandon_endpoint();
    REQUIRE(std::filesystem::remove(endpoint));
    {
        std::ofstream successor(endpoint, std::ios::binary);
        REQUIRE(successor.good());
        successor << "successor";
    }

    server.stop();

    REQUIRE(std::filesystem::exists(endpoint));
    std::ifstream input(endpoint, std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(input), {})
        == "successor");
    std::error_code ignored;
    std::filesystem::remove_all(runtime, ignored);
}
#endif
