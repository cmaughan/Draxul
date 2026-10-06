#include <catch2/catch_test_macros.hpp>
#include "fake_renderer.h"
#include "fake_window.h"
#include "support/test_support.h"
#include <draxul/personal_assistant_host.h>
#include <draxul/text_service.h>
#include <SDL3/SDL_keycode.h>

using namespace draxul;

namespace
{
class PersonalCallbacks : public IHostCallbacks
{
public:
    std::shared_ptr<PersonalAgentSnapshot> snapshot = std::make_shared<PersonalAgentSnapshot>();
    std::vector<std::string> opened;
    void open_personal_agent(std::string id) override { opened.push_back(std::move(id)); }
    std::optional<PersonalAgentCommand> command;
    std::shared_ptr<PersonalAgentCommandResult> command_result;
    std::string personal_command(const PersonalAgentCommand& value) override
    { command=value; command_result.reset(); return "test-request"; }
    std::shared_ptr<const PersonalAgentCommandResult> personal_result(std::string_view) const override
    { return command_result; }
    void request_frame() override {}
    void request_quit() override {}
    void wake_window() override {}
    void set_window_title(const std::string&) override {}
    void set_text_input_area(int, int, int, int) override {}
    std::shared_ptr<const PersonalAgentSnapshot> personal_agents() const override { return snapshot; }
};

class InspectablePersonalHost : public PersonalAssistantHost
{
public:
    std::string text() const
    {
        std::string result;
        for (int row = 0; row < grid_rows(); ++row)
        {
            for (int column = 0; column < grid_cols(); ++column)
                result += grid().get_cell(column, row).text.view();
            result += '\n';
        }
        return result;
    }
};
}

TEST_CASE("personal entry opens provider conversations without a definition form", "[personal][host]")
{
    tests::FakeWindow window;
    tests::FakeTermRenderer renderer;
    TextService text_service;
    tests::init_text_service(text_service);
    PersonalCallbacks callbacks;
    HostContext context{.window=&window,.grid_renderer=&renderer,.text_service=&text_service,
        .initial_viewport={.grid_size={100,30}}};
    InspectablePersonalHost host;
    REQUIRE(host.initialize(context,callbacks));
    CHECK(host.text().find("chat in its terminal")!=std::string::npos);
    CHECK(host.text().find("Model (explicit)")==std::string::npos);
    host.on_key({.keycode=SDLK_N,.pressed=true});
    REQUIRE(callbacks.opened.size()==1);
    CHECK(callbacks.opened.back().empty());
    REQUIRE(host.dispatch_action("personal.select/news"));
    host.on_key({.keycode=SDLK_RETURN,.pressed=true});
    CHECK(callbacks.opened.back()=="news");
    CHECK_FALSE(callbacks.command);
    host.request_close();
    CHECK_FALSE(host.is_running());
}

TEST_CASE("personal host provider is built in and launchable", "[personal][host]")
{
    HostProviderRegistry registry;
    register_personal_assistant_host_provider(registry);
    REQUIRE(registry.has(HostKind::PersonalAssistant));
    REQUIRE(registry.create(HostKind::PersonalAssistant));
    CHECK(parse_host_kind("personal-assistant") == HostKind::PersonalAssistant);
    CHECK_FALSE(is_server_owned_shell_host(HostKind::PersonalAssistant));
    REQUIRE(registry.metadata(HostKind::PersonalAssistant));
    CHECK(registry.metadata(HostKind::PersonalAssistant)->palette_visible);
}
