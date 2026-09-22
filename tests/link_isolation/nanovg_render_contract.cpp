#include <draxul/nanovg_pass.h>

int main()
{
    auto pass = draxul::create_nanovg_pass();
    if (!pass)
        return 1;
    pass->set_draw_callback({});
    return 0;
}
