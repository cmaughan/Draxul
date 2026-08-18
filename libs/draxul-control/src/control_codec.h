#pragma once

#include "control_transport.h"

#include <draxul/control_plane.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace draxul::control_detail
{

uint64_t fnv1a(std::string_view text);
std::string endpoint_key(std::string_view session_id);
std::string session_key(std::string_view session_id);
std::string normalized_runtime_key(
    const std::filesystem::path& runtime_directory);
std::string random_token();

bool depth_within_limit(std::string_view text);
std::array<uint8_t, 4> frame_prefix(size_t size);
size_t frame_size(const std::array<uint8_t, 4>& prefix);

nlohmann::json response_json(
    std::string_view id, const ControlMethodResult& result);
std::string dump_wire_json(const nlohmann::json& value);
ControlMethodResult parse_request(std::string_view bytes,
    std::string_view expected_token, ControlRequest& request);

std::string encode_request(std::string_view id, std::string_view token,
    std::string_view method, const nlohmann::json& params,
    std::chrono::milliseconds timeout);
ControlClientResult parse_response(
    std::string_view bytes, std::string_view expected_id);

using ExactRead = std::function<TransportStatus(
    void*, size_t, TransportStage)>;
using ExactWrite = std::function<TransportStatus(
    const void*, size_t, TransportStage)>;

TransportStatus read_control_frame(
    const ExactRead& read_exact, std::string& bytes);
TransportStatus write_control_frame(
    const ExactWrite& write_exact, std::string_view bytes);

} // namespace draxul::control_detail
