#include "session_attach_internal.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <aclapi.h>
#include <windows.h>
#endif

using namespace draxul;
using namespace draxul::session_attach_detail;
using namespace draxul::tests;

namespace
{

struct FakeConnectionState
{
    std::string request;
    std::vector<std::string> response_chunks;
    bool write_succeeds = true;
    bool read_succeeds = true;
};

class FakeConnection final : public SessionConnection
{
public:
    explicit FakeConnection(std::shared_ptr<FakeConnectionState> state)
        : state_(std::move(state))
    {
    }

    bool read_request(std::string*, std::string*) override { return false; }

    bool write_all(std::string_view payload, std::string* error) override
    {
        state_->request.append(payload);
        if (state_->write_succeeds)
            return true;
        if (error)
            *error = "fake write failure";
        return false;
    }

    bool read_response(std::string* response, std::string* error) override
    {
        if (!state_->read_succeeds)
        {
            if (error)
                *error = "fake read failure";
            return false;
        }
        if (response)
        {
            response->clear();
            for (const auto& chunk : state_->response_chunks)
                response->append(chunk);
        }
        return true;
    }

private:
    std::shared_ptr<FakeConnectionState> state_;
};

class FakeTransport final : public SessionTransport
{
public:
    explicit FakeTransport(std::vector<std::string> response_chunks)
        : state(std::make_shared<FakeConnectionState>())
    {
        state->response_chunks = std::move(response_chunks);
    }

    bool start(std::string*) override { return true; }
    std::unique_ptr<SessionConnection> accept(std::string*) override { return {}; }

    TransportResult connect(
        std::unique_ptr<SessionConnection>* connection, std::chrono::milliseconds) override
    {
        *connection = std::make_unique<FakeConnection>(state);
        return {};
    }

    TransportResult probe(std::chrono::milliseconds) override { return {}; }
    void wake(std::string_view) override {}
    void close() override {}

    std::shared_ptr<FakeConnectionState> state;
};

} // namespace

TEST_CASE("session attach protocol reassembles a response delivered in partial chunks",
    "[session_attach][protocol][fake_transport]")
{
    FakeTransport transport({ "workspace_", "count=2\npane_count=5\ndetached=", "1\nowner_pid=4242\n",
        "last_attached_unix_s=111\nlast_detached_unix_s=222", "\n" });

    SessionAttachServer::LiveSessionInfo info;
    std::string error;
    REQUIRE(query_live_session_via_transport(transport, &info, &error));
    CHECK(error.empty());
    CHECK(transport.state->request == "query-live-session");
    CHECK(info.workspace_count == 2);
    CHECK(info.pane_count == 5);
    CHECK(info.detached);
    CHECK(info.owner_pid == 4242);
    CHECK(info.last_attached_unix_s == 111);
    CHECK(info.last_detached_unix_s == 222);
}

TEST_CASE("session attach protocol rejects malformed and incomplete response frames",
    "[session_attach][protocol][fake_transport]")
{
    const std::vector<std::string> malformed{
        "workspace_count=2\npane_count=5\ndetached=1\nowner_pid=4242\n"
        "last_attached_unix_s=111\n",
        "workspace_count=two\npane_count=5\ndetached=1\nowner_pid=4242\n"
        "last_attached_unix_s=111\nlast_detached_unix_s=222\n",
        "workspace_count=2\npane_count=5\ndetached\nowner_pid=4242\n"
        "last_attached_unix_s=111\nlast_detached_unix_s=222\n",
    };

    for (const auto& response : malformed)
    {
        CAPTURE(response);
        FakeTransport transport({ response });
        SessionAttachServer::LiveSessionInfo info;
        std::string error;
        CHECK_FALSE(query_live_session_via_transport(transport, &info, &error));
        CHECK_FALSE(error.empty());
        CHECK(transport.state->request == "query-live-session");
    }
}

TEST_CASE("session attach command waits for the server acknowledgement",
    "[session_attach][protocol][fake_transport]")
{
    FakeTransport transport({ "ok" });
    std::string error;
    CHECK(send_command_via_transport(
              transport, SessionAttachServer::Command::Detach, &error)
        == SessionAttachServer::AttachStatus::Attached);
    CHECK(error.empty());
    CHECK(transport.state->request == "detach");

    FakeTransport rejected({ "denied" });
    CHECK(send_command_via_transport(
              rejected, SessionAttachServer::Command::Shutdown, &error)
        == SessionAttachServer::AttachStatus::Error);
    CHECK(error == "denied");
    CHECK(rejected.state->request == "shutdown");
}

TEST_CASE("session attach platform transport observes a bounded missing-endpoint probe",
    "[session_attach][transport][timeout]")
{
    TempDir temp_dir("session-attach-probe-timeout");
    HomeDirRedirect redirect(temp_dir.path);
    auto transport = make_session_transport("definitely-not-running");
    CHECK(transport->probe(std::chrono::milliseconds(5)).status == TransportStatus::NoServer);
}

#ifdef _WIN32
namespace
{

std::string expected_pipe_name(std::string_view session_id)
{
    return "\\\\.\\pipe\\draxul-session-attach-" + endpoint_suffix(session_id);
}

std::vector<unsigned char> current_user_token()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<unsigned char> buffer(bytes);
    const bool ok = bytes > 0
        && GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes) != FALSE;
    CloseHandle(token);
    return ok ? buffer : std::vector<unsigned char>{};
}

} // namespace

TEST_CASE("session attach Windows pipe is owned by and grants access to the current user",
    "[session_attach][transport][windows][security]")
{
    TempDir temp_dir("session-attach-win-security");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    REQUIRE(server.start("security", [](SessionAttachServer::Command) {}));

    const std::string pipe_name = expected_pipe_name("security");
    HANDLE pipe = CreateFileA(pipe_name.c_str(),
        GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    REQUIRE(pipe != INVALID_HANDLE_VALUE);

    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    REQUIRE(GetSecurityInfo(pipe,
                SE_KERNEL_OBJECT,
                OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                &owner,
                nullptr,
                &dacl,
                nullptr,
                &descriptor)
        == ERROR_SUCCESS);

    const auto token = current_user_token();
    REQUIRE_FALSE(token.empty());
    const auto* token_user = reinterpret_cast<const TOKEN_USER*>(token.data());
    CHECK(EqualSid(owner, token_user->User.Sid) != FALSE);
    REQUIRE(dacl != nullptr);

    bool found_user_access = false;
    for (DWORD i = 0; i < dacl->AceCount; ++i)
    {
        void* raw_ace = nullptr;
        REQUIRE(GetAce(dacl, i, &raw_ace) != FALSE);
        const auto* header = static_cast<const ACE_HEADER*>(raw_ace);
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
            continue;
        const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(raw_ace);
        const PSID sid = const_cast<DWORD*>(&ace->SidStart);
        if (EqualSid(sid, token_user->User.Sid) != FALSE)
        {
            found_user_access = (ace->Mask & FILE_GENERIC_READ) == FILE_GENERIC_READ
                && (ace->Mask & FILE_GENERIC_WRITE) == FILE_GENERIC_WRITE;
        }
    }
    CHECK(found_user_access);

    LocalFree(descriptor);
    CloseHandle(pipe);
    server.stop();
}

TEST_CASE("session attach Windows transport rejects a competing live endpoint",
    "[session_attach][transport][windows][shutdown]")
{
    TempDir temp_dir("session-attach-win-exclusive");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer first;
    SessionAttachServer second;
    REQUIRE(first.start("exclusive", [](SessionAttachServer::Command) {}));
    std::string error;
    CHECK_FALSE(second.start("exclusive", [](SessionAttachServer::Command) {}, &error));
    CHECK_FALSE(error.empty());
    first.stop();
}
#endif
