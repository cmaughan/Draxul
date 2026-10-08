#include <draxul/filesystem_path_text.h>
#include <draxul/personal_assistant_host.h>
#include "personal_chat_font.h"
#include "personal_chat_images.h"
#include <draxul/text_service.h>
#include <draxul/window.h>
#include <draxul/unicode.h>
#include <draxul/app_config_types.h>
#include <draxul/markdown/markdown_parser.h>
#include <unordered_set>
#include <nanovg.h>
#include <SDL3/SDL_keycode.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace draxul
{
namespace
{
void box(NVGcontext* vg,float x,float y,float w,float h,float r,NVGcolor color)
{ nvgBeginPath(vg); nvgRoundedRect(vg,x,y,std::max(0.f,w),std::max(0.f,h),r); nvgFillColor(vg,color); nvgFill(vg); }
}
PersonalAssistantHost::PersonalAssistantHost():images_(std::make_unique<personal_detail::ChatImages>()) {}
PersonalAssistantHost::~PersonalAssistantHost()=default;
bool PersonalAssistantHost::initialize(const HostContext& context,IHostCallbacks& callbacks)
{
    TextServiceConfig fonts;
    if(context.config)
    {
        fonts.font_path=context.config->font_path;
        fonts.bold_font_path=context.config->bold_font_path;
        fonts.italic_font_path=context.config->italic_font_path;
        fonts.bold_italic_font_path=context.config->bold_italic_font_path;
        fonts.fallback_paths=context.config->fallback_paths;
        fonts.enable_ligatures=context.config->enable_ligatures;
    }
    if(fonts.font_path.empty() && context.text_service) fonts.font_path=context.text_service->primary_font_path();
    display_ppi_=std::max(1.f,context.display_ppi);
    if(!rich_text_.initialize(fonts,TextService::DEFAULT_POINT_SIZE,display_ppi_))
    { init_error_="Unable to initialize personal chat Markdown fonts.";return false; }
    markdown_pass_=markdown::create_markdown_render_pass();
    return GridHostBase::initialize(context,callbacks);
}
void PersonalAssistantHost::on_viewport_changed()
{
    apply_grid_size(std::max(1,viewport().grid_size.x),std::max(1,viewport().grid_size.y)); changed();
}
bool PersonalAssistantHost::initialize_host()
{
    highlights().set_default_bg(color_from_rgb(0x11151D));
    highlights().set_default_fg(color_from_rgb(0xE7ECF4));
    grid_pipeline().set_enable_ligatures(false);
    set_cursor_busy(true);
    selected_id_=launch_options().source_path;
    pass_=create_nanovg_pass(); running_=true;
    pump(); return true;
}
void PersonalAssistantHost::shutdown()
{
    // The server owns the conversation. Closing a presentation must not stop it.
    running_=false; images_->reset(); pass_.reset(); markdown_pass_.reset(); rich_text_.shutdown(); markdown_cache_.clear(); chat_.reset();
}
void PersonalAssistantHost::changed() { dirty_=true; callbacks().request_frame(); }
std::string PersonalAssistantHost::status_text() const
{
    if(!chat_) return selected_id_.empty()?"Personal conversations":"Connecting...";
    if(chat_->state=="working") return "Working...";
    if(chat_->state=="approval") return "Needs your approval";
    if(chat_->state=="idle") return "Ready";
    return chat_->state=="error"?"Needs attention":"Starting assistant...";
}
void PersonalAssistantHost::pump()
{
    if(!running_) return;
    if(images_->poll()) {markdown_cache_.clear();dirty_=true;}
    if(images_->pending()) callbacks().request_frame();
    const auto metadata=callbacks().personal_agents();
    if(metadata!=snapshot_)
    {
        snapshot_=metadata; dirty_=true;
        if(snapshot_)
            for(const auto& agent:snapshot_->agents)
                if(agent.id==selected_id_) { name_=agent.name; callbacks().set_window_title(name_); }
    }
    if(!selected_id_.empty())
    {
        const auto state=callbacks().personal_chat(selected_id_);
        if(state!=chat_)
        {
            chat_=state; dirty_=true;
            // Keep a rejected draft available for editing/resending, never silently discard it.
            if(chat_ && !submitted_draft_.empty())
            {
                const bool accepted=std::ranges::any_of(chat_->messages,[this](const auto& m){return m.role=="user" && m.id==submitted_id_;});
                if(accepted) submitted_draft_.clear();
                else if(!chat_->error.empty() || (chat_->last_command_id==submitted_id_ && !chat_->last_command_error.empty())) { if(draft_.empty()) { draft_=submitted_draft_; cursor_=draft_.size(); } submitted_draft_.clear(); }
            }
        }
    }
    if(dirty_)
    {
        // Keep debug/readiness text available to the host harness; rendering is native vector UI.
        grid().clear(); write_line(0,name_); write_line(2,status_text());
        write_line(4,"Write a message below. Enter sends; Shift+Enter adds a line.");
        set_content_ready(true); flush_grid(); dirty_=false; callbacks().request_frame();
    }
}
void PersonalAssistantHost::write_line(int row,std::string_view text)
{
    if(row>=grid_rows()) return;
    int column=1;
    for(const auto& cluster:display_clusters(text))
    {
        if(column+cluster.cell_width>=grid_cols()) break;
        grid().set_cell(column,row,cluster.text,0,cluster.cell_width==2); column+=cluster.cell_width;
    }
}
void PersonalAssistantHost::insert(std::string_view text)
{
    if(selected_id_.empty()) return;
    const size_t existing=select_all_?0:draft_.size();
    if(existing+text.size()>16384) { error_="Messages can be up to 16 KiB."; changed(); return; }
    if(select_all_) { draft_.clear(); cursor_=0; select_all_=false; }
    draft_.insert(cursor_,text); cursor_+=text.size(); preedit_.clear(); error_.clear(); changed();
}
void PersonalAssistantHost::send()
{
    if(!preedit_.empty() || draft_.find_first_not_of(" \t\r\n")==std::string::npos) return;
    if(!chat_ || chat_->state!="idle" || !submitted_draft_.empty()) { error_="Wait until this conversation is ready."; changed(); return; }
    const auto request_id=callbacks().personal_chat_command(selected_id_,"send",draft_);
    if(!request_id.empty())
    { submitted_id_=request_id; submitted_draft_=draft_; draft_.clear(); cursor_=0; select_all_=false; scroll_=0; error_.clear(); }
    else error_="Message could not be queued. Your draft has been kept.";
    changed();
}
void PersonalAssistantHost::on_text_input(const TextInputEvent& event) { insert(event.text); }
void PersonalAssistantHost::on_text_editing(const TextEditingEvent& event) { preedit_=event.text; changed(); }
void PersonalAssistantHost::on_key(const KeyEvent& event)
{
    if(!event.pressed) return;
    if(selected_id_.empty()) { if(event.keycode==SDLK_N || event.keycode==SDLK_RETURN) callbacks().open_personal_agent({}); return; }
    const bool command=(event.mod&(kModCtrl|kModSuper))!=0;
    if(command && event.keycode==SDLK_A) select_all_=true;
    else if(command && event.keycode==SDLK_V) insert(window().clipboard_text());
    else if(command && event.keycode==SDLK_C) dispatch_action("copy");
    else if(command && event.keycode==SDLK_X && select_all_)
    { window().set_clipboard_text(draft_); draft_.clear(); cursor_=0; select_all_=false; }
    else if(event.keycode==SDLK_RETURN && preedit_.empty())
    { if(event.mod&kModShift) insert("\n"); else send(); }
    else if(event.keycode==SDLK_ESCAPE)
    { preedit_.clear(); select_all_=false; if(chat_ && (chat_->state=="working" || chat_->state=="approval")) callbacks().personal_chat_command(selected_id_,"stop"); }
    else if(preedit_.empty() && (event.keycode==SDLK_BACKSPACE || event.keycode==SDLK_DELETE))
    {
        if(select_all_) { draft_.clear(); cursor_=0; select_all_=false; }
        else if(event.keycode==SDLK_BACKSPACE && cursor_>0)
        {
            size_t previous=0;
            for(const auto& cluster:display_clusters(draft_)) { if(cluster.byte_end>=cursor_) {previous=cluster.byte_start;break;} }
            draft_.erase(previous,cursor_-previous); cursor_=previous;
        }
        else if(cursor_<draft_.size()) draft_.erase(cursor_,next_display_cluster_end(draft_,cursor_)-cursor_);
    }
    else if(event.keycode==SDLK_HOME) {cursor_=0;select_all_=false;}
    else if(event.keycode==SDLK_END) {cursor_=draft_.size();select_all_=false;}
    else if(event.keycode==SDLK_LEFT && cursor_>0)
    {
        for(const auto& cluster:display_clusters(draft_)) if(cluster.byte_end>=cursor_) {cursor_=cluster.byte_start;break;}
        select_all_=false;
    }
    else if(event.keycode==SDLK_RIGHT && cursor_<draft_.size()) {cursor_=next_display_cluster_end(draft_,cursor_);select_all_=false;}
    changed();
}
bool PersonalAssistantHost::dispatch_action(std::string_view action)
{
    if(action=="personal.new") {callbacks().open_personal_agent({});return true;}
    if(action=="paste") {insert(window().clipboard_text());return true;}
    if(action=="copy")
    {
        if(select_all_) window().set_clipboard_text(draft_);
        else if(chat_) for(auto it=chat_->messages.rbegin();it!=chat_->messages.rend();++it)
            if(it->role=="assistant") {window().set_clipboard_text(it->text);break;}
        return true;
    }
    if(action=="personal.send") {send();return true;}
    if(!action.starts_with("personal.select/")) return false;
    const std::string id(action.substr(16));
    if(id!=selected_id_)
    {selected_id_=id;chat_.reset();draft_.clear();cursor_=0;scroll_=0;submitted_draft_.clear();}
    changed();pump();return true;
}
std::string_view PersonalAssistantHost::link_at(int px,int py) const
{
    const float x=static_cast<float>(px-markdown_viewport_.x),y=static_cast<float>(py-markdown_viewport_.y);
    if(x<0 || y<0 || x>=markdown_viewport_.width || y>=markdown_viewport_.height) return {};
    return markdown::markdown_link_at(markdown_frame_,x,y);
}
std::optional<MouseCursor> PersonalAssistantHost::mouse_cursor_at(int px,int py) const
{
    return link_at(px,py).empty()?std::nullopt:std::optional<MouseCursor>(MouseCursor::Pointer);
}
void PersonalAssistantHost::on_mouse_button(const MouseButtonEvent& event)
{
    if(!event.pressed) return;
    if(event.button==1)
    {
        const auto link=link_at(event.pos.x,event.pos.y);
        if(!link.empty())
        {
            if(!window().open_url(link)) callbacks().push_toast(2,"Failed to open link.");
            return;
        }
    }
    const glm::vec2 point=glm::vec2(event.pos-viewport().pixel_pos)/std::max(.1f,viewport().pixel_scale);
    for(auto it=hits_.rbegin();it!=hits_.rend();++it)
    {
        const auto& r=it->rect;
        if(point.x<r.x || point.y<r.y || point.x>=r.x+r.z || point.y>=r.y+r.w) continue;
        if(it->action=="copy" && event.button==3) window().set_clipboard_text(it->text);
        if(event.button!=1) return;
        if(it->action=="new") callbacks().open_personal_agent({});
        else if(it->action=="send") send();
        else if(it->action=="reconnect") callbacks().personal_chat_command(selected_id_,"reconnect");
        else if(it->action=="stop") callbacks().personal_chat_command(selected_id_,"stop");
        else if((it->action=="approve" || it->action=="decline") && chat_)
            callbacks().personal_chat_command(selected_id_,it->action,{},chat_->approval_id);
        changed(); return;
    }
}
void PersonalAssistantHost::on_mouse_wheel(const MouseWheelEvent& event)
{ scroll_=std::clamp(scroll_+event.delta.y*44.f,0.f,max_scroll_); changed(); }
void PersonalAssistantHost::draw(IFrameContext& frame)
{
    if(!pass_) return;
    const auto& font_path=text_service().primary_font_path();
    if(nanovg_font_path_!=font_path)
    {
        images_->reset();
        pass_=create_nanovg_pass();
        nanovg_font_path_=font_path;
    }
    pass_->set_draw_callback([this](NVGcontext* vg,int width,int height){paint(vg,width,height);});
    const auto& vp=viewport();
    frame.record_render_pass(*pass_,{vp.pixel_pos.x,vp.pixel_pos.y,vp.pixel_size.x,vp.pixel_size.y});
    if(markdown_pass_ && !markdown_frame_.rows.empty())
    {
        auto list=markdown::build_markdown_draw_list(markdown_frame_,markdown_theme_,rich_text_,
            {.viewport_width=markdown_viewport_.width,.viewport_height=markdown_viewport_.height,.pixel_scale=vp.pixel_scale});
        auto uploads=markdown::collect_markdown_atlas_uploads(rich_text_,list.used_atlas_ids,++markdown_revision_);
        markdown_pass_->set_draw_list(std::move(list),std::move(uploads),markdown_revision_);
        frame.record_render_pass(*markdown_pass_,markdown_viewport_);
    }
    if(images_->pending()) callbacks().request_frame();
    frame.flush_submit_chunk();
}
void PersonalAssistantHost::paint(NVGcontext* vg,int width,int height)
{
    const float scale=std::max(.1f,viewport().pixel_scale);
    const float w=width/scale,h=height/scale;
    const float font_size=std::clamp(static_cast<float>(text_service().metrics().cell_height)/scale*.79f,13.f,24.f);
    const int font=personal_detail::chat_font(vg,text_service().primary_font_path());
    nvgSave(vg);nvgScale(vg,scale,scale);
    box(vg,0,0,w,h,0,nvgRGB(17,21,29));
    nvgFontFaceId(vg,font);nvgFontSize(vg,font_size);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);nvgTextLineHeight(vg,1.35f);
    hits_.clear(); markdown_frame_={};
    images_->begin(vg,snapshot_ && !snapshot_->root.empty()?
        path_from_utf8(snapshot_->root)/"agents"/selected_id_:std::filesystem::path{});
    const float point_size=font_size*scale*72.f/display_ppi_;
    if(point_size!=markdown_point_size_)
    {
        markdown_point_size_=point_size; markdown_cache_.clear();
        markdown_theme_=markdown::default_markdown_theme(point_size);
        markdown_theme_.document_background=Color(0,0,0,0);
        for(auto* style:{&markdown_theme_.body,&markdown_theme_.heading1,&markdown_theme_.heading2,
            &markdown_theme_.heading3,&markdown_theme_.heading4,&markdown_theme_.heading5,
            &markdown_theme_.heading6,&markdown_theme_.code}) style->line_height_multiplier=1.05f;
    }
    const float pad=std::min(16.f,w*.05f),content_w=std::max(40.f,w-2*pad);
    auto text=[&](float x,float y,float wrap,const std::string& value,NVGcolor color){nvgFillColor(vg,color);nvgTextBox(vg,x,y,wrap,value.c_str(),nullptr);};
    const float button_h=std::max(30.f,font_size+12.f);
    auto button=[&](float x,float y,float bw,const std::string& label,const std::string& action,NVGcolor color){
        box(vg,x,y,bw,button_h,button_h*.5f,color);
        nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
        nvgFillColor(vg,nvgRGB(245,248,255));nvgText(vg,x+bw*.5f,y+button_h*.5f,label.c_str(),nullptr);
        nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_TOP);
        hits_.push_back({{x,y,bw,button_h},action,{}});
    };
    text(pad,17,content_w-80,name_,nvgRGB(239,243,251));
    nvgFontSize(vg,font_size*.78f);text(pad,42,content_w-80,status_text(),nvgRGB(146,161,186));nvgFontSize(vg,font_size);
    button(w-pad-64,17,64,"New","new",nvgRGB(46,58,78));
    if(selected_id_.empty())
    {
        const std::string title="A quieter place to talk.";
        text(pad,110,content_w,title,nvgRGB(239,243,251));
        text(pad,151,content_w,"Choose a personal agent in the sidebar, or press New to start a conversation.\n\nYour messages and answers appear here. Routine tool calls stay out of the chat.",nvgRGB(159,175,199));
        if(snapshot_ && !snapshot_->error.empty()) text(pad,300,content_w,snapshot_->error,nvgRGB(255,171,145));
        images_->end(vg);nvgRestore(vg);return;
    }
    const bool approval=chat_ && chat_->state=="approval";
    const float composer_h=std::min(84.f,std::max(52.f,h*.20f));
    const float composer_y=h-pad-composer_h;
    const float history_top=65,history_bottom=std::max(history_top,composer_y-26-(approval?44:0));
    const float bubble_w=std::max(40.f,std::min(700.f,content_w*.87f));
    struct Bubble{float x,y,w,h;std::string text;bool user;const markdown::LayoutDocument* markdown=nullptr;};
    std::vector<Bubble> bubbles;float total=0;
    float ascender=0,descender=0,line_height=0;
    nvgTextMetrics(vg,&ascender,&descender,&line_height);
    // NanoVG box bounds include a full line advance below the final baseline.
    // Keep inter-line spacing, but remove that trailing leading from the bubble.
    const float trailing_leading=std::max(0.f,line_height-(ascender-descender));
    constexpr float bubble_pad_y=8.f;
    auto add=[&](const std::string& value,bool user){
        float bounds[4];nvgTextBoxBounds(vg,0,0,bubble_w-28,value.c_str(),nullptr,bounds);
        const float bh=std::max(ascender-descender,bounds[3]-bounds[1]-trailing_leading)+2*bubble_pad_y;
        bubbles.push_back({user?w-pad-bubble_w:pad,total,bubble_w,bh,value,user});total+=bh+12;
    };
    std::unordered_set<std::string> retained;
    if(chat_) for(const auto& message:chat_->messages)
    {
        if(message.role!="assistant") { add(message.text,message.role=="user");continue; }
        retained.insert(message.id);
        auto [entry,inserted]=markdown_cache_.try_emplace(message.id);
        auto& cached=entry->second;
        const float text_width=(bubble_w-28)*scale;
        if(inserted || cached.source!=message.text || cached.width!=text_width || cached.scale!=scale)
        {
            cached.source=message.text;cached.width=text_width;cached.scale=scale;
            auto parsed=markdown::parse_markdown({},message.text);
            cached.layout=markdown::layout_markdown_document(parsed.document,markdown_theme_,
                [this](const RichTextStyleKey& style){return rich_text_.metrics_for(style);},
                {.viewport_width=text_width,.pixel_scale=scale,.margin_columns=0,
                    .image_size=[this](std::string_view destination){return images_->size(destination);}});
        }
        for(const auto& row:cached.layout.rows)
            for(const auto& image:row.images) images_->size(image.destination);
        // The document viewer adds a gap after each block; the bubble owns its outer padding.
        const float body_h=cached.layout.rows.empty()?font_size:
            (cached.layout.rows.back().y+cached.layout.rows.back().height)/scale;
        const float bh=body_h+2*bubble_pad_y;
        bubbles.push_back({pad,total,bubble_w,bh,message.text,false,&cached.layout});total+=bh+12;
    }
    std::erase_if(markdown_cache_,[&](const auto& entry){return !retained.contains(entry.first);});
    if(chat_ && !chat_->error.empty()) add(chat_->error,false);
    if(chat_ && !chat_->last_command_error.empty()) add(chat_->last_command_error,false);
    if(approval) add("Approval requested\n\n"+chat_->approval_text,false);
    if(bubbles.empty()) add("What would you like help with?",false);
    max_scroll_=std::max(0.f,total-(history_bottom-history_top));scroll_=std::clamp(scroll_,0.f,max_scroll_);
    const float top=history_top-max_scroll_+scroll_;
    nvgScissor(vg,0,history_top,w,std::max(0.f,history_bottom-history_top));
    markdown_viewport_={viewport().pixel_pos.x+static_cast<int>(std::round((pad+14)*scale)),
        viewport().pixel_pos.y+static_cast<int>(std::round(history_top*scale)),
        std::max(1,static_cast<int>(std::floor((bubble_w-28)*scale))),
        std::max(0,static_cast<int>(std::floor((history_bottom-history_top)*scale)))};
    for(const auto& bubble:bubbles)
    {
        const float y=top+bubble.y;
        if(y+bubble.h<history_top || y>history_bottom) continue;
        box(vg,bubble.x,y,bubble.w,bubble.h,17,bubble.user?nvgRGB(41,104,218):nvgRGB(37,44,58));
        if(bubble.markdown)
        {
            const float offset=(y+bubble_pad_y-history_top)*scale;
            for(auto row:bubble.markdown->rows)
            {
                row.y+=offset;row.baseline+=offset;
                for(auto& image:row.images)
                {
                    image.y+=offset;
                    if(image.y+image.height>0 && image.y<markdown_viewport_.height)
                        images_->draw(vg,image.destination,image.alt,pad+14+image.x/scale,
                            history_top+image.y/scale,image.width/scale,image.height/scale);
                }
                for(auto& decoration:row.decorations) decoration.y+=offset;
                for(auto& run:row.runs)
                {
                    run.baseline+=offset;
                    if(markdown::is_web_link(run.link_destination))
                    {
                        run.color=markdown_theme_.accent;
                        row.decorations.push_back({.kind=markdown::Decoration::Kind::Divider,
                            .x=run.x,.y=run.baseline+2*scale,.width=run.width,.height=scale,
                            .color=markdown_theme_.accent});
                    }
                }
                markdown_frame_.rows.push_back(std::move(row));
            }
        }
        else text(bubble.x+14,y+bubble_pad_y,bubble.w-28,bubble.text,nvgRGB(240,244,251));
        const float hit_y=std::max(y,history_top),hit_end=std::min(y+bubble.h,history_bottom);
        hits_.push_back({{bubble.x,hit_y,bubble.w,hit_end-hit_y},"copy",bubble.text});
    }
    images_->end(vg);
    nvgResetScissor(vg);
    if(max_scroll_>0) box(vg,w-6,history_top+(history_bottom-history_top-30)*(1-scroll_/max_scroll_),3,30,1.5f,nvgRGB(79,92,115));
    if(approval)
    {
        button(pad,composer_y-77,96,"Allow","approve",nvgRGB(36,114,79));
        button(pad+108,composer_y-77,104,"Decline","decline",nvgRGB(116,60,66));
    }
    nvgFontSize(vg,font_size*.74f);
    std::string status=error_.empty()?(chat_?chat_->error:""):error_;
    if(status.empty()) status=chat_ && chat_->state=="working"?"Working quietly...  Esc stops":"Enter sends  ·  Shift+Enter adds a line";
    nvgScissor(vg,pad,composer_y-24,content_w,20);text(pad,composer_y-22,content_w,status,nvgRGB(157,175,200));nvgResetScissor(vg);
    nvgFontSize(vg,font_size);
    box(vg,pad,composer_y,content_w,composer_h,18,nvgRGB(29,36,49));
    const float input_w=std::max(20.f,content_w-112);
    nvgScissor(vg,pad+12,composer_y+10,input_w,composer_h-20);
    std::string edit=draft_;edit.insert(cursor_,preedit_.empty()?"|":preedit_+"|");
    float bounds[4];const auto prefix=edit.substr(0,cursor_+preedit_.size()+1);
    nvgTextBoxBounds(vg,0,0,input_w,prefix.c_str(),nullptr,bounds);
    const float edit_scroll=std::max(0.f,bounds[3]-bounds[1]-(composer_h-28));
    if(select_all_) box(vg,pad+12,composer_y+10,input_w,composer_h-20,3,nvgRGB(49,71,108));
    if(draft_.empty() && preedit_.empty()) text(pad+24,composer_y+14,input_w-10,"Message...",nvgRGB(129,145,168));
    text(pad+14,composer_y+14-edit_scroll,input_w,edit,nvgRGB(236,242,252));nvgResetScissor(vg);
    const bool busy=chat_ && (chat_->state=="working" || chat_->state=="approval");
    const bool failed=chat_ && chat_->state=="error";
    button(w-pad-84,composer_y+(composer_h-button_h)*.5f,72,failed?"Retry":busy?"Stop":"Send",failed?"reconnect":busy?"stop":"send",busy?nvgRGB(108,57,66):nvgRGB(41,104,218));
    callbacks().set_text_input_area(static_cast<int>((pad+14)*scale)+viewport().pixel_pos.x,
        static_cast<int>((composer_y+14)*scale)+viewport().pixel_pos.y,static_cast<int>(input_w*scale),static_cast<int>((composer_h-20)*scale));
    nvgRestore(vg);
}
void register_personal_assistant_host_provider(HostProviderRegistry& registry)
{registry.register_provider(HostKind::PersonalAssistant,[]{return std::make_unique<PersonalAssistantHost>();});}
}
