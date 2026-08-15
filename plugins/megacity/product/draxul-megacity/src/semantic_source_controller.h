#pragma once

#include <chrono>
#include <draxul/code_semantic_model.h>
#include <draxul/treesitter.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace draxul
{

struct SemanticSourceUpdate
{
    std::shared_ptr<const CodebaseSnapshot> parsed_snapshot;
    std::shared_ptr<const CodeSemanticSnapshot> semantic_snapshot;
    std::vector<std::string> available_modules;
    double scan_ms = 0.0;
    double semantic_ms = 0.0;
};

// Owns scanner lifetime and turns its completed immutable parse snapshot into
// the equally immutable semantic snapshot consumed by Megacity presentation.
class SemanticSourceController
{
public:
    explicit SemanticSourceController(std::filesystem::path root = {});
    ~SemanticSourceController();

    void set_root(std::filesystem::path root);
    const std::filesystem::path& root() const;

    void start();
    void stop();
    bool started() const;
    bool ready() const;

    std::optional<SemanticSourceUpdate> poll();
    std::shared_ptr<const CodebaseSnapshot> scanner_snapshot() const;
    const std::shared_ptr<const CodebaseSnapshot>& parsed_snapshot() const;
    CodebaseScanProgress progress() const;
    const std::shared_ptr<const CodeSemanticSnapshot>& semantics() const;
    const std::vector<std::string>& available_modules() const;

private:
    CodebaseScanner scanner_;
    std::filesystem::path root_;
    std::shared_ptr<const CodebaseSnapshot> parsed_snapshot_;
    std::shared_ptr<const CodeSemanticSnapshot> semantic_snapshot_;
    std::vector<std::string> available_modules_;
    std::chrono::steady_clock::time_point scan_start_{};
    bool started_ = false;
    bool ready_ = false;
};

} // namespace draxul
