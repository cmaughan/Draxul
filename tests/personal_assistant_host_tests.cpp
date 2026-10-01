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

TEST_CASE("personal host displays read only state and preserves definitions on close", "[personal][host]")
{
    tests::FakeWindow window;
    tests::FakeTermRenderer renderer;
    TextService text_service;
    tests::init_text_service(text_service);
    PersonalCallbacks callbacks;
    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .initial_viewport = { .grid_size = { 100, 30 } },
    };
    InspectablePersonalHost host;
    REQUIRE(host.initialize(context, callbacks));
    CHECK(host.text().find("Configure [agents]") != std::string::npos);
    callbacks.snapshot = std::make_shared<PersonalAgentSnapshot>();
    callbacks.snapshot->root = "test collection";
    callbacks.snapshot->name = "Personal";
    callbacks.snapshot->agents = {
        { .id = "news", .name = "News", .profile = "codex", .model = "chosen", .revision = 1,
            .instructions = "Read the news only." },
        { .id = "mail", .name = "Mail", .profile = "codex", .model = "chosen", .revision = 1,
            .instructions = "Read inbox only." },
    };
    host.pump();
    CHECK(host.text().find("Read the news only.") != std::string::npos);
    CHECK(host.text().find("execution disabled") != std::string::npos);
    host.on_key({ .keycode = SDLK_DOWN, .pressed = true });
    host.pump();
    CHECK(host.text().find("Read inbox only.") != std::string::npos);
    CHECK(host.dispatch_action("personal.select/news"));
    CHECK(host.text().find("Read the news only.") != std::string::npos);
    host.request_close();
    CHECK_FALSE(host.is_running());
    CHECK(callbacks.snapshot->agents.size() == 2);
    InspectablePersonalHost reopened;
    REQUIRE(reopened.initialize(context, callbacks));
    CHECK(reopened.text().find("Read the news only.") != std::string::npos);
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
