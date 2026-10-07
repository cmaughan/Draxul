#pragma once
#include <filesystem>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>
#include <nanovg.h>

namespace draxul::personal_detail
{
struct DecodedChatImage
{
    int width=0,height=0;
    std::vector<unsigned char> pixels;
    std::string error;
};
std::filesystem::path chat_image_path(std::string_view destination,const std::filesystem::path& base);
DecodedChatImage decode_chat_image(const std::filesystem::path& path);

// One worker at a time; only visible images decode. GPU resources belong to the
// current NanoVG context and are released as soon as an image leaves the viewport.
class ChatImages
{
public:
    void begin(NVGcontext* vg,const std::filesystem::path& base);
    std::pair<float,float> size(std::string_view destination);
    void draw(NVGcontext* vg,std::string_view destination,std::string_view alt,
        float x,float y,float width,float height);
    void end(NVGcontext* vg);
    bool poll();
    bool pending() const { return worker_.valid(); }
    void reset();
private:
    struct Entry
    {
        int width=0,height=0,texture=0;
        std::vector<unsigned char> pixels;
        std::string error;
        uint64_t referenced=0,visible=0;
    };
    std::unordered_map<std::string,Entry> entries_;
    std::filesystem::path base_;
    std::future<DecodedChatImage> worker_;
    std::string loading_;
    uint64_t frame_=0;
    size_t gpu_bytes_=0;
};
}
