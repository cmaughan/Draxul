#include <draxul/filesystem_path_text.h>
#include "personal_chat_images.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 8192
#include <stb_image.h>

namespace draxul::personal_detail
{
namespace
{
constexpr size_t max_file=16*1024*1024,max_pixels=8*1024*1024,max_gpu=64*1024*1024;
std::string unescape(std::string_view text)
{
    std::string result;
    auto hex=[](char c){if(c>='0' && c<='9') return c-'0';if(c>='A' && c<='F')return c-'A'+10;if(c>='a' && c<='f')return c-'a'+10;return -1;};
    for(size_t i=0;i<text.size();++i)
    {
        unsigned char c=text[i];
        if(c=='%')
        {
            if(i+2>=text.size() || hex(text[i+1])<0 || hex(text[i+2])<0) return {};
            c=static_cast<unsigned char>(hex(text[i+1])*16+hex(text[i+2]));i+=2;
        }
        if(c<32 || c==127) return {};
        result+=static_cast<char>(c);
    }
    return result;
}
}
std::filesystem::path chat_image_path(std::string_view destination,const std::filesystem::path& base)
{
    // Decode Markdown/URL escapes, but never interpret network authorities as files.
    if(destination.starts_with("file://"))
    {
        destination.remove_prefix(7);
        if(destination.starts_with("localhost/")) destination.remove_prefix(9);
        if(!destination.starts_with('/')) return {};
#ifdef _WIN32
        if(destination.size()>3 && destination[2]==':') destination.remove_prefix(1);
#endif
    }
    else if(destination.find("://")!=std::string_view::npos || destination.starts_with("data:")) return {};
    const auto decoded=unescape(destination);
    if(decoded.empty()) return {};
    auto path=path_from_utf8(decoded);
    // Do not open network shares when the reply promises a local image.
    if(decoded.starts_with("//") || decoded.starts_with("\\\\")) return {};
    if(path.is_relative())
    {
        if(base.empty()) return {};
        path=base/path;
    }
    return path.lexically_normal();
}
DecodedChatImage decode_chat_image(const std::filesystem::path& path)
{
    DecodedChatImage result;
    if(path.empty()) {result.error="Only local image files are supported";return result;}
    std::error_code ec;
    if(!std::filesystem::is_regular_file(path,ec)) {result.error="Image file is unavailable";return result;}
    const auto bytes=std::filesystem::file_size(path,ec);
    if(ec || bytes==0 || bytes>max_file) {result.error="Image file exceeds 16 MiB or is empty";return result;}
    std::ifstream file(path,std::ios::binary);
    std::vector<unsigned char> data(static_cast<size_t>(bytes));
    if(!file.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size())))
    {result.error="Could not read image file";return result;}
    int channels=0;
    if(!stbi_info_from_memory(data.data(),static_cast<int>(data.size()),&result.width,&result.height,&channels)
        || result.width<=0 || result.height<=0)
    {result.width=result.height=0;result.error="Unsupported or damaged image";return result;}
    if(static_cast<size_t>(result.width)*result.height>max_pixels)
    {result.width=result.height=0;result.error="Image exceeds 8 megapixels";return result;}
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(data.data(),static_cast<int>(data.size()),&result.width,&result.height,&channels,4),stbi_image_free);
    if(!pixels) {result.width=result.height=0;result.error="Could not decode image";return result;}
    result.pixels.assign(pixels.get(),pixels.get()+static_cast<size_t>(result.width)*result.height*4);
    return result;
}
void ChatImages::begin(NVGcontext* vg,const std::filesystem::path& base)
{
    ++frame_;
    if(base_!=base)
    {
        for(const auto& [key,entry]:entries_) if(entry.texture) nvgDeleteImage(vg,entry.texture);
        reset();base_=base;
    }
}
std::pair<float,float> ChatImages::size(std::string_view destination)
{
    auto& entry=entries_[std::string(destination)];entry.referenced=frame_;
    return entry.width>0?std::pair<float,float>{entry.width,entry.height}:std::pair<float,float>{640,180};
}
bool ChatImages::poll()
{
    if(!worker_.valid() || worker_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return false;
    DecodedChatImage result;
    try { result=worker_.get(); } catch(...) { result.error="Could not load image"; }
    if(auto found=entries_.find(loading_);found!=entries_.end())
    {
        auto& entry=found->second;entry.width=result.width;entry.height=result.height;
        entry.pixels=std::move(result.pixels);entry.error=std::move(result.error);
    }
    loading_.clear();return true;
}
void ChatImages::draw(NVGcontext* vg,std::string_view destination,std::string_view alt,float x,float y,float width,float height)
{
    auto& entry=entries_[std::string(destination)];entry.referenced=entry.visible=frame_;
    if(!entry.texture && !entry.pixels.empty())
    {
        const auto bytes=entry.pixels.size();
        if(gpu_bytes_+bytes<=max_gpu)
        {
            entry.texture=nvgCreateImageRGBA(vg,entry.width,entry.height,0,entry.pixels.data());
            if(entry.texture) gpu_bytes_+=bytes;
            else entry.error="Could not upload image";
        }
        else entry.error="Visible images exceed the 64 MiB image budget";
        std::vector<unsigned char>().swap(entry.pixels);
    }
    if(entry.texture)
    {
        nvgBeginPath(vg);nvgRect(vg,x,y,width,height);
        nvgFillPaint(vg,nvgImagePattern(vg,x,y,width,height,0,entry.texture,1));nvgFill(vg);return;
    }
    if(entry.error.empty() && !worker_.valid())
    {
        loading_=destination;
        const auto path=chat_image_path(destination,base_);
        worker_=std::async(std::launch::async,[path]{return decode_chat_image(path);});
    }
    nvgSave(vg);nvgIntersectScissor(vg,x,y,width,height);
    nvgBeginPath(vg);nvgRect(vg,x,y,width,height);nvgFillColor(vg,nvgRGB(26,32,44));nvgFill(vg);
    const auto label=(alt.empty()?std::string("Image"):std::string(alt))+"\n"+(entry.error.empty()?"Loading image...":entry.error);
    nvgFillColor(vg,nvgRGB(159,175,199));nvgTextBox(vg,x+8,y+8,std::max(1.f,width-16),label.c_str(),nullptr);nvgRestore(vg);
}
void ChatImages::end(NVGcontext* vg)
{
    for(auto& [key,entry]:entries_)
    {
        if(entry.visible==frame_) continue;
        if(entry.texture)
        {
            nvgDeleteImage(vg,entry.texture);entry.texture=0;
            gpu_bytes_-=static_cast<size_t>(entry.width)*entry.height*4;
        }
        std::vector<unsigned char>().swap(entry.pixels);
    }
    std::erase_if(entries_,[this](const auto& pair){return pair.second.referenced!=frame_;});
}
void ChatImages::reset()
{
    // The caller destroys the old NanoVG context, which owns its textures.
    entries_.clear();gpu_bytes_=0;loading_.clear();
}
}
