#include <draxul/log.h>

#include <array>
#include <cstdio>
#include <mutex>

namespace draxul
{

namespace
{
std::mutex g_log_mutex;
LogOptions g_options;
LogSink g_sink;
}

void configure_logging(const LogOptions& options)
{
    std::lock_guard lock(g_log_mutex);
    g_options = options;
}

void set_log_sink(LogSink sink)
{
    std::lock_guard lock(g_log_mutex);
    g_sink = std::move(sink);
}

void clear_log_sink()
{
    std::lock_guard lock(g_log_mutex);
    g_sink = {};
}

void log_printf(LogLevel level, LogCategory category, const char* format, ...)
{
    std::array<char, 2048> message{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message.data(), message.size(), format, args);
    va_end(args);

    const char* label = "info";
    switch (level)
    {
    case LogLevel::Error:
        label = "error";
        break;
    case LogLevel::Warn:
        label = "warn";
        break;
    case LogLevel::Debug:
        label = "debug";
        break;
    case LogLevel::Info:
        break;
    }
    std::lock_guard lock(g_log_mutex);
    if (static_cast<int>(level) > static_cast<int>(g_options.min_level))
        return;
    LogRecord record{ level, category, message.data() };
    if (g_sink)
        g_sink(record);
    if (g_options.enable_stderr)
        std::fprintf(stderr, "[scoreview][%s] %s\n", label, message.data());
}

} // namespace draxul
