#include "support/test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <draxul/log.h>

#include <string>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

TEST_CASE("logger filters by level", "[log]")
{
    ScopedLogCapture capture;
    LogOptions options;
    options.min_level = LogLevel::Warn;
    options.enable_stderr = false;
    configure_logging(options);
    set_log_sink([&capture](const LogRecord& record) { capture.records.push_back(record); });

    log_message(LogLevel::Info, LogCategory::App, "hidden");
    log_message(LogLevel::Warn, LogCategory::App, "shown");

    REQUIRE(capture.records.size() == 1);
    REQUIRE(capture.records[0].message == std::string("shown"));
}

TEST_CASE("logger filters by category", "[log]")
{
    ScopedLogCapture capture;
    LogOptions options;
    options.enable_stderr = false;
    options.enabled_categories = { LogCategory::Rpc };
    configure_logging(options);
    set_log_sink([&capture](const LogRecord& record) { capture.records.push_back(record); });

    log_message(LogLevel::Error, LogCategory::App, "skip");
    log_message(LogLevel::Error, LogCategory::Rpc, "keep");

    REQUIRE(capture.records.size() == 1);
    REQUIRE(capture.records[0].category == LogCategory::Rpc);
}

TEST_CASE("logger sinks may recurse and clear themselves", "[log][reentrant]")
{
    LogOptions options;
    options.enable_stderr = false;
    configure_logging(options);
    std::vector<std::string> messages;
    set_log_sink([&](const LogRecord& record) {
        messages.push_back(record.message);
        if (record.message == "outer")
            log_message(LogLevel::Info, LogCategory::Test, "inner");
        else
            clear_log_sink();
    });

    log_message(LogLevel::Info, LogCategory::Test, "outer");
    log_message(LogLevel::Info, LogCategory::Test, "after-clear");
    CHECK(messages == std::vector<std::string>{ "outer", "inner" });
}

TEST_CASE("an in-flight logger sink survives concurrent replacement",
    "[log][reentrant]")
{
    LogOptions options;
    options.enable_stderr = false;
    configure_logging(options);
    int first = 0;
    int second = 0;
    set_log_sink([&](const LogRecord&) {
        ++first;
        set_log_sink([&](const LogRecord&) { ++second; });
    });
    log_message(LogLevel::Info, LogCategory::Test, "replace");
    log_message(LogLevel::Info, LogCategory::Test, "new-sink");
    CHECK(first == 1);
    CHECK(second == 1);
}

TEST_CASE("an in-flight logger sink may shut logging down", "[log][reentrant]")
{
    LogOptions options;
    options.enable_stderr = false;
    configure_logging(options);
    bool returned = false;
    set_log_sink([&](const LogRecord&) {
        shutdown_logging();
        returned = true;
    });
    log_message(LogLevel::Info, LogCategory::Test, "shutdown");
    CHECK(returned);
}
