#pragma once

// Pure conversion functions shared between NvimHost and clipboard unit tests.
// Keeping these as free functions ensures the tests exercise the same algorithm
// that runs in production rather than a separately maintained copy.

#include <draxul/nvim.h>

#include <string>
#include <vector>

namespace draxul
{

// Join the lines array from a clipboard_set notification into a single string.
// params layout: [register, lines_array, regtype]
inline std::string clipboard_params_to_text(const std::vector<MpackValue>& params)
{
    if (params.size() < 3 || params[1].type() != MpackValue::Array)
        return {};

    const auto& lines = params[1].as_array();
    std::string text;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i > 0)
            text += '\n';
        if (lines[i].type() == MpackValue::String)
            text += lines[i].as_str();
    }
    return text;
}

// Split a plain text string into the [lines_array, regtype] MpackValue shape
// expected by Neovim's clipboard_get RPC response.
inline MpackValue clipboard_text_to_response(const std::string& text)
{
    std::vector<MpackValue> lines;
    std::string::size_type pos = 0;
    while (pos <= text.size())
    {
        auto nl = text.find('\n', pos);
        if (nl == std::string::npos)
        {
            lines.push_back(NvimRpc::make_str(text.substr(pos)));
            break;
        }
        lines.push_back(NvimRpc::make_str(text.substr(pos, nl - pos)));
        pos = nl + 1;
    }
    if (lines.empty())
        lines.push_back(NvimRpc::make_str(""));

    return NvimRpc::make_array({ NvimRpc::make_array(std::move(lines)), NvimRpc::make_str("v") });
}

} // namespace draxul
