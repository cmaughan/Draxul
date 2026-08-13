#include <draxul/plugin_api.h>

typedef char DraxulPluginAbiMustBeV2[
    DRAXUL_PLUGIN_ABI_VERSION == 2u ? 1 : -1];

int main(void)
{
    DraxulPluginViewportV2 viewport = { 0 };
    viewport.struct_size = sizeof(DraxulPluginViewportV2);
    viewport.width = 1;
    viewport.height = 1;
    return viewport.width == 1 && viewport.height == 1 ? 0 : 1;
}
