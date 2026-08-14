#include <draxul/satview/satview_texture_assets.h>

#include <filesystem>

namespace
{

struct SatViewTestAssetRoot
{
    SatViewTestAssetRoot()
    {
        draxul::satview::set_satview_asset_root(
            std::filesystem::path(DRAXUL_SATVIEW_TEST_ASSET_ROOT));
    }
};

SatViewTestAssetRoot test_asset_root;

} // namespace
