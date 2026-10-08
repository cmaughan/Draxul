#pragma once
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace draxul
{
// Single worker ownership. No terminal emulation, shell expansion or GUI-thread IO.
class PersonalChatProcess
{
public:
    PersonalChatProcess();
    ~PersonalChatProcess();
    void start(const std::string& executable, const std::vector<std::string>& args,
        const std::filesystem::path& directory);
    void write(std::string_view text, std::stop_token stop);
    // Drains available bytes only. stderr is kept separate from the JSON protocol.
    std::string read();
    std::string diagnostics();
    bool running();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
