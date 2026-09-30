#pragma once

#include <draxul/control_plane.h>
#include <cstdio>
#include <functional>
#include <iostream>

namespace draxul
{

// Per-invocation ports: callers can capture IO and control routing without
// redirecting process-global streams. Defaults use the real local transport.
struct CliContext
{
    FILE* output = stdout;
    FILE* error = stderr;
    std::istream* input = &std::cin;
    std::function<ControlClientResult(std::string_view,
        const std::filesystem::path&, std::string_view, nlohmann::json)> request
        = [](std::string_view id, const std::filesystem::path& runtime,
              std::string_view method, nlohmann::json params) {
              return ControlClient::request(id, runtime, method, std::move(params));
          };
};

} // namespace draxul
