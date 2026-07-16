#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>

#include <draxul/host_registry.h>
#include <draxul/kanban/kanban_host.h>
#include <draxul/log.h>
#include <draxul/markdown/markdown_host.h>
#include <draxul/nanovg_demo_host.h>

#include <cstdlib>
#include <string_view>

#ifndef _WIN32
#include <csignal>
#endif
#ifdef DRAXUL_ENABLE_MEGACITY
#include <draxul/megacity_host.h>
#endif
#ifdef DRAXUL_ENABLE_SATVIEW
#include <draxul/satview/satview_host.h>
#endif
#ifdef DRAXUL_ENABLE_SCOREVIEW
#include <draxul/scoreview/score_host.h>
#endif

int main(int argc, char* argv[])
{
#ifndef _WIN32
    // Ignore SIGPIPE so tests that spawn child processes (rpc-fake, shell hosts)
    // don't kill the entire test binary when a pipe breaks.
    signal(SIGPIPE, SIG_IGN);
#endif
    draxul::LogOptions log_options;
    log_options.enable_stderr = false;

    if (const char* stderr_env = std::getenv("DRAXUL_TEST_STDERR_LOGS"))
    {
        const std::string_view value(stderr_env);
        if (value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "on")
            log_options.enable_stderr = true;
    }

    if (const char* file_env = std::getenv("DRAXUL_TEST_LOG_FILE");
        file_env && *file_env != '\0')
    {
        log_options.enable_file = true;
        log_options.file_path = file_env;
    }

    draxul::configure_logging(log_options);

    // Tests share the same provider registry as the app. Register the same
    // set the executable would so HostManager can create real hosts.
    auto& registry = draxul::HostProviderRegistry::global();
    registry.clear();
    draxul::register_builtin_host_providers(registry);
    draxul::register_nanovg_demo_host_provider(registry);
    draxul::markdown::register_markdown_host_provider(registry);
    draxul::kanban::register_kanban_host_provider(registry);
#ifdef DRAXUL_ENABLE_MEGACITY
    draxul::register_megacity_host_provider(registry);
#endif
#ifdef DRAXUL_ENABLE_SATVIEW
    draxul::satview::register_satview_host_provider(registry);
#endif
#ifdef DRAXUL_ENABLE_SCOREVIEW
    draxul::scoreview::register_score_host_provider(registry);
#endif

    return Catch::Session().run(argc, argv);
}
