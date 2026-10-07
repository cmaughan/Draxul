#include <catch2/catch_test_macros.hpp>
#include "fake_renderer.h"
#include "fake_window.h"
#include "support/test_support.h"
#include "support/temp_dir.h"
#include <draxul/personal_assistant_host.h>
#include "../modules/personal-assistant/src/personal_chat_font.h"
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
    std::shared_ptr<PersonalChatSnapshot> chat=std::make_shared<PersonalChatSnapshot>();
    struct Sent { std::string id, action, text, approval; };
    std::vector<Sent> sent;
    std::shared_ptr<const PersonalChatSnapshot> personal_chat(std::string_view) override { return chat; }
    std::string personal_chat_command(std::string_view id,std::string_view action,std::string_view text,std::string_view approval) override
    { sent.push_back({std::string(id),std::string(action),std::string(text),std::string(approval)});return "test-request"; }
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
    CHECK(host.text().find("Write a message")!=std::string::npos);
    CHECK(host.text().find("Model (explicit)")==std::string::npos);
    host.on_key({.keycode=SDLK_N,.pressed=true});
    REQUIRE(callbacks.opened.size()==1);
    CHECK(callbacks.opened.back().empty());
    REQUIRE(host.dispatch_action("personal.select/news"));
    callbacks.chat->state="idle";
    host.on_text_input({.text="Hello こんにちは"});
    host.on_key({.keycode=SDLK_RETURN,.mod=kModShift,.pressed=true});
    host.on_text_input({.text="Second line"});
    host.on_key({.keycode=SDLK_RETURN,.pressed=true});
    REQUIRE(callbacks.sent.size()==1);
    CHECK(callbacks.sent[0].id=="news");
    CHECK(callbacks.sent[0].text=="Hello こんにちは\nSecond line");
    callbacks.chat=std::make_shared<PersonalChatSnapshot>(*callbacks.chat);
    callbacks.chat->state="working";host.pump();
    host.on_key({.keycode=SDLK_ESCAPE,.pressed=true});
    REQUIRE(callbacks.sent.size()==2);CHECK(callbacks.sent.back().action=="stop");
    host.on_text_editing({.text="にほん"});
    host.on_key({.keycode=SDLK_RETURN,.pressed=true});
    CHECK(callbacks.sent.size()==2); // IME confirmation must not submit

    CHECK_FALSE(callbacks.command);
    callbacks.chat=std::make_shared<PersonalChatSnapshot>(*callbacks.chat);
    callbacks.chat->state="idle";callbacks.chat->last_command_id="test-request";
    callbacks.chat->last_command_error="Connection lost before confirmation.";
    host.pump();host.on_text_editing({.text=""});
    host.on_key({.keycode=SDLK_RETURN,.pressed=true});
    REQUIRE(callbacks.sent.size()==3);
    CHECK(callbacks.sent.back().text=="Hello こんにちは\nSecond line");
    host.request_close();
    CHECK_FALSE(host.is_running());
    CHECK(callbacks.sent.size()==3); // closing the UI does not stop its provider
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


TEST_CASE("personal chat reuses real NanoVG fonts with long bundle paths", "[personal][host][font]")
{
    tests::TempDir temp("personal-font");
    const auto folder=temp.path/std::string(80,'f');
    std::filesystem::create_directories(folder);
    const auto font_path=folder/"regular.ttf";
    std::filesystem::copy_file(tests::bundled_font_path(),font_path);
    REQUIRE(font_path.string().size()>63);
    NVGparams params{};
    params.renderCreate=[](void*){return 1;};
    params.renderCreateTexture=[](void*,int,int,int,int,const unsigned char*){return 1;};
    params.renderDeleteTexture=[](void*,int){return 1;};
    // Exercise the real NanoVG/fontstash allocator without requiring a GPU.
    std::unique_ptr<NVGcontext,decltype(&nvgDeleteInternal)> vg(nvgCreateInternal(&params),nvgDeleteInternal);
    REQUIRE(vg);
    const auto path=font_path.string();
    const int first=personal_detail::chat_font(vg.get(),path);
    REQUIRE(first>=0);
    for(int frame=0;frame<1000;++frame)
        REQUIRE(personal_detail::chat_font(vg.get(),path)==first);
    nvgFontFaceId(vg.get(),first);nvgFontSize(vg.get(),16);
    CHECK(nvgTextBounds(vg.get(),0,0,"Chat text",nullptr,nullptr)>0);
    // Context replacement after a font change can initialize independently.
    std::unique_ptr<NVGcontext,decltype(&nvgDeleteInternal)> second(nvgCreateInternal(&params),nvgDeleteInternal);
    REQUIRE(second);
    CHECK(personal_detail::chat_font(second.get(),path)==first);
}

#include "../modules/personal-assistant/src/personal_chat_images.h"
#include <fstream>
#include <draxul/filesystem_path_text.h>
#include <thread>

TEST_CASE("personal chart paths and decoding handle local reports and bad files", "[personal][host][image]")
{
    using namespace personal_detail;
    tests::TempDir temp("personal-images");
    const auto folder=temp.path/"chart space";
    std::filesystem::create_directories(folder);
    const auto source=std::filesystem::path(__FILE__).parent_path()/"render/fixtures/personal-chat-chart.png";
    const auto file=folder/std::filesystem::path(u8"株.png");
    std::filesystem::copy_file(source,file);
    auto decoded=decode_chat_image(file);
    CHECK(decoded.error.empty());CHECK(decoded.width==400);CHECK(decoded.height==112);
    CHECK(decoded.pixels.size()==400*112*4);
    CHECK(chat_image_path("chart%20space/株.png",temp.path)==file);
    CHECK(chat_image_path(path_to_utf8(file),{})==file);
    CHECK(chat_image_path("file://"+std::string(file.is_absolute() && !path_to_utf8(file).starts_with('/')?"/":"")+path_to_utf8(file),{})==file);
    CHECK(chat_image_path("https://example.com/image.png",temp.path).empty());
    CHECK(chat_image_path("file://remote/share.png",temp.path).empty());
    CHECK(chat_image_path("bad%00.png",temp.path).empty());
    CHECK(chat_image_path("relative.png",{}).empty());
    CHECK_FALSE(decode_chat_image(folder/"missing.png").error.empty());
    CHECK_FALSE(decode_chat_image(folder).error.empty());
    const auto invalid=folder/"invalid.png";
    {std::ofstream out(invalid);out<<"not an image";}
    CHECK_FALSE(decode_chat_image(invalid).error.empty());
    std::filesystem::resize_file(invalid,17*1024*1024);
    CHECK(decode_chat_image(invalid).error.find("16 MiB")!=std::string::npos);
}

TEST_CASE("personal chart textures are reused while visible and released on scroll", "[personal][host][image]")
{
    using namespace personal_detail;
    struct Textures {int next=0,created=0,deleted=0;};Textures textures;
    NVGparams params{};params.userPtr=&textures;
    params.renderCreate=[](void*){return 1;};
    params.renderCreateTexture=[](void* p,int,int,int,int,const unsigned char*){auto& t=*static_cast<Textures*>(p);++t.created;return ++t.next;};
    params.renderDeleteTexture=[](void* p,int){++static_cast<Textures*>(p)->deleted;return 1;};
    params.renderFill=[](void*,NVGpaint*,NVGcompositeOperationState,NVGscissor*,float,const float*,const NVGpath*,int){};
    params.renderGetTextureSize=[](void*,int,int* w,int* h){*w=400;*h=112;return 1;};
    std::unique_ptr<NVGcontext,decltype(&nvgDeleteInternal)> vg(nvgCreateInternal(&params),nvgDeleteInternal);
    REQUIRE(vg);
    const auto base=std::filesystem::path(__FILE__).parent_path()/"render/fixtures";
    ChatImages cache;
    auto paint=[&](bool visible){
        cache.begin(vg.get(),base);cache.size("personal-chat-chart.png");
        if(visible) cache.draw(vg.get(),"personal-chat-chart.png","Chart",0,0,400,112);
        cache.end(vg.get());
    };
    const int original=textures.created;
    paint(true);REQUIRE(cache.pending());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!cache.poll() && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE_FALSE(cache.pending());
    CHECK(cache.size("personal-chat-chart.png")==std::pair<float,float>{400,112});
    paint(true);CHECK(textures.created==original+1);
    for(int frame=0;frame<100;++frame) paint(true);
    CHECK(textures.created==original+1);CHECK_FALSE(cache.pending());
    const int deleted=textures.deleted;
    paint(false);CHECK(textures.deleted==deleted+1);
    paint(true);CHECK(cache.pending()); // returning loads again, instead of retaining hidden image memory
}
