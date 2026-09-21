#include <draxul/cluster_blit.h>
#include <draxul/font_metrics.h>
#include <draxul/rich_text_service.h>
#include <draxul/text_atlas_builder.h>
#include <draxul/text_service.h>

int main()
{
    draxul::TextService text;
    return text.point_size() < 0.0f ? 1 : 0;
}
