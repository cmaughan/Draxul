#include <draxul/nvim_protocol.h>
#include <draxul/mpack_codec.h>
#include <draxul/nvim_ui.h>

int main()
{
    draxul::MpackValue value = draxul::MpackValue::make_str("protocol");
    std::vector<char> encoded;
    return draxul::encode_mpack_value(value, encoded)
            && !encoded.empty()
        ? 0
        : 1;
}
