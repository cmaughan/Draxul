#include <catch2/catch_test_macros.hpp>

#ifdef DRAXUL_ENABLE_MEGACITY

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

TEST_CASE("tree-sitter scanner restart does not expose stale snapshots", "[treesitter]")
{
    const auto temp_root
        = std::filesystem::temp_directory_path() / "draxul-treesitter-restart";
    const auto first_root = temp_root / "first";
    const auto second_root = temp_root / "second";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(first_root);
    std::filesystem::create_directories(second_root);

    {
        std::ofstream out(first_root / "old.cpp");
        REQUIRE(out.is_open());
        out << "int old_function() { return 1; }\n";
    }
    {
        std::ofstream out(second_root / "new.cpp");
        REQUIRE(out.is_open());
        out << "int new_function() { return 2; }\n";
    }

    CodebaseScanner scanner;
    scanner.start(first_root);
    const auto first_snapshot = wait_for_complete_snapshot(scanner);
    scanner.stop();

    REQUIRE(first_snapshot);
    REQUIRE(first_snapshot->complete);
    REQUIRE(first_snapshot->files.size() == 1);
    REQUIRE(first_snapshot->files[0].path == "old.cpp");

    scanner.start(second_root);
    if (const auto after_restart = scanner.snapshot())
    {
        CHECK(std::none_of(after_restart->files.begin(), after_restart->files.end(), [](const ParsedFile& file) {
            return file.path == "old.cpp";
        }));
    }
    const auto second_snapshot = wait_for_complete_snapshot(scanner);
    scanner.stop();

    REQUIRE(second_snapshot);
    REQUIRE(second_snapshot->complete);
    REQUIRE(second_snapshot->files.size() == 1);
    CHECK(second_snapshot->files[0].path == "new.cpp");

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

TEST_CASE("tree-sitter snapshot does not flatten nested type fields into the parent type", "[treesitter]")
{
    const auto temp_root
        = std::filesystem::temp_directory_path() / "draxul-treesitter-nested-fields";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);

    const auto source_path = temp_root / "nested_fields.h";
    {
        std::ofstream out(source_path);
        REQUIRE(out.is_open());
        out << "class Foo {};\n";
        out << "class Bar {};\n";
        out << "class Outer {\n";
        out << "public:\n";
        out << "    struct Deps {\n";
        out << "        Foo* foo = nullptr;\n";
        out << "        Bar* bar = nullptr;\n";
        out << "    };\n";
        out << "private:\n";
        out << "    Deps deps_;\n";
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

    const SymbolRecord* outer = find_symbol(snapshot->files[0], SymbolKind::Class, "Outer");
    REQUIRE(outer != nullptr);
    REQUIRE(outer->field_count == 2);
    REQUIRE(outer->fields.size() == 2);

    CHECK(outer->fields[0].name == "deps_");
    CHECK(outer->fields[0].type_name == "Deps");
    CHECK(outer->fields[0].referenced_types == std::vector<std::string>{ "Deps" });

    CHECK(outer->fields[1].name == "count_");
    CHECK(outer->fields[1].type_name == "int");
    CHECK(outer->fields[1].referenced_types.empty());

    const SymbolRecord* deps = find_symbol(snapshot->files[0], SymbolKind::Struct, "Deps");
    REQUIRE(deps != nullptr);
    REQUIRE(deps->field_count == 2);
    REQUIRE(deps->fields.size() == 2);
    CHECK(deps->fields[0].name == "foo");
    CHECK(deps->fields[1].name == "bar");

    std::filesystem::remove_all(temp_root);
}

TEST_CASE("tree-sitter snapshot captures direct inherited base types", "[treesitter]")
{
    const auto temp_root
        = std::filesystem::temp_directory_path() / "draxul-treesitter-inheritance";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);

    const auto source_path = temp_root / "inheritance.h";
    {
        std::ofstream out(source_path);
        REQUIRE(out.is_open());
        out << "class IRenderer {};\n";
        out << "struct DeviceBase {};\n";
        out << "class VkRenderDevice : public IRenderer, public DeviceBase {};\n";
    }

    CodebaseScanner scanner;
    scanner.start(temp_root);
    const auto snapshot = wait_for_complete_snapshot(scanner);
    scanner.stop();

    REQUIRE(snapshot);
    REQUIRE(snapshot->complete);
    REQUIRE(snapshot->files.size() == 1);

    const SymbolRecord* vk = find_symbol(snapshot->files[0], SymbolKind::Class, "VkRenderDevice");
    REQUIRE(vk != nullptr);
    CHECK(vk->inherited_types == std::vector<std::string>{ "DeviceBase", "IRenderer" });

    std::filesystem::remove_all(temp_root);
}

TEST_CASE("tree-sitter captures inline methods defined inside class body", "[treesitter]")
{
    const auto temp_root
        = std::filesystem::temp_directory_path() / "draxul-treesitter-inline-methods";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root);

    const auto source_path = temp_root / "inline_methods.h";
    {
        std::ofstream out(source_path);
        REQUIRE(out.is_open());
        out << "class Foo {\n";
        out << "public:\n";
        out << "    void set(int id) { id_ = id; }\n";
        out << "    int get() const { return id_; }\n";
        out << "private:\n";
        out << "    int id_;\n";
        out << "};\n";
    }

    CodebaseScanner scanner;
    scanner.start(temp_root);
    const auto snapshot = wait_for_complete_snapshot(scanner);
    scanner.stop();

    REQUIRE(snapshot);
    REQUIRE(snapshot->complete);
    REQUIRE(snapshot->files.size() == 1);

    // The class itself should be found.
    const SymbolRecord* foo = find_symbol(snapshot->files[0], SymbolKind::Class, "Foo");
    REQUIRE(foo != nullptr);

    // The inline methods should be captured as Function symbols with parent "Foo".
    std::vector<std::string> method_names;
    for (const auto& sym : snapshot->files[0].symbols)
    {
        if (sym.kind == SymbolKind::Function && sym.parent == "Foo")
            method_names.push_back(sym.name);
    }
    std::sort(method_names.begin(), method_names.end());
    CHECK(method_names == std::vector<std::string>{ "get", "set" });

    std::filesystem::remove_all(temp_root);
}

TEST_CASE("tree-sitter snapshot skips generated and dependency directories by default", "[treesitter]")
{
    const auto temp_root
        = std::filesystem::temp_directory_path() / "draxul-treesitter-skip-vcpkg";
    std::filesystem::remove_all(temp_root);
    std::filesystem::create_directories(temp_root / "src");
    std::filesystem::create_directories(temp_root / "build" / "generated");
    std::filesystem::create_directories(temp_root / "_deps" / "generated");
    std::filesystem::create_directories(temp_root / "CMakeFiles" / "generated");
    std::filesystem::create_directories(temp_root / "vcpkg");
    std::filesystem::create_directories(temp_root / "third_party" / "vcpkg");
    std::filesystem::create_directories(temp_root / ".cache");
    const auto skipped_relative_link_parent = temp_root / ".cache" / "vcpkg" / "buildtrees"
        / "sdl2" / "src" / "sdl.clean" / "android-project-ant";
    std::filesystem::create_directories(skipped_relative_link_parent);
    const auto skipped_file_style_relative_link_parent = temp_root / ".cache" / "vcpkg" / "buildtrees"
        / "sdl2" / "src" / "sdl.clean" / "android-project-ant-file-style";
    std::filesystem::create_directories(skipped_file_style_relative_link_parent);

    {
        std::ofstream out(temp_root / "src" / "kept.h");
        REQUIRE(out.is_open());
        out << "class Kept {};\n";
    }
    {
        std::ofstream out(temp_root / "vcpkg" / "ignored.h");
        REQUIRE(out.is_open());
        out << "class IgnoredTopLevel {};\n";
    }
    {
        std::ofstream out(temp_root / "third_party" / "vcpkg" / "ignored_nested.h");
        REQUIRE(out.is_open());
        out << "class IgnoredNested {};\n";
    }
    {
        std::ofstream out(temp_root / "build" / "generated" / "ignored_build.h");
        REQUIRE(out.is_open());
        out << "class IgnoredBuild {};\n";
    }
    {
        std::ofstream out(temp_root / "_deps" / "generated" / "ignored_deps.h");
        REQUIRE(out.is_open());
        out << "class IgnoredDeps {};\n";
    }
    {
        std::ofstream out(temp_root / "CMakeFiles" / "generated" / "ignored_cmake.h");
        REQUIRE(out.is_open());
        out << "class IgnoredCMakeFiles {};\n";
    }

    std::error_code file_symlink_ec;
    std::filesystem::create_symlink(
        temp_root / "missing_target.h",
        temp_root / "dangling_file.h",
        file_symlink_ec);

    std::error_code dir_symlink_ec;
    std::filesystem::create_directory_symlink(
        temp_root / "missing_dir",
        temp_root / "dangling_dir.h",
        dir_symlink_ec);

    std::error_code skipped_symlink_ec;
    std::filesystem::create_symlink(
        temp_root / ".cache" / "missing_target.h",
        temp_root / ".cache" / "dangling_source.h",
        skipped_symlink_ec);

    std::error_code skipped_relative_dir_symlink_ec;
    std::filesystem::create_directory_symlink(
        std::filesystem::path("..") / "android-project" / "app" / "src" / "main" / "java",
        skipped_relative_link_parent / "src",
        skipped_relative_dir_symlink_ec);

    std::error_code skipped_file_style_relative_symlink_ec;
    std::filesystem::create_symlink(
        std::filesystem::path("..") / "android-project" / "app" / "src" / "main" / "java",
        skipped_file_style_relative_link_parent / "src",
        skipped_file_style_relative_symlink_ec);

    const bool symlink_creation_unavailable = file_symlink_ec && dir_symlink_ec
        && skipped_symlink_ec && skipped_relative_dir_symlink_ec && skipped_file_style_relative_symlink_ec;
    if (symlink_creation_unavailable)
        INFO("Dangling symlink coverage skipped because this platform did not allow symlink creation.");

    CodebaseScanner scanner;
    scanner.start(temp_root);
    const auto snapshot = wait_for_complete_snapshot(scanner);
    scanner.stop();

    REQUIRE(snapshot);
    REQUIRE(snapshot->complete);
    REQUIRE(snapshot->files.size() == 1);
    CHECK(snapshot->files[0].path == "src/kept.h");

    const std::vector<std::string> type_names = collect_type_names(snapshot->files[0]);
    CHECK(type_names == std::vector<std::string>{ "Kept" });

    std::filesystem::remove_all(temp_root);
}

} // namespace draxul

#endif // DRAXUL_ENABLE_MEGACITY
