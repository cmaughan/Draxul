#include <draxul/cluster_blit.h>
#include <draxul/font_metrics.h>
#include <draxul/rich_text_service.h>
#include <draxul/text_atlas_builder.h>
#include <draxul/text_service.h>

// This target links only the public draxul-font contract. Native headers are
// intentionally unavailable to its compiler even though the final static link
// still pulls in FreeType and HarfBuzz for draxul-font's implementation.
#if __has_include(<ft2build.h>)
#error "draxul-font leaks FreeType include paths through its public interface"
#endif
#if __has_include(<hb.h>)
#error "draxul-font leaks HarfBuzz include paths through its public interface"
#endif

int main()
{
    draxul::TextService text;
    return text.point_size() < 0.0f ? 1 : 0;
}
