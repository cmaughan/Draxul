#include <draxul/treesitter.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <tree_sitter/api.h>

// Grammar entry point from tree-sitter-cpp
extern "C" {
const TSLanguage* tree_sitter_cpp(void);
}

namespace draxul
{

namespace
{

// clang-format off
static const char* kCppQuery = R"(
(function_definition
  declarator: (function_declarator
    declarator: [
      (identifier) @fn
      (qualified_identifier) @fn
      (destructor_name) @fn
    ]))

(class_specifier
  name: (type_identifier) @cls)

(struct_specifier
  name: (type_identifier) @st)

(preproc_include
  path: _ @inc)
)";
// clang-format on

// Directories to skip anywhere in the path
bool should_skip_path(const std::filesystem::path& rel)
{
    for (const auto& part : rel)
    {
        const auto s = part.string();
        if (!s.empty() && s[0] == '.')
            return true;
        if (s == "build" || s == "_deps" || s == "CMakeFiles")
            return true;
    }
    return false;
}

bool is_cpp_source(const std::filesystem::path& path)
{
    const auto ext = path.extension().string();
    return ext == ".cpp" || ext == ".h" || ext == ".cc" || ext == ".c"
        || ext == ".mm" || ext == ".inl" || ext == ".hpp";
}

// Strip surrounding quotes or angle brackets from an include path node's text.
std::string strip_include_delimiters(std::string s)
{
    if (s.size() >= 2
        && ((s.front() == '"' && s.back() == '"')
            || (s.front() == '<' && s.back() == '>')))
    {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

// ---------------------------------------------------------------------------

CodebaseScanner::CodebaseScanner() = default;

CodebaseScanner::~CodebaseScanner()
{
    stop();
}

void CodebaseScanner::start(std::filesystem::path root)
{
    stop_flag_.store(false, std::memory_order_relaxed);
    thread_ = std::thread(&CodebaseScanner::scan_thread, this, std::move(root));
}

void CodebaseScanner::stop()
{
    stop_flag_.store(true, std::memory_order_relaxed);
    if (thread_.joinable())
        thread_.join();
}

std::shared_ptr<const CodebaseSnapshot> CodebaseScanner::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void CodebaseScanner::publish(std::shared_ptr<CodebaseSnapshot> snap)
{
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = std::move(snap);
}

void CodebaseScanner::scan_thread(std::filesystem::path root)
{
    const TSLanguage* lang = tree_sitter_cpp();

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, lang);

    uint32_t error_offset = 0;
    TSQueryError error_type = TSQueryErrorNone;
    TSQuery* query = ts_query_new(
        lang, kCppQuery, static_cast<uint32_t>(strlen(kCppQuery)),
        &error_offset, &error_type);
    // query may be null if the grammar version is incompatible; we continue
    // without symbol extraction but still count errors

    TSQueryCursor* cursor = query ? ts_query_cursor_new() : nullptr;

    auto snapshot = std::make_shared<CodebaseSnapshot>();
    snapshot->scan_time = std::chrono::steady_clock::now();

    std::error_code ec;
    auto dir_iter = std::filesystem::recursive_directory_iterator(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec)
    {
        ts_parser_delete(parser);
        if (cursor)
            ts_query_cursor_delete(cursor);
        if (query)
            ts_query_delete(query);
        return;
    }

    for (auto it = std::filesystem::begin(dir_iter),
              end = std::filesystem::end(dir_iter);
        it != end; it.increment(ec))
    {
        if (ec || stop_flag_.load(std::memory_order_relaxed))
            break;

        const auto& entry = *it;
        if (!entry.is_regular_file())
            continue;

        const auto rel = entry.path().lexically_relative(root);
        if (should_skip_path(rel))
        {
            it.disable_recursion_pending();
            continue;
        }

        if (!is_cpp_source(entry.path()))
            continue;

        const std::string source = read_file(entry.path());
        if (source.empty())
            continue;

        TSTree* tree = ts_parser_parse_string(
            parser, nullptr, source.c_str(),
            static_cast<uint32_t>(source.size()));
        if (!tree)
            continue;

        ParsedFile file;
        // Normalise to forward slashes for display
        file.path = rel.generic_string();

        const TSNode root_node = ts_tree_root_node(tree);

        // Count parse errors via root node's has-error flag
        if (ts_node_has_error(root_node))
            file.error_count = 1; // coarse: tree has at least one error

        if (query && cursor)
        {
            ts_query_cursor_exec(cursor, query, root_node);
            TSQueryMatch match;
            while (ts_query_cursor_next_match(cursor, &match))
            {
                for (uint32_t i = 0; i < match.capture_count; ++i)
                {
                    const TSQueryCapture& cap = match.captures[i];
                    uint32_t name_len = 0;
                    const char* cap_name = ts_query_capture_name_for_id(
                        query, cap.index, &name_len);

                    const uint32_t start_byte = ts_node_start_byte(cap.node);
                    const uint32_t end_byte = ts_node_end_byte(cap.node);
                    const uint32_t line = ts_node_start_point(cap.node).row + 1; // 1-based

                    if (start_byte >= end_byte
                        || end_byte > static_cast<uint32_t>(source.size()))
                        continue;

                    std::string sym_name = source.substr(start_byte, end_byte - start_byte);

                    SymbolKind kind;
                    if (name_len == 2 && strncmp(cap_name, "fn", 2) == 0)
                        kind = SymbolKind::Function;
                    else if (name_len == 3
                        && strncmp(cap_name, "cls", 3) == 0)
                        kind = SymbolKind::Class;
                    else if (name_len == 2 && strncmp(cap_name, "st", 2) == 0)
                        kind = SymbolKind::Struct;
                    else if (name_len == 3
                        && strncmp(cap_name, "inc", 3) == 0)
                    {
                        kind = SymbolKind::Include;
                        sym_name = strip_include_delimiters(std::move(sym_name));
                    }
                    else
                        continue;

                    file.symbols.push_back({ kind, std::move(sym_name), line });
                }
            }
        }

        ts_tree_delete(tree);
        snapshot->files.push_back(std::move(file));

        // Publish partial results every 20 files
        if (snapshot->files.size() % 20 == 0)
            publish(std::make_shared<CodebaseSnapshot>(*snapshot));
    }

    snapshot->complete = true;
    snapshot->scan_time = std::chrono::steady_clock::now();
    publish(std::move(snapshot));

    if (cursor)
        ts_query_cursor_delete(cursor);
    if (query)
        ts_query_delete(query);
    ts_parser_delete(parser);
}

} // namespace draxul
