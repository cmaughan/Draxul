#include <draxul/perf_timing.h>
#include <draxul/treesitter.h>

#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tree_sitter/api.h>
#include <unordered_set>

// Grammar entry point from tree-sitter-cpp
extern "C" {
const TSLanguage* tree_sitter_cpp(void);
}

namespace draxul
{

namespace
{

// clang-format off
static constexpr std::string_view kCppQuery = R"(
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

// Captures the name of any class/struct that contains at least one pure-virtual
// member declaration (field_declaration with a pure_specifier, i.e. `= 0`).
// Compiled as a separate optional query so a grammar version mismatch here
// does not break the main symbol extraction.
// Pure virtual member declarations parse as field_declaration (not function_definition).
// The "= 0" suffix becomes a number_literal child alongside the function_declarator.
// We match field_declaration nodes that have both a function_declarator child
// (ruling out plain data members) and a number_literal child (the = 0).
static constexpr std::string_view kAbstractQuery = R"(
(class_specifier
  name: (type_identifier) @acls
  body: (field_declaration_list
    (field_declaration
      (function_declarator)
      (number_literal))))

(struct_specifier
  name: (type_identifier) @acls
  body: (field_declaration_list
    (field_declaration
      (function_declarator)
      (number_literal))))
)";
// clang-format on

// Directories to skip anywhere in the path
bool should_skip_path(const std::filesystem::path& rel)
{
    PERF_MEASURE();
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
    // .mm (Objective-C++) is parsed with the C++ grammar. ObjC syntax produces
    // parse errors but tree-sitter is error-tolerant, so C++ symbols (classes,
    // functions, structs) are still extracted correctly.
    return ext == ".cpp" || ext == ".h" || ext == ".cc" || ext == ".c"
        || ext == ".inl" || ext == ".hpp" || ext == ".mm";
}

// Strip surrounding quotes or angle brackets from an include path node's text.
std::string strip_include_delimiters(std::string s)
{
    PERF_MEASURE();
    if (s.size() >= 2
        && ((s.front() == '"' && s.back() == '"')
            || (s.front() == '<' && s.back() == '>')))
    {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

// Sniff the first chunk of a file for Objective-C keywords that are illegal
// in standard C++. If found, the file should be skipped — the C++ grammar
// cannot extract meaningful symbols from it and would produce spurious errors.
bool is_objc_source(const std::string& source)
{
    PERF_MEASURE();
    // Only look at the first 8 KB to keep it fast.
    const std::string_view head(source.data(), std::min(source.size(), size_t{ 8192 }));
    for (const char* kw : { "@interface", "@implementation", "@protocol", "@property", "@end" })
    {
        if (head.find(kw) != std::string_view::npos)
            return true;
    }
    return false;
}

std::string read_file(const std::filesystem::path& path)
{
    PERF_MEASURE();
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

constexpr uint32_t kMaxErrorsPerFile = 50;

// Walk the tree and collect positions of ERROR nodes (up to the cap).
void collect_errors(TSNode node, std::vector<ParseError>& errors)
{
    PERF_MEASURE();
    if (errors.size() >= kMaxErrorsPerFile)
        return;
    if (ts_node_is_error(node))
    {
        const TSPoint pt = ts_node_start_point(node);
        errors.push_back({ pt.row + 1, pt.column });
        return; // don't recurse further into an error node
    }
    const uint32_t n = ts_node_child_count(node);
    for (uint32_t i = 0; i < n && errors.size() < kMaxErrorsPerFile; ++i)
        collect_errors(ts_node_child(node, i), errors);
}

bool node_type_is(TSNode node, const char* type)
{
    return ts_node_is_null(node) == 0 && std::strcmp(ts_node_type(node), type) == 0;
}

TSNode find_ancestor_of_type(TSNode node, const char* type)
{
    PERF_MEASURE();
    TSNode current = node;
    while (ts_node_is_null(current) == 0)
    {
        if (node_type_is(current, type))
            return current;
        current = ts_node_parent(current);
    }
    return TSNode{};
}

TSNode find_named_child_of_type(TSNode node, const char* type)
{
    PERF_MEASURE();
    const uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
    {
        const TSNode child = ts_node_named_child(node, i);
        if (node_type_is(child, type))
            return child;
    }
    return TSNode{};
}

std::string node_text(const std::string& source, TSNode node)
{
    PERF_MEASURE();
    const uint32_t start_byte = ts_node_start_byte(node);
    const uint32_t end_byte = ts_node_end_byte(node);
    if (start_byte >= end_byte || end_byte > static_cast<uint32_t>(source.size()))
        return {};
    return source.substr(start_byte, end_byte - start_byte);
}

std::string class_name_from_node(const std::string& source, TSNode class_node)
{
    const TSNode name_node = find_named_child_of_type(class_node, "type_identifier");
    return ts_node_is_null(name_node) ? std::string{} : node_text(source, name_node);
}

bool subtree_contains_type(TSNode node, const char* type)
{
    PERF_MEASURE();
    if (node_type_is(node, type))
        return true;
    const uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
    {
        if (subtree_contains_type(ts_node_child(node, i), type))
            return true;
    }
    return false;
}

uint32_t count_data_fields(TSNode class_node)
{
    PERF_MEASURE();
    const TSNode body = find_named_child_of_type(class_node, "field_declaration_list");
    if (ts_node_is_null(body))
        return 0;

    uint32_t count = 0;
    const uint32_t child_count = ts_node_named_child_count(body);
    for (uint32_t i = 0; i < child_count; ++i)
    {
        const TSNode child = ts_node_named_child(body, i);
        if (!node_type_is(child, "field_declaration"))
            continue;
        if (subtree_contains_type(child, "function_declarator"))
            continue;
        ++count;
    }
    return count;
}

bool is_type_like_node(TSNode node)
{
    const char* type = ts_node_type(node);
    return std::strcmp(type, "type_identifier") == 0
        || std::strcmp(type, "template_type") == 0
        || std::strcmp(type, "qualified_identifier") == 0
        || std::strcmp(type, "sized_type_specifier") == 0
        || std::strcmp(type, "primitive_type") == 0;
}

std::string field_display_type_from_declaration(TSNode field_node, const std::string& source)
{
    PERF_MEASURE();
    const uint32_t child_count = ts_node_named_child_count(field_node);
    for (uint32_t i = 0; i < child_count; ++i)
    {
        const TSNode child = ts_node_named_child(field_node, i);
        if (is_type_like_node(child))
            return node_text(source, child);
    }
    return {};
}

void collect_type_references(TSNode node, const std::string& source, std::set<std::string>& out);
bool has_type_definition_body(TSNode type_node);

bool is_nested_type_definition_node(TSNode node)
{
    return ((node_type_is(node, "class_specifier") || node_type_is(node, "struct_specifier"))
        && has_type_definition_body(node));
}

void collect_declared_field_names(TSNode node, const std::string& source, std::vector<std::string>& out)
{
    PERF_MEASURE();
    if (node_type_is(node, "function_declarator"))
        return;
    if (is_nested_type_definition_node(node))
        return;
    if (node_type_is(node, "field_identifier") || node_type_is(node, "identifier"))
    {
        const std::string name = node_text(source, node);
        if (!name.empty())
            out.push_back(name);
        return;
    }

    const uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
        collect_declared_field_names(ts_node_named_child(node, i), source, out);
}

std::vector<SymbolRecord::FieldRecord> collect_data_field_records(TSNode class_node, const std::string& source)
{
    PERF_MEASURE();
    std::vector<SymbolRecord::FieldRecord> fields;
    const TSNode body = find_named_child_of_type(class_node, "field_declaration_list");
    if (ts_node_is_null(body))
        return fields;

    const uint32_t child_count = ts_node_named_child_count(body);
    for (uint32_t i = 0; i < child_count; ++i)
    {
        const TSNode child = ts_node_named_child(body, i);
        if (!node_type_is(child, "field_declaration"))
            continue;
        if (subtree_contains_type(child, "function_declarator"))
            continue;

        std::vector<std::string> field_names;
        const uint32_t field_child_count = ts_node_named_child_count(child);
        for (uint32_t field_child = 0; field_child < field_child_count; ++field_child)
            collect_declared_field_names(ts_node_named_child(child, field_child), source, field_names);
        if (field_names.empty())
            continue;

        std::set<std::string> refs;
        collect_type_references(child, source, refs);
        const std::vector<std::string> referenced_types(refs.begin(), refs.end());
        const std::string type_name = field_display_type_from_declaration(child, source);

        for (const auto& field_name : field_names)
            fields.push_back({ field_name, type_name, referenced_types });
    }

    return fields;
}

std::vector<std::string> collect_direct_base_type_names(
    TSNode type_node, const std::string& source, std::string_view self_name)
{
    PERF_MEASURE();
    std::set<std::string> refs;
    const uint32_t child_count = ts_node_named_child_count(type_node);
    for (uint32_t i = 0; i < child_count; ++i)
    {
        const TSNode child = ts_node_named_child(type_node, i);
        if (node_type_is(child, "field_declaration_list"))
            continue;
        collect_type_references(child, source, refs);
    }
    refs.erase(std::string(self_name));
    return { refs.begin(), refs.end() };
}

bool has_type_definition_body(TSNode type_node)
{
    return ts_node_is_null(find_named_child_of_type(type_node, "field_declaration_list")) == 0;
}

void collect_type_references(TSNode node, const std::string& source, std::set<std::string>& out)
{
    PERF_MEASURE();
    if (is_nested_type_definition_node(node))
        return;

    const char* type = ts_node_type(node);
    if (std::strcmp(type, "type_identifier") == 0
        || std::strcmp(type, "sized_type_specifier") == 0
        || std::strcmp(type, "template_type") == 0)
    {
        const std::string text = node_text(source, node);
        if (!text.empty())
            out.insert(text);
    }

    const uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i)
        collect_type_references(ts_node_child(node, i), source, out);
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
    PERF_MEASURE();
    stop_flag_.store(false);
    thread_ = std::thread(&CodebaseScanner::scan_thread, this, std::move(root));
}

void CodebaseScanner::stop()
{
    PERF_MEASURE();
    stop_flag_.store(true);
    if (thread_.joinable())
        thread_.join();
}

std::shared_ptr<const CodebaseSnapshot> CodebaseScanner::snapshot() const
{
    PERF_MEASURE();
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void CodebaseScanner::publish(std::shared_ptr<CodebaseSnapshot> snap)
{
    PERF_MEASURE();
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = std::move(snap);
}

void CodebaseScanner::scan_thread(std::filesystem::path root)
{
    PERF_MEASURE();
    const TSLanguage* lang = tree_sitter_cpp();

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, lang);

    uint32_t error_offset = 0;
    TSQueryError error_type = TSQueryErrorNone;
    TSQuery* query = ts_query_new(
        lang, kCppQuery.data(), static_cast<uint32_t>(kCppQuery.size()),
        &error_offset, &error_type);
    // query may be null if the grammar version is incompatible; we continue
    // without symbol extraction but still count errors

    TSQueryCursor* cursor = query ? ts_query_cursor_new() : nullptr;

    // Abstract-class detection is best-effort: if the grammar version doesn't
    // support pure_specifier the query won't compile and we fall back gracefully.
    TSQuery* abstract_query = ts_query_new(
        lang, kAbstractQuery.data(), static_cast<uint32_t>(kAbstractQuery.size()),
        &error_offset, &error_type);
    TSQueryCursor* abstract_cursor = abstract_query ? ts_query_cursor_new() : nullptr;

    auto snapshot = std::make_shared<CodebaseSnapshot>();
    snapshot->scan_time = std::chrono::steady_clock::now();

    std::error_code ec;
    auto dir_iter = std::filesystem::recursive_directory_iterator(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec)
    {
        ts_parser_delete(parser);
        if (abstract_cursor)
            ts_query_cursor_delete(abstract_cursor);
        if (abstract_query)
            ts_query_delete(abstract_query);
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
        if (ec || stop_flag_.load())
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

        // Collect ERROR node positions for display in the UI.
        if (ts_node_has_error(root_node))
            collect_errors(root_node, file.errors);

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
                    std::string parent_name;
                    uint32_t end_line = line;
                    uint32_t field_count = 0;
                    std::vector<std::string> referenced_types;
                    std::vector<SymbolRecord::FieldRecord> fields;
                    std::vector<std::string> inherited_types;
                    if (name_len == 2 && strncmp(cap_name, "fn", 2) == 0)
                    {
                        kind = SymbolKind::Function;
                        const TSNode function_node = find_ancestor_of_type(cap.node, "function_definition");
                        if (ts_node_is_null(function_node) == 0)
                            end_line = ts_node_end_point(function_node).row + 1;

                        const TSNode enclosing_class = find_ancestor_of_type(cap.node, "class_specifier");
                        const TSNode enclosing_struct = find_ancestor_of_type(cap.node, "struct_specifier");
                        const TSNode owner_node = ts_node_is_null(enclosing_class) == 0 ? enclosing_class : enclosing_struct;
                        if (ts_node_is_null(owner_node) == 0)
                            parent_name = class_name_from_node(source, owner_node);

                        // qualified_identifier nodes represent out-of-class method
                        // definitions (e.g. "Foo::bar"). Split off the class prefix
                        // so free functions and methods can be distinguished in the UI.
                        if (parent_name.empty() && strcmp(ts_node_type(cap.node), "qualified_identifier") == 0)
                        {
                            const size_t sep = sym_name.rfind("::");
                            if (sep != std::string::npos)
                            {
                                parent_name = sym_name.substr(0, sep);
                                sym_name = sym_name.substr(sep + 2);
                            }
                        }
                    }
                    else if (name_len == 3 && strncmp(cap_name, "cls", 3) == 0)
                    {
                        kind = SymbolKind::Class;
                        const TSNode class_node = find_ancestor_of_type(cap.node, "class_specifier");
                        if (ts_node_is_null(class_node) == 0)
                        {
                            if (!has_type_definition_body(class_node))
                                continue;
                            end_line = ts_node_end_point(class_node).row + 1;
                            fields = collect_data_field_records(class_node, source);
                            field_count = static_cast<uint32_t>(fields.size());
                            std::set<std::string> refs;
                            collect_type_references(class_node, source, refs);
                            refs.erase(sym_name);
                            referenced_types.assign(refs.begin(), refs.end());
                            inherited_types = collect_direct_base_type_names(class_node, source, sym_name);
                        }
                    }
                    else if (name_len == 2 && strncmp(cap_name, "st", 2) == 0)
                    {
                        kind = SymbolKind::Struct;
                        const TSNode struct_node = find_ancestor_of_type(cap.node, "struct_specifier");
                        if (ts_node_is_null(struct_node) == 0)
                        {
                            if (!has_type_definition_body(struct_node))
                                continue;
                            end_line = ts_node_end_point(struct_node).row + 1;
                            fields = collect_data_field_records(struct_node, source);
                            field_count = static_cast<uint32_t>(fields.size());
                            std::set<std::string> refs;
                            collect_type_references(struct_node, source, refs);
                            refs.erase(sym_name);
                            referenced_types.assign(refs.begin(), refs.end());
                            inherited_types = collect_direct_base_type_names(struct_node, source, sym_name);
                        }
                    }
                    else if (name_len == 3 && strncmp(cap_name, "inc", 3) == 0)
                    {
                        kind = SymbolKind::Include;
                        sym_name = strip_include_delimiters(std::move(sym_name));
                    }
                    else
                        continue;

                    SymbolRecord record;
                    record.kind = kind;
                    record.name = std::move(sym_name);
                    record.parent = std::move(parent_name);
                    record.is_abstract = false;
                    record.line = line;
                    record.end_line = end_line;
                    record.field_count = field_count;
                    record.referenced_types = std::move(referenced_types);
                    record.fields = std::move(fields);
                    record.inherited_types = std::move(inherited_types);
                    file.symbols.push_back(std::move(record));
                }
            }
        }

        // Mark abstract classes using the optional abstract query.
        if (abstract_query && abstract_cursor)
        {
            std::unordered_set<std::string> abstract_names;
            ts_query_cursor_exec(abstract_cursor, abstract_query, root_node);
            TSQueryMatch amatch;
            while (ts_query_cursor_next_match(abstract_cursor, &amatch))
            {
                for (uint32_t i = 0; i < amatch.capture_count; ++i)
                {
                    const TSQueryCapture& cap = amatch.captures[i];
                    const uint32_t s = ts_node_start_byte(cap.node);
                    const uint32_t e = ts_node_end_byte(cap.node);
                    if (s < e && e <= static_cast<uint32_t>(source.size()))
                        abstract_names.insert(source.substr(s, e - s));
                }
            }
            for (auto& sym : file.symbols)
            {
                if ((sym.kind == SymbolKind::Class || sym.kind == SymbolKind::Struct)
                    && abstract_names.count(sym.name))
                    sym.is_abstract = true;
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

    if (abstract_cursor)
        ts_query_cursor_delete(abstract_cursor);
    if (abstract_query)
        ts_query_delete(abstract_query);
    if (cursor)
        ts_query_cursor_delete(cursor);
    if (query)
        ts_query_delete(query);
    ts_parser_delete(parser);
}

} // namespace draxul
