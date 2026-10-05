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
    // Advancement throws, standing in for a std::filesystem exception such
    // as a Windows narrow-name conversion failure escaping the scan.
    Exception,
};

class FailingKanbanDirectoryCursor final
    : public kanban::KanbanDirectoryCursor
{
public:
    explicit FailingKanbanDirectoryCursor(
        std::unique_ptr<kanban::KanbanDirectoryCursor> inner,
        std::error_code failure, int& injected_failures,
        bool throw_failure = false)
        : inner_(std::move(inner))
        , failure_(failure)
        , injected_failures_(&injected_failures)
        , throw_failure_(throw_failure)
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
        if (throw_failure_)
        {
            throw std::filesystem::filesystem_error(
                "simulated kanban scan exception", inner_->entry().path(),
                failure_);
        }
        error = failure_;
        return false;
    }

private:
    std::unique_ptr<kanban::KanbanDirectoryCursor> inner_;
    std::error_code failure_;
    int* injected_failures_ = nullptr;
    bool throw_failure_ = false;
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
            || failure_point_ == KanbanScanFailurePoint::Construction)
        {
            return cursor;
        }
        const bool throws = failure_point_ == KanbanScanFailurePoint::Exception;
        return std::make_unique<FailingKanbanDirectoryCursor>(
            std::move(cursor),
            std::make_error_code(throws ? std::errc::illegal_byte_sequence
                                        : std::errc::io_error),
            injected_failures, throws);
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
