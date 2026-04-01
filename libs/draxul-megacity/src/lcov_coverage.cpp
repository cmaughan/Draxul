#include "lcov_coverage.h"

#include <draxul/log.h>

#include <fstream>
#include <memory>
#include <sstream>

#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#endif

namespace draxul
{

namespace
{

std::string file_function_key(std::string_view file, std::string_view function)
{
    std::string key;
    key.reserve(file.size() + function.size() + 1);
    key.append(file);
    key.push_back('\n');
    key.append(function);
    return key;
}

/// Attempt to demangle a C++ mangled name. Returns the original string if demangling fails.
std::string try_demangle(const std::string& name)
{
#if defined(__GNUC__) || defined(__clang__)
    if (name.empty() || !name.starts_with("_Z"))
        return name;
    int status = 0;
    std::unique_ptr<char, decltype(&std::free)> demangled(
        abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status), std::free);
    if (status == 0 && demangled)
        return std::string(demangled.get());
#endif
    return name;
}

/// Extract the unqualified function name from a possibly mangled/qualified LCOV function name.
/// LCOV FN lines often contain demangled C++ names like "draxul::Foo::bar(int)".
/// We strip the parameter list and return the last component after "::".
std::string extract_short_function_name(std::string_view fn)
{
    // Strip parameters: everything from the first '(' onwards
    const size_t paren = fn.find('(');
    if (paren != std::string_view::npos)
        fn = fn.substr(0, paren);

    // Strip trailing whitespace
    while (!fn.empty() && fn.back() == ' ')
        fn.remove_suffix(1);

    // Take last component after "::"
    const size_t last_scope = fn.rfind("::");
    if (last_scope != std::string_view::npos)
        fn = fn.substr(last_scope + 2);

    return std::string(fn);
}

/// Make a path relative to repo_root. Returns the path unchanged if it doesn't start with repo_root.
std::string make_relative(const std::filesystem::path& absolute, const std::filesystem::path& repo_root)
{
    std::error_code ec;
    const auto rel = std::filesystem::relative(absolute, repo_root, ec);
    if (ec || rel.empty() || rel.string().starts_with(".."))
        return absolute.generic_string();
    return rel.generic_string();
}

} // namespace

LcovCoverageReport parse_lcov(std::string_view content)
{
    LcovCoverageReport report;
    LcovFileEntry* current_file = nullptr;

    size_t pos = 0;
    while (pos < content.size())
    {
        const size_t line_end = content.find('\n', pos);
        const std::string_view line = content.substr(pos, line_end == std::string_view::npos ? std::string_view::npos : line_end - pos);
        pos = line_end == std::string_view::npos ? content.size() : line_end + 1;

        // Strip trailing CR
        const std::string_view trimmed = (!line.empty() && line.back() == '\r')
            ? line.substr(0, line.size() - 1)
            : line;

        if (trimmed.starts_with("SF:"))
        {
            report.files.push_back({});
            current_file = &report.files.back();
            current_file->source_file = trimmed.substr(3);
        }
        else if (trimmed.starts_with("FN:") && current_file)
        {
            // FN:line_number,function_name
            const size_t comma = trimmed.find(',', 3);
            if (comma != std::string_view::npos)
            {
                const std::string_view fn_name = trimmed.substr(comma + 1);
                current_file->functions.push_back({ std::string(fn_name), 0 });
            }
        }
        else if (trimmed.starts_with("FNDA:") && current_file)
        {
            // FNDA:hit_count,function_name
            const size_t comma = trimmed.find(',', 5);
            if (comma != std::string_view::npos)
            {
                const uint64_t hits = std::strtoull(std::string(trimmed.substr(5, comma - 5)).c_str(), nullptr, 10);
                const std::string_view fn_name = trimmed.substr(comma + 1);

                // Find matching FN entry and update hit count
                for (auto& fn : current_file->functions)
                {
                    if (fn.function_name == fn_name)
                    {
                        fn.hit_count = hits;
                        break;
                    }
                }
            }
        }
        else if (trimmed == "end_of_record")
        {
            current_file = nullptr;
        }
    }

    // Compute totals
    for (const auto& file : report.files)
    {
        for (const auto& fn : file.functions)
        {
            ++report.total_functions;
            if (fn.hit_count > 0)
                ++report.covered_functions;
        }
    }

    return report;
}

LcovCoverageReport load_lcov_file(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
    {
        DRAXUL_LOG_DEBUG(LogCategory::App, "LCOV file not found: %s", path.string().c_str());
        return {};
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
    {
        DRAXUL_LOG_WARN(LogCategory::App, "Failed to open LCOV file: %s", path.string().c_str());
        return {};
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    return parse_lcov(ss.str());
}

LcovFunctionLookup build_lcov_lookup(
    const LcovCoverageReport& report,
    const std::filesystem::path& repo_root)
{
    LcovFunctionLookup lookup;
    lookup.total_report_functions = report.total_functions;
    lookup.covered_report_functions = report.covered_functions;

    for (const auto& file : report.files)
    {
        const std::string relative_file = make_relative(file.source_file, repo_root);

        for (const auto& fn : file.functions)
        {
            const LcovFunctionLookup::Entry entry{ fn.hit_count };

            // Demangle if the LCOV name is a mangled C++ symbol
            const std::string demangled = try_demangle(fn.function_name);

            // Exact: relative_file + demangled function name
            lookup.by_file_function[file_function_key(relative_file, demangled)] = entry;

            // Short name variant: relative_file + unqualified function name
            const std::string short_name = extract_short_function_name(demangled);
            if (short_name != demangled)
                lookup.by_file_function[file_function_key(relative_file, short_name)] = entry;

            // Function-only fallback (short name)
            auto& fallback = lookup.by_function_only[short_name];
            fallback.hit_count = std::max(fallback.hit_count, fn.hit_count);
        }
    }

    return lookup;
}

bool lcov_function_covered(
    const LcovFunctionLookup& lookup,
    std::string_view relative_source_file,
    std::string_view /*owner_qualified_name*/,
    std::string_view function_name)
{
    // Try exact file + function match
    const auto exact_it = lookup.by_file_function.find(
        file_function_key(relative_source_file, function_name));
    if (exact_it != lookup.by_file_function.end())
        return exact_it->second.hit_count > 0;

    // Fallback: function name only
    const auto fn_it = lookup.by_function_only.find(std::string(function_name));
    if (fn_it != lookup.by_function_only.end())
        return fn_it->second.hit_count > 0;

    return false;
}

} // namespace draxul
