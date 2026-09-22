#pragma once

#include <filesystem>
#include <memory>
#include <system_error>

namespace draxul::kanban
{

// Private filesystem seam used by the production board scan and deterministic
// failure tests. It is deliberately kept under src/ rather than exported from
// the draxul-kanban public include tree.
class KanbanDirectoryCursor
{
public:
    virtual ~KanbanDirectoryCursor() = default;

    virtual bool at_end() const noexcept = 0;
    virtual const std::filesystem::directory_entry& entry() const = 0;
    virtual bool increment(std::error_code& error) = 0;
};

class KanbanDirectoryOperations
{
public:
    virtual ~KanbanDirectoryOperations() = default;

    virtual std::unique_ptr<KanbanDirectoryCursor> open(
        const std::filesystem::path& directory,
        std::error_code& error) = 0;
};

std::shared_ptr<KanbanDirectoryOperations>
native_kanban_directory_operations();

// Thread-local and scoped so parallel test processes and production callers
// cannot retain injected behavior after a test exits.
class ScopedKanbanDirectoryOperationsOverride final
{
public:
    explicit ScopedKanbanDirectoryOperationsOverride(
        KanbanDirectoryOperations& operations);
    ~ScopedKanbanDirectoryOperationsOverride();

    ScopedKanbanDirectoryOperationsOverride(
        const ScopedKanbanDirectoryOperationsOverride&) = delete;
    ScopedKanbanDirectoryOperationsOverride& operator=(
        const ScopedKanbanDirectoryOperationsOverride&) = delete;

private:
    KanbanDirectoryOperations* previous_ = nullptr;
};

} // namespace draxul::kanban
