#include <catch2/catch_test_macros.hpp>

#include <draxul/treesitter.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace draxul
{

namespace
{

std::shared_ptr<const CodebaseSnapshot> wait_for_complete_snapshot(
    CodebaseScanner& scanner,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(2000))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (const auto snapshot = scanner.snapshot(); snapshot && snapshot->complete)
            return snapshot;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return scanner.snapshot();
}

std::vector<std::string> collect_type_names(const ParsedFile& file)
{
    std::vector<std::string> names;
    for (const auto& symbol : file.symbols)
    {
        if (symbol.kind == SymbolKind::Class || symbol.kind == SymbolKind::Struct)
            names.push_back(symbol.name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

const SymbolRecord* find_symbol(
    const ParsedFile& file, SymbolKind kind, const std::string& name)
{
    for (const auto& symbol : file.symbols)
    {
        if (symbol.kind == kind && symbol.name == name)
            return &symbol;
    }
    return nullptr;
}

} // namespace

TEST_CASE("tree-sitter snapshot excludes forward class and struct declarations", "[treesitter]")
{
    const auto temp_root
        = std::filesystem::temp_directory_path() / "draxul-treesitter-forward-decls";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);

    const auto source_path = temp_root / "sample.h";
    {
        std::ofstream out(source_path);
        REQUIRE(out.is_open());
        out << "class IHost;\n";
        out << "struct PlainData;\n";
        out << "class Concrete final {};\n";
        out << "struct PlainDataDef { int value; };\n";
    }

    CodebaseScanner scanner;
    scanner.start(temp_root);
    const auto snapshot = wait_for_complete_snapshot(scanner);
    scanner.stop();

    REQUIRE(snapshot);
    REQUIRE(snapshot->complete);
    REQUIRE(snapshot->files.size() == 1);
    REQUIRE(snapshot->files[0].path == "sample.h");

    const std::vector<std::string> type_names = collect_type_names(snapshot->files[0]);
    CHECK(type_names == std::vector<std::string>{ "Concrete", "PlainDataDef" });

    std::filesystem::remove_all(temp_root);
}

TEST_CASE("tree-sitter snapshot captures direct fields with type references", "[treesitter]")
{
    const auto temp_root
        = std::filesystem::temp_directory_path() / "draxul-treesitter-fields";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);

    const auto source_path = temp_root / "fields.h";
    {
        std::ofstream out(source_path);
        REQUIRE(out.is_open());
        out << "class Foo {};\n";
        out << "class Bar {};\n";
        out << "struct Link {\n";
        out << "    Foo foo_;\n";
        out << "    Bar* bar_;\n";
        out << "    int count_;\n";
        out << "};\n";
    }

    CodebaseScanner scanner;
    scanner.start(temp_root);
    const auto snapshot = wait_for_complete_snapshot(scanner);
    scanner.stop();

    REQUIRE(snapshot);
    REQUIRE(snapshot->complete);
    REQUIRE(snapshot->files.size() == 1);
    const SymbolRecord* link = find_symbol(snapshot->files[0], SymbolKind::Struct, "Link");
    REQUIRE(link != nullptr);
    REQUIRE(link->field_count == 3);
    REQUIRE(link->fields.size() == 3);

    CHECK(link->fields[0].name == "foo_");
    CHECK(link->fields[0].type_name == "Foo");
    CHECK(link->fields[0].referenced_types == std::vector<std::string>{ "Foo" });

    CHECK(link->fields[1].name == "bar_");
    CHECK(link->fields[1].type_name == "Bar");
    CHECK(link->fields[1].referenced_types == std::vector<std::string>{ "Bar" });

    CHECK(link->fields[2].name == "count_");
    CHECK(link->fields[2].type_name == "int");
    CHECK(link->fields[2].referenced_types.empty());

    std::filesystem::remove_all(temp_root);
}

} // namespace draxul
