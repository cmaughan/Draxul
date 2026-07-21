#include <draxul/bmp.h>

int main()
{
    const draxul::CapturedFrame frame;
    return frame.valid() ? 1 : 0;
}
