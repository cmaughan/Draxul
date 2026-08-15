#include <draxul/app_config_types.h>
#include <draxul/config_document.h>
#include <draxul/config_schema.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/toml_support.h>

namespace draxul
{

void ConfigDocument::merge_core_config(const AppConfig& config)
{
    PERF_MEASURE();
    std::string parse_error;
    auto parsed = toml_support::parse_document(config.serialize(), &parse_error);
    if (!parsed)
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Failed to merge core config into document: %s",
            parse_error.c_str());
        return;
    }

    // Only the core schema's keys are replaced. Plugin-owned tables remain
    // untouched in the shared document tree.
    config_schema::for_each_core_top_level_key([&](std::string_view key) {
        document_.erase(key);
        if (const toml::node* node = parsed->get(key))
            document_.insert_or_assign(std::string(key), *node);
    });
}

} // namespace draxul
