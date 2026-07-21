#include "semantic_source_controller.h"

#include <algorithm>

namespace draxul
{

SemanticSourceController::SemanticSourceController(std::filesystem::path root)
    : root_(std::move(root))
{
}

SemanticSourceController::~SemanticSourceController()
{
    stop();
}

void SemanticSourceController::set_root(std::filesystem::path root)
{
    root_ = std::move(root);
}

const std::filesystem::path& SemanticSourceController::root() const
{
    return root_;
}

void SemanticSourceController::start()
{
    if (started_)
        return;
    parsed_snapshot_.reset();
    semantic_snapshot_.reset();
    available_modules_.clear();
    ready_ = false;
    scanner_.start(root_);
    started_ = true;
    scan_start_ = std::chrono::steady_clock::now();
}

void SemanticSourceController::stop()
{
    if (started_)
        scanner_.stop();
    started_ = false;
    ready_ = false;
    parsed_snapshot_.reset();
    semantic_snapshot_.reset();
    available_modules_.clear();
}

bool SemanticSourceController::started() const
{
    return started_;
}

bool SemanticSourceController::ready() const
{
    return ready_;
}

std::optional<SemanticSourceUpdate> SemanticSourceController::poll()
{
    if (!started_ || ready_)
        return std::nullopt;

    const auto snapshot = scanner_.snapshot();
    if (!snapshot || !snapshot->complete)
        return std::nullopt;

    const auto scan_end = std::chrono::steady_clock::now();
    const auto semantic_start = std::chrono::steady_clock::now();
    auto semantics = std::make_shared<CodeSemanticSnapshot>(build_code_semantic_snapshot(*snapshot));

    std::vector<std::string> modules;
    for (const CodeSemanticNode& node : semantics->nodes)
    {
        if (node.kind == CodeSemanticNodeKind::Module)
            modules.push_back(node.module_path);
    }
    std::sort(modules.begin(), modules.end());
    modules.erase(std::unique(modules.begin(), modules.end()), modules.end());

    const auto semantic_end = std::chrono::steady_clock::now();
    parsed_snapshot_ = snapshot;
    semantic_snapshot_ = semantics;
    available_modules_ = modules;
    ready_ = true;

    return SemanticSourceUpdate{
        .parsed_snapshot = std::move(snapshot),
        .semantic_snapshot = std::move(semantics),
        .available_modules = std::move(modules),
        .scan_ms = std::chrono::duration<double, std::milli>(scan_end - scan_start_).count(),
        .semantic_ms = std::chrono::duration<double, std::milli>(semantic_end - semantic_start).count(),
    };
}

std::shared_ptr<const CodebaseSnapshot> SemanticSourceController::scanner_snapshot() const
{
    return started_ ? scanner_.snapshot() : nullptr;
}

const std::shared_ptr<const CodebaseSnapshot>& SemanticSourceController::parsed_snapshot() const
{
    return parsed_snapshot_;
}

CodebaseScanProgress SemanticSourceController::progress() const
{
    return started_ ? scanner_.progress() : CodebaseScanProgress{};
}

const std::shared_ptr<const CodeSemanticSnapshot>& SemanticSourceController::semantics() const
{
    return semantic_snapshot_;
}

const std::vector<std::string>& SemanticSourceController::available_modules() const
{
    return available_modules_;
}

} // namespace draxul
