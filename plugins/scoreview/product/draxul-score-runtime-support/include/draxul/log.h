#pragma once

#include <cstdarg>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

enum class LogCategory
{
    App,
    Renderer,
    Test,
};

enum class LogLevel
{
    Error,
    Warn,
    Info,
    Debug,
};

struct LogRecord
{
    LogLevel level = LogLevel::Info;
    LogCategory category = LogCategory::App;
    std::string message;
};

struct LogOptions
{
    LogLevel min_level = LogLevel::Info;
    bool enable_stderr = true;
    bool enable_file = false;
    std::string file_path;
    std::vector<LogCategory> enabled_categories;
};

using LogSink = std::function<void(const LogRecord&)>;

void configure_logging(const LogOptions& options = {});
void set_log_sink(LogSink sink);
void clear_log_sink();
void log_printf(LogLevel level, LogCategory category, const char* format, ...);

} // namespace draxul

#define DRAXUL_LOG_ERROR(category, ...) \
    ::draxul::log_printf(::draxul::LogLevel::Error, category, __VA_ARGS__)
#define DRAXUL_LOG_WARN(category, ...) \
    ::draxul::log_printf(::draxul::LogLevel::Warn, category, __VA_ARGS__)
#define DRAXUL_LOG_INFO(category, ...) \
    ::draxul::log_printf(::draxul::LogLevel::Info, category, __VA_ARGS__)
#define DRAXUL_LOG_DEBUG(category, ...) \
    ::draxul::log_printf(::draxul::LogLevel::Debug, category, __VA_ARGS__)
