#pragma once

// Minimal RFC 4648 base64 encode/decode used by the OSC 52 clipboard sequence.
// Header-only so the tiny implementation can be inlined and reused by tests.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace draxul
{

inline std::string base64_encode(std::string_view input)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= input.size())
    {
        const uint32_t triple = (static_cast<uint8_t>(input[i]) << 16)
            | (static_cast<uint8_t>(input[i + 1]) << 8)
            | static_cast<uint8_t>(input[i + 2]);
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
        out.push_back(kAlphabet[triple & 0x3F]);
        i += 3;
    }
    if (i < input.size())
    {
        const uint32_t b0 = static_cast<uint8_t>(input[i]);
        const uint32_t b1 = (i + 1 < input.size()) ? static_cast<uint8_t>(input[i + 1]) : 0;
        const uint32_t triple = (b0 << 16) | (b1 << 8);
        out.push_back(kAlphabet[(triple >> 18) & 0x3F]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3F]);
        if (i + 1 < input.size())
            out.push_back(kAlphabet[(triple >> 6) & 0x3F]);
        else
            out.push_back('=');
        out.push_back('=');
    }
    return out;
}

inline std::optional<std::string> base64_decode(std::string_view input)
{
    auto value_of = [](char c) -> int {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        return -1;
    };

    // Strip ASCII whitespace and trailing padding to make length checks robust.
    std::string cleaned;
    cleaned.reserve(input.size());
    for (char c : input)
    {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
        cleaned.push_back(c);
    }
    while (!cleaned.empty() && cleaned.back() == '=')
        cleaned.pop_back();

    std::string out;
    out.reserve((cleaned.size() * 3) / 4);
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : cleaned)
    {
        const int v = value_of(c);
        if (v < 0)
            return std::nullopt;
        buffer = (buffer << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace draxul
