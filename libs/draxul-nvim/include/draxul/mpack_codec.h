#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <draxul/nvim_protocol.h>

namespace draxul
{

bool encode_mpack_value(const MpackValue& value, std::vector<char>& out);
// On failure, when `hard_error` is non-null it is set to true if the data is
// definitely structurally invalid (e.g. starts with the reserved 0xC1 prefix byte)
// and the caller should advance past the bad byte to recover. When false the failure
// may simply be a truncated value and the caller should wait for more data.
bool decode_mpack_value(std::span<const uint8_t> bytes, MpackValue& value, size_t* consumed = nullptr,
    bool* hard_error = nullptr);

bool encode_rpc_request(
    uint32_t msgid, const std::string& method, const std::vector<MpackValue>& params, std::vector<char>& out);
bool encode_rpc_notification(
    const std::string& method, const std::vector<MpackValue>& params, std::vector<char>& out);
bool encode_rpc_response(
    uint32_t msgid, const MpackValue& error, const MpackValue& result, std::vector<char>& out);

} // namespace draxul
