#include "sign_label_atlas.h"

#include <draxul/text_service.h>

namespace draxul
{

std::shared_ptr<SignLabelAtlas> build_sign_label_atlas(
    TextService& text_service,
    const std::vector<SignLabelRequest>& requests,
    uint64_t revision)
{
    std::vector<TextAtlasRequest> atlas_requests;
    atlas_requests.reserve(requests.size());
    for (const SignLabelRequest& request : requests)
    {
        TextAtlasRequest atlas_request;
        atlas_request.key = request.key;
        atlas_request.text = request.text;
        atlas_request.target_pixel_size = {
            request.target_pixel_width,
            request.target_pixel_height,
        };
        atlas_request.horizontal_align = request.align_primary_to_start
            ? TextAtlasHorizontalAlign::Start
            : TextAtlasHorizontalAlign::Center;
        atlas_request.vertical_align = request.vertical_align == SignLabelVerticalAlign::Top
            ? TextAtlasVerticalAlign::Top
            : TextAtlasVerticalAlign::Center;
        atlas_request.color = {
            request.text_r,
            request.text_g,
            request.text_b,
            255,
        };
        atlas_requests.push_back(std::move(atlas_request));
    }
    auto atlas = std::make_shared<SignLabelAtlas>();
    static_cast<TextAtlas&>(*atlas) = build_text_atlas(text_service, atlas_requests, revision);
    return atlas;
}

} // namespace draxul
