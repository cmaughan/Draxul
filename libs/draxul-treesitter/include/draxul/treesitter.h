#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace draxul
{

enum class SymbolKind
{
    Function,
    Class,
    Struct,
    Include,
};

struct SymbolRecord
{
    SymbolKind kind = SymbolKind::Function;
    std::string name;
    std::string parent; // for methods: owning class (e.g. "Foo" for Foo::bar); empty for free functions
    bool is_abstract = false; // Class/Struct only: has at least one pure-virtual member
    uint32_t line = 0; // 1-based
};

struct ParseError
{
    uint32_t line = 0; // 1-based
    uint32_t col = 0; // 0-based
};

struct ParsedFile
{
    std::string path; // relative to scan root, forward-slash separated
    std::vector<SymbolRecord> symbols;
    std::vector<ParseError> errors;
};

struct CodebaseSnapshot
{
    std::vector<ParsedFile> files;
    std::chrono::steady_clock::time_point scan_time;
    bool complete = false;
};

// Scans a source tree in a background thread using tree-sitter's C++ grammar.
// Call start() once; snapshot() returns the latest atomic snapshot at any time.
// Call stop() (or destroy the object) to join the thread cleanly.
class CodebaseScanner
{
public:
    CodebaseScanner();
    ~CodebaseScanner();

    void start(std::filesystem::path root);
    void stop();

    // Thread-safe. Returns nullptr before the first partial snapshot is ready.
    std::shared_ptr<const CodebaseSnapshot> snapshot() const;

private:
    void scan_thread(std::filesystem::path root);
    void publish(std::shared_ptr<CodebaseSnapshot> snap);

    mutable std::mutex mutex_;
    std::shared_ptr<const CodebaseSnapshot> snapshot_;
    std::thread thread_;
    std::atomic<bool> stop_flag_{ false };
};

} // namespace draxul
