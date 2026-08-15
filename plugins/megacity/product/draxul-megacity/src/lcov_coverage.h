#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace draxul
{

struct LcovFunctionEntry
{
    std::string function_name;
    uint64_t hit_count = 0;
};

struct LcovFileEntry
{
    std::string source_file;
    std::vector<LcovFunctionEntry> functions;
};

struct LcovCoverageReport
{
    std::vector<LcovFileEntry> files;
    uint32_t total_functions = 0;
    uint32_t covered_functions = 0;
};

/// Parse an LCOV tracefile from a string. Extracts SF, FN, FNDA, and end_of_record.
/// Only function-level coverage is extracted; line data is ignored.
[[nodiscard]] LcovCoverageReport parse_lcov(std::string_view content);

/// Load and parse an LCOV tracefile from disk.
/// Returns an empty report if the file cannot be read.
[[nodiscard]] LcovCoverageReport load_lcov_file(const std::filesystem::path& path);

/// Build a lookup from (relative_source_file, function_name) -> hit_count.
/// The repo_root is stripped from absolute source paths in the LCOV report
/// to produce relative paths matching the semantic model.
struct LcovFunctionLookup
{
    struct Entry
    {
        uint64_t hit_count = 0;
    };

    /// Key: "relative_file\nfunction_name"
    std::unordered_map<std::string, Entry> by_file_function;
    /// Key: "function_name" (fallback for demangled or unqualified names)
    std::unordered_map<std::string, Entry> by_function_only;

    uint32_t total_report_functions = 0;
    uint32_t covered_report_functions = 0;
};

[[nodiscard]] LcovFunctionLookup build_lcov_lookup(
    const LcovCoverageReport& report,
    const std::filesystem::path& repo_root);

/// Check if a function is covered by the LCOV report.
/// Tries file+function exact match first, then function-only fallback.
[[nodiscard]] bool lcov_function_covered(
    const LcovFunctionLookup& lookup,
    std::string_view relative_source_file,
    std::string_view owner_qualified_name,
    std::string_view function_name);

} // namespace draxul
