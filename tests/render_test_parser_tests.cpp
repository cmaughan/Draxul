#include <draxul/json_util.h>
#include <draxul/render_test.h>

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{

void write_text_file(const std::filesystem::path& path, const std::string& text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::trunc);
    out << text;
}

} // namespace

TEST_CASE("render test parser: multiline commands array parses", "[render]")
{
    const auto dir = std::filesystem::temp_directory_path() / "draxul-render-test-parser-multi";
    const auto path = dir / "multiline.toml";
    write_text_file(path,
        "name = \"multiline\"\n"
        "width = 900\n"
        "height = 700\n"
        "debug_overlay = true\n"
        "enable_ligatures = false\n"
        "commands = [\n"
        "  \"edit ${PROJECT_ROOT}/README.md\",\n"
        "  \"set nowrap\",\n"
        "  \"lua vim.cmd([[normal! ggzt]])\",\n"
        "]\n");

    std::string err;
    auto scenario = draxul::load_render_test_scenario(path, &err);
    INFO("multiline render scenario should load");
    REQUIRE(scenario.has_value());
    INFO("name");
    REQUIRE(scenario->name == std::string("multiline"));
    INFO("width");
    REQUIRE(scenario->width == 900);
    INFO("height");
    REQUIRE(scenario->height == 700);
    INFO("debug_overlay flag");
    REQUIRE(scenario->debug_overlay);
    INFO("enable_ligatures flag");
    REQUIRE(!scenario->enable_ligatures);
    INFO("command count");
    REQUIRE(static_cast<int>(scenario->commands.size()) == 3);
    INFO("project root placeholder expands");
    REQUIRE(scenario->commands[0]
        == std::string("edit " + (std::filesystem::path{ DRAXUL_PROJECT_ROOT } / "README.md").lexically_normal().generic_string()));
    INFO("second command");
    REQUIRE(scenario->commands[1] == std::string("set nowrap"));
    INFO("third command");
    REQUIRE(scenario->commands[2] == std::string("lua vim.cmd([[normal! ggzt]])"));

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("render test parser: missing commands field returns error", "[render]")
{
    const auto dir = std::filesystem::temp_directory_path() / "draxul-render-test-parser-nocmd";
    const auto path = dir / "nocmd.toml";
    write_text_file(path, "name = \"no-commands\"\nwidth = 800\n");

    std::string err;
    auto scenario = draxul::load_render_test_scenario(path, &err);
    INFO("missing commands should fail");
    REQUIRE(!scenario.has_value());
    INFO("error message should be set");
    REQUIRE(!err.empty());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("render test parser: hash comments are stripped in scenario files", "[render]")
{
    const auto dir = std::filesystem::temp_directory_path() / "draxul-render-test-parser-comment";
    const auto path = dir / "commented.toml";
    write_text_file(path,
        "# This is a comment\n"
        "width = 640 # inline comment\n"
        "height = 480\n"
        "commands = [\"echo\"]\n");

    std::string err;
    auto scenario = draxul::load_render_test_scenario(path, &err);
    INFO("scenario with comments should load");
    REQUIRE(scenario.has_value());
    INFO("width after stripping inline comment");
    REQUIRE(scenario->width == 640);
    INFO("height");
    REQUIRE(scenario->height == 480);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("render test parser: ${PROJECT_ROOT} placeholder expands to the compile-time project root", "[render]")
{
    const auto dir = std::filesystem::temp_directory_path() / "draxul-render-test-parser-projroot";
    const auto path = dir / "projroot.toml";
    write_text_file(path,
        "font_path = \"${PROJECT_ROOT}/fonts/test.ttf\"\n"
        "commands = [\"echo\"]\n");

    std::string err;
    auto scenario = draxul::load_render_test_scenario(path, &err);
    INFO("scenario should load");
    REQUIRE(scenario.has_value());
    const auto expected = (std::filesystem::path{ DRAXUL_PROJECT_ROOT } / "fonts/test.ttf").lexically_normal().generic_string();
    INFO("${PROJECT_ROOT} expands to project root");
    REQUIRE(scenario->font_path.lexically_normal().generic_string() == expected);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("render test parser: omitted font_path leaves the font override unset", "[render]")
{
    const auto dir = std::filesystem::temp_directory_path() / "draxul-render-test-parser-font-override";
    const auto path = dir / "nanovg-like.toml";
    write_text_file(path,
        "host = \"nanovg-demo\"\n"
        "commands = [\"noop\"]\n");

    std::string err;
    auto scenario = draxul::load_render_test_scenario(path, &err);
    INFO("scenario without font_path should load");
    REQUIRE(scenario.has_value());

    const draxul::AppOptions options = scenario->make_app_options();
    INFO("missing font_path should not clear the bundled font fallback");
    REQUIRE_FALSE(options.config_overrides.font_path.has_value());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("render test parser: invalid TOML scenario returns a parse error", "[render]")
{
    const auto dir = std::filesystem::temp_directory_path() / "draxul-render-test-parser-invalid";
    const auto path = dir / "invalid.toml";
    write_text_file(path,
        "width = 640\n"
        "commands = [\"echo\"\n");

    std::string err;
    auto scenario = draxul::load_render_test_scenario(path, &err);
    INFO("invalid TOML should fail");
    REQUIRE(!scenario.has_value());
    INFO("parse error should be set");
    REQUIRE(!err.empty());

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("render test parser: host fields parse for powershell scenarios", "[render]")
{
    const auto dir = std::filesystem::temp_directory_path() / "draxul-render-test-parser-host";
    const auto path = dir / "host.toml";
    write_text_file(path,
        "host = \"powershell\"\n"
        "host_command = \"pwsh.exe\"\n"
        "host_args = [\"-NoLogo\", \"-NoProfile\"]\n"
        "commands = [\"Write-Host ready\"]\n");

    std::string err;
    auto scenario = draxul::load_render_test_scenario(path, &err);
    INFO("powershell scenario should load");
    REQUIRE(scenario.has_value());
    INFO("host kind parses");
    REQUIRE(scenario->host_kind == draxul::HostKind::PowerShell);
    INFO("host command parses");
    REQUIRE(scenario->host_command == std::string("pwsh.exe"));
    INFO("host args count");
    REQUIRE(static_cast<int>(scenario->host_args.size()) == 2);
    INFO("first host arg");
    REQUIRE(scenario->host_args[0] == std::string("-NoLogo"));
    INFO("second host arg");
    REQUIRE(scenario->host_args[1] == std::string("-NoProfile"));

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("render test parser: plugin identity and configuration flow to app options",
    "[render][plugin]")
{
    const auto dir = std::filesystem::temp_directory_path()
        / "draxul-render-test-parser-plugin";
    const auto path = dir / "plugin.toml";
    write_text_file(path,
        "host = \"plugin\"\n"
        "plugin_id = \"dev.draxul.spinning-triangle\"\n"
        "plugin_config_json = '{\"paused\":true,\"source\":\"${PROJECT_ROOT}/plugins\"}'\n"
        "commands = [\"\"]\n");
    std::string error;
    const auto scenario = draxul::load_render_test_scenario(path, &error);
    REQUIRE(scenario);
    CHECK(scenario->host_kind == draxul::HostKind::Plugin);
    const auto options = scenario->make_app_options();
    CHECK(options.host_plugin_id
        == "dev.draxul.spinning-triangle");
    const auto expected_plugin_root
        = (std::filesystem::path{ DRAXUL_PROJECT_ROOT } / "plugins")
              .lexically_normal()
              .generic_string();
    CHECK(options.host_plugin_config_json
        == "{\"paused\":true,\"source\":\"" + expected_plugin_root + "\"}");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("render test parser: plugin reload package expands build root",
    "[render][plugin][reload]")
{
    const auto dir = std::filesystem::temp_directory_path()
        / "draxul-render-test-parser-plugin-reload";
    const auto path = dir / "plugin-reload.toml";
    write_text_file(path,
        "host = \"plugin\"\n"
        "plugin_id = \"dev.draxul.spinning-triangle\"\n"
        "reload_plugin_id = \"dev.draxul.spinning-triangle\"\n"
        "reload_plugin_package = \"${BUILD_ROOT}/reload-package\"\n"
        "commands = [\"\"]\n");
    std::string error;
    const auto scenario = draxul::load_render_test_scenario(path, &error);
    INFO(error);
    REQUIRE(scenario);
    CHECK(scenario->reload_plugin_id
        == "dev.draxul.spinning-triangle");
    CHECK(scenario->reload_plugin_package.lexically_normal()
        == (std::filesystem::path{ DRAXUL_BUILD_ROOT }
               / "reload-package")
               .lexically_normal());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("render test plugin publication swaps and restores current generation",
    "[render][plugin][reload]")
{
    const auto dir = std::filesystem::temp_directory_path()
        / "draxul-render-test-plugin-publication";
    std::error_code cleanup_error;
    std::filesystem::remove_all(dir, cleanup_error);
    const auto source = dir / "source";
    const auto destination = dir / "destination";
    write_text_file(source / "generations" / "replacement"
            / "plugin.toml",
        "schema_version = 1\n");
    write_text_file(source / "generations" / "replacement"
            / "package.json",
        R"({"schema_version":1,"build_id":"replacement","files":{}})");
    write_text_file(source / "current.json",
        R"({"schema_version":1,"generation":"replacement"})");
    write_text_file(destination / "generations" / "original"
            / "plugin.toml",
        "schema_version = 1\n");
    const std::string original_pointer
        = R"({"schema_version":1,"generation":"original"})";
    write_text_file(destination / "current.json", original_pointer);

    std::string error;
    const auto publication
        = draxul::publish_render_test_plugin_generation(
            source, destination, &error);
    INFO(error);
    REQUIRE(publication);
    CHECK(std::filesystem::is_regular_file(destination / "generations"
        / "replacement" / "plugin.toml"));
    std::ifstream active_pointer(destination / "current.json");
    const std::string active((std::istreambuf_iterator<char>(active_pointer)),
        {});
    CHECK(active.find("replacement") != std::string::npos);
    active_pointer.close();

    REQUIRE(draxul::restore_render_test_plugin_generation(
        *publication, &error));
    std::ifstream restored_pointer(destination / "current.json");
    const std::string restored(
        (std::istreambuf_iterator<char>(restored_pointer)), {});
    CHECK(restored == original_pointer);
    CHECK_FALSE(std::filesystem::exists(destination / "generations"
        / "replacement"));
    std::filesystem::remove_all(dir, cleanup_error);
}

TEST_CASE("render test parser: source path flows to non-terminal hosts", "[render][markdown]")
{
    const auto dir = std::filesystem::temp_directory_path()
        / "draxul-render-test-parser-source";
    const auto path = dir / "markdown.toml";
    write_text_file(path,
        "host = \"markdown\"\n"
        "source = \"${PROJECT_ROOT}/README.md\"\n"
        "commands = [\"\"]\n");

    std::string error;
    const auto scenario = draxul::load_render_test_scenario(path, &error);
    INFO(error);
    REQUIRE(scenario);
    CHECK(scenario->host_kind == draxul::HostKind::Markdown);
    const auto expected_source
        = (std::filesystem::path{ DRAXUL_PROJECT_ROOT } / "README.md")
              .lexically_normal();
    CHECK(scenario->source_path.lexically_normal() == expected_source);
    CHECK(std::filesystem::path{ scenario->make_app_options().host_source_path }
              .lexically_normal()
        == expected_source);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("render test parser: json_escape_string escapes double quotes and backslashes", "[render]")
{
    INFO("plain passthrough");
    REQUIRE(draxul::json_escape_string("hello") == std::string("hello"));
    INFO("embedded quotes");
    REQUIRE(draxul::json_escape_string("say \"hi\"") == std::string("say \\\"hi\\\""));
    INFO("backslash");
    REQUIRE(draxul::json_escape_string("path\\file") == std::string("path\\\\file"));
    INFO("newline");
    REQUIRE(draxul::json_escape_string("line1\nline2") == std::string("line1\\nline2"));
    INFO("tab");
    REQUIRE(draxul::json_escape_string("tab\there") == std::string("tab\\there"));
}
