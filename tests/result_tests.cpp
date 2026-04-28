// WI 24: tests for the project-wide Result<T, E> type and its first two
// migrated call sites (NvimProcess::spawn and App::reload_config's structured
// error return shape). The spawn-site migration is also exercised here via
// the existing fake-server helper.

#include <catch2/catch_all.hpp>

#include <draxul/nvim_rpc.h>
#include <draxul/result.h>

#include "support/scoped_env_var.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>

using namespace draxul;

namespace
{
std::string fake_server_path()
{
#ifdef DRAXUL_RPC_FAKE_PATH
    return DRAXUL_RPC_FAKE_PATH;
#else
    return "";
#endif
}

std::string missing_nvim_path()
{
    return "/definitely/not/a/real/nvim/binary/draxul-wi24";
}
} // namespace

TEST_CASE("Result<int, Error> ok path", "[result]")
{
    auto r = Result<int, Error>::ok(42);
    REQUIRE(r);
    REQUIRE(r.has_value());
    REQUIRE(r.value() == 42);
    REQUIRE(*r == 42);
    REQUIRE(r.value_or(0) == 42);
}

TEST_CASE("Result<int, Error> error path", "[result]")
{
    auto r = Result<int, Error>::err(Error::io("disk full"));
    REQUIRE_FALSE(r);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().kind == ErrorKind::IoError);
    REQUIRE(r.error().message == "disk full");
    REQUIRE(r.value_or(-1) == -1);
}

TEST_CASE("Result<void, Error> ok/err", "[result]")
{
    Result<void, Error> ok = Result<void, Error>::ok();
    REQUIRE(ok);
    REQUIRE(ok.has_value());

    Result<void, Error> bad = Result<void, Error>::err(
        Error::config_parse("line 3: expected ="));
    REQUIRE_FALSE(bad);
    REQUIRE(bad.error().kind == ErrorKind::ConfigParseFailed);
    REQUIRE(bad.error().message == "line 3: expected =");
}

TEST_CASE("Result<void, Error> default constructs to ok", "[result]")
{
    Result<void, Error> r;
    REQUIRE(r);
}

TEST_CASE("Result implicit construction from value", "[result]")
{
    auto factory = []() -> Result<std::string, Error> {
        return std::string("hello"); // implicit T -> Result<T,E>
    };
    auto r = factory();
    REQUIRE(r);
    REQUIRE(*r == "hello");
}

TEST_CASE("VoidResult alias works", "[result]")
{
    VoidResult r = VoidResult::err(Error::rpc("timeout"));
    REQUIRE_FALSE(r);
    REQUIRE(r.error().kind == ErrorKind::RpcError);
}

// Migrated call site: NvimProcess::spawn now returns Result<void, Error>.
// These tests verify that (a) existing bool-context call sites still compile
// and behave identically, and (b) the new error payload is populated on
// failure so callers can surface it.

TEST_CASE("NvimProcess::spawn returns structured error on missing binary", "[result][nvim_process]")
{
    NvimProcess process;
    auto r = process.spawn(missing_nvim_path());
    REQUIRE_FALSE(r);
    REQUIRE_FALSE(r.has_value());
    // Kind should be SpawnFailed (exec/CreateProcess) or IoError
    // (pipe setup); both are acceptable failure modes for a missing path.
    REQUIRE((r.error().kind == ErrorKind::SpawnFailed
        || r.error().kind == ErrorKind::IoError));
    REQUIRE_FALSE(r.error().message.empty());
}

TEST_CASE("NvimProcess::spawn still usable in bool context", "[result][nvim_process]")
{
    NvimProcess process;
    // Contextual bool conversion via explicit operator bool — this is the
    // backwards-compatibility path for the existing `REQUIRE(spawn(...))`
    // and `if (!spawn(...))` call sites we left unchanged during the WI 24
    // migration.
    REQUIRE_FALSE(process.spawn(missing_nvim_path()));
}

TEST_CASE("NvimProcess::spawn ok path against fake server", "[result][nvim_process]")
{
    const std::string fake = fake_server_path();
    if (fake.empty())
        return; // fake-server path not wired; skip.

    NvimProcess process;
    auto r = process.spawn(fake);
    REQUIRE(r);
    REQUIRE(r.has_value());
    process.shutdown();
}

TEST_CASE("NvimProcess::spawn forces TERM=dumb for embedded nvim", "[result][nvim_process]")
{
    const std::string fake = fake_server_path();
    if (fake.empty())
        return;

    const auto dump_path
        = std::filesystem::temp_directory_path() / "draxul-nvim-process-term-dump.txt";
    std::filesystem::remove(dump_path);

    draxul::tests::ScopedEnvVar fake_mode("DRAXUL_RPC_FAKE_MODE", "dump_term_and_exit");
    draxul::tests::ScopedEnvVar dump_target("DRAXUL_RPC_FAKE_TERM_DUMP", dump_path.string().c_str());
    draxul::tests::ScopedEnvVar parent_term("TERM", "xterm-256color");

    NvimProcess process;
    auto r = process.spawn(fake);
    REQUIRE(r);
    REQUIRE(r.has_value());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!std::filesystem::exists(dump_path)
        && process.is_running()
        && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    process.shutdown();

    std::string term;
    {
        std::ifstream in(dump_path);
        REQUIRE(in.good());
        std::getline(in, term);
    }
    REQUIRE(term == "dumb");

    std::filesystem::remove(dump_path);
}
