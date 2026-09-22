#pragma once

#include "../../modules/kanban/draxul-kanban/src/kanban_directory_scan.h"

#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

namespace draxul::tests
{

enum class KanbanScanFailurePoint
{
    Construction,
    Advancement,
};

class FailingKanbanDirectoryCursor final
    : public kanban::KanbanDirectoryCursor
{
public:
    explicit FailingKanbanDirectoryCursor(
        std::unique_ptr<kanban::KanbanDirectoryCursor> inner,
        std::error_code failure, int& injected_failures)
        : inner_(std::move(inner))
        , failure_(failure)
        , injected_failures_(&injected_failures)
    {
    }

    bool at_end() const noexcept override
    {
        return inner_->at_end();
    }

    const std::filesystem::directory_entry& entry() const override
    {
        return inner_->entry();
    }

    bool increment(std::error_code& error) override
    {
        ++*injected_failures_;
        error = failure_;
        return false;
    }

private:
    std::unique_ptr<kanban::KanbanDirectoryCursor> inner_;
    std::error_code failure_;
    int* injected_failures_ = nullptr;
};

class FaultInjectingKanbanDirectoryOperations final
    : public kanban::KanbanDirectoryOperations
{
public:
    FaultInjectingKanbanDirectoryOperations(
        std::filesystem::path failure_directory,
        KanbanScanFailurePoint failure_point)
        : failure_directory_(canonicalize_for_match(
              std::move(failure_directory)))
        , failure_point_(failure_point)
        , native_(kanban::native_kanban_directory_operations())
    {
    }

    std::unique_ptr<kanban::KanbanDirectoryCursor> open(
        const std::filesystem::path& directory,
        std::error_code& error) override
    {
        const bool matches_failure_directory
            = canonicalize_for_match(directory) == failure_directory_;
        if (matches_failure_directory
            && failure_point_ == KanbanScanFailurePoint::Construction)
        {
            ++injected_failures;
            error = std::make_error_code(std::errc::permission_denied);
            return {};
        }

        auto cursor = native_->open(directory, error);
        if (!cursor || error || !matches_failure_directory
            || failure_point_ != KanbanScanFailurePoint::Advancement)
        {
            return cursor;
        }
        return std::make_unique<FailingKanbanDirectoryCursor>(
            std::move(cursor),
            std::make_error_code(std::errc::io_error),
            injected_failures);
    }

    int injected_failures = 0;

private:
    static std::filesystem::path canonicalize_for_match(
        const std::filesystem::path& path)
    {
        std::error_code error;
        const auto canonical = std::filesystem::weakly_canonical(path, error);
        return error ? path.lexically_normal() : canonical;
    }

    std::filesystem::path failure_directory_;
    KanbanScanFailurePoint failure_point_;
    std::shared_ptr<kanban::KanbanDirectoryOperations> native_;
};

} // namespace draxul::tests
