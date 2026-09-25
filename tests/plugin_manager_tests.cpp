#include <catch2/catch_test_macros.hpp>

#include <draxul/plugin_manager.h>
#include <draxul/plugin_host.h>
#include <draxul/events.h>
#include <draxul/base_renderer.h>
#include "plugin_storage.h"
#include "support/fake_renderer.h"
#include "support/test_host_callbacks.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{

class TempPlugins
{
public:
    TempPlugins()
    {
        root = std::filesystem::temp_directory_path()
            / ("draxul-plugin-test-"
                + std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::filesystem::create_directories(root);
    }
    ~TempPlugins()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    std::filesystem::path root;
};

void install_plugin(const std::filesystem::path& tier,
    std::string_view directory_name, std::string_view id,
    const std::filesystem::path& library,
    std::string_view manifest_library = {},
    std::string_view name = "Fixture",
    std::string_view version = "1.0.0")
{
    const auto package = tier / directory_name;
    const auto directory = package / "generations" / "build-1";
    std::filesystem::create_directories(directory);
    const std::string library_name = manifest_library.empty()
        ? library.filename().string()
        : std::string(manifest_library);
    if (!library.empty() && manifest_library.empty())
    {
        std::filesystem::copy_file(library,
            directory / library.filename(),
            std::filesystem::copy_options::overwrite_existing);
    }
    std::ofstream manifest(directory / "plugin.toml");
    manifest << "schema_version = 1\n"
             << "id = \"" << id << "\"\n"
             << "name = \"" << name << "\"\n"
             << "version = \"" << version << "\"\n"
             << "abi_version = 2\n"
#ifdef _WIN32
             << "[platform.windows]\n"
#elif defined(__APPLE__)
             << "[platform.macos]\n"
#else
             << "[platform.linux]\n"
#endif
             << "library = \"" << library_name << "\"\n";
    std::ofstream pointer(package / "current.json");
    pointer << R"({"schema_version":1,"generation":"build-1"})";
}

void install_plugin_generation(const std::filesystem::path& generations,
    std::string_view generation, std::string_view id,
    const std::filesystem::path& library,
    std::string_view manifest_library = {},
    std::string_view name = "Fixture",
    std::string_view version = "1.0.0")
{
    const auto directory = generations / generation;
    std::filesystem::create_directories(directory);
    const std::string library_name = manifest_library.empty()
        ? library.filename().string()
        : std::string(manifest_library);
    if (!library.empty() && manifest_library.empty())
    {
        std::filesystem::copy_file(library,
            directory / library.filename(),
            std::filesystem::copy_options::overwrite_existing);
    }
    std::ofstream manifest(directory / "plugin.toml");
    manifest << "schema_version = 1\n"
             << "id = \"" << id << "\"\n"
             << "name = \"" << name << "\"\n"
             << "version = \"" << version << "\"\n"
             << "abi_version = 2\n"
#ifdef _WIN32
             << "[platform.windows]\n"
#elif defined(__APPLE__)
             << "[platform.macos]\n"
#else
             << "[platform.linux]\n"
#endif
             << "library = \"" << library_name << "\"\n";
}

class FaultInjectingStorageFiles final
    : public draxul::PluginStorageFileOperations
{
public:
    enum class Failure
    {
        None,
        CreateDirectories,
        FileSize,
        Read,
        Open,
        Write,
        Flush,
        Replace,
        Remove,
    };

    class Output final : public draxul::PluginStorageOutputFile
    {
    public:
        Output(FaultInjectingStorageFiles& owner,
            std::unique_ptr<draxul::PluginStorageOutputFile> delegate)
            : owner_(owner)
            , delegate_(std::move(delegate))
        {
        }

        bool write(std::string_view value,
            std::error_code& error) override
        {
            if (owner_.failure == Failure::Write)
                return owner_.fail(error);
            return delegate_->write(value, error);
        }

        bool flush(std::error_code& error) override
        {
            if (owner_.failure == Failure::Flush)
                return owner_.fail(error);
            return delegate_->flush(error);
        }

    private:
        FaultInjectingStorageFiles& owner_;
        std::unique_ptr<draxul::PluginStorageOutputFile> delegate_;
    };

    explicit FaultInjectingStorageFiles(Failure initial_failure)
        : failure(initial_failure)
        , delegate(draxul::native_plugin_storage_file_operations())
    {
    }

    bool create_directories(const std::filesystem::path& path,
        std::error_code& error) override
    {
        if (failure == Failure::CreateDirectories)
            return fail(error);
        return delegate->create_directories(path, error);
    }

    bool file_size(const std::filesystem::path& path,
        std::uintmax_t& size, std::error_code& error) override
    {
        if (failure == Failure::FileSize)
            return fail(error);
        return delegate->file_size(path, size, error);
    }

    bool read_all(const std::filesystem::path& path, std::string& value,
        std::error_code& error) override
    {
        if (failure == Failure::Read)
            return fail(error);
        return delegate->read_all(path, value, error);
    }

    std::unique_ptr<draxul::PluginStorageOutputFile> open_output(
        const std::filesystem::path& path,
        std::error_code& error) override
    {
        last_temporary = path;
        if (inject_collision_once)
        {
            inject_collision_once = false;
            collision_temporary = path;
            std::ofstream abandoned(path, std::ios::binary);
            abandoned << "abandoned writer";
            abandoned.close();
            error = std::make_error_code(std::errc::file_exists);
            return {};
        }
        if (failure == Failure::Open)
        {
            (void)fail(error);
            return {};
        }
        auto output = delegate->open_output(path, error);
        if (!output)
            return {};
        return std::make_unique<Output>(*this, std::move(output));
    }

    bool replace(const std::filesystem::path& temporary,
        const std::filesystem::path& target,
        std::error_code& error) override
    {
        ++replace_calls;
        if (failure == Failure::Replace
            || (fail_replace_call > 0
                && replace_calls == fail_replace_call))
            return fail(error);
        const bool replaced = delegate->replace(temporary, target, error);
        if (!replaced)
            last_replace_error = error;
        return replaced;
    }

    bool remove(const std::filesystem::path& path,
        std::error_code& error) override
    {
        ++remove_calls;
        last_removed = path;
        if (failure == Failure::Remove)
            return fail(error);
        return delegate->remove(path, error);
    }

    std::filesystem::path temporary_directory(
        std::error_code& error) override
    {
        return delegate->temporary_directory(error);
    }

    bool fail(std::error_code& error)
    {
        error = injected_error();
        return false;
    }

    static std::error_code injected_error()
    {
        return std::make_error_code(std::errc::permission_denied);
    }

    Failure failure;
    std::shared_ptr<draxul::PluginStorageFileOperations> delegate;
    int fail_replace_call = 0;
    int replace_calls = 0;
    int remove_calls = 0;
    bool inject_collision_once = false;
    std::filesystem::path last_temporary;
    std::filesystem::path collision_temporary;
    std::filesystem::path last_removed;
    std::error_code last_replace_error;
};

std::string read_storage(draxul::PluginStorage& storage, uint32_t scope,
    std::string_view key)
{
    size_t size = 0;
    REQUIRE(storage.read_json(scope, key.data(), key.size(), nullptr,
        &size) == DRAXUL_PLUGIN_STORAGE_OK);
    std::string value(size, '\0');
    REQUIRE(storage.read_json(scope, key.data(), key.size(), value.data(),
        &size) == DRAXUL_PLUGIN_STORAGE_OK);
    value.resize(size - 1);
    return value;
}

} // namespace

TEST_CASE("PluginStorage owns scoped paths and JSON buffer policy",
    "[plugin][storage]")
{
    TempPlugins temp;
    const auto root = temp.root / "storage";
    const auto resources = temp.root / "module";
    draxul::PluginStorage storage(root);
    storage.initialize("dev.draxul.storage", "pane-1", resources);

    CHECK(storage.service_path(DRAXUL_PLUGIN_PATH_RESOURCES) == resources);
    CHECK(storage.service_path(DRAXUL_PLUGIN_PATH_CONFIG)
        == root / "config" / "dev.draxul.storage");
    CHECK(storage.service_path(DRAXUL_PLUGIN_PATH_DATA)
        == root / "data" / "dev.draxul.storage");
    CHECK(storage.service_path(DRAXUL_PLUGIN_PATH_CACHE)
        == root / "cache" / "dev.draxul.storage");
    CHECK(storage.service_path(DRAXUL_PLUGIN_PATH_TEMPORARY)
        == root / "temporary" / "dev.draxul.storage");
    CHECK(storage.storage_path(DRAXUL_PLUGIN_STORAGE_PLUGIN, "state")
        == root / "config" / "dev.draxul.storage" / "state"
            / "state.json");
    CHECK(storage.storage_path(DRAXUL_PLUGIN_STORAGE_PANE, "state")
        == root / "config" / "dev.draxul.storage" / "panes" / "pane-1"
            / "state.json");

    size_t path_size = 0;
    REQUIRE(storage.get_service_path(DRAXUL_PLUGIN_PATH_DATA, nullptr,
        &path_size));
    CHECK(std::filesystem::is_directory(
        root / "data" / "dev.draxul.storage"));
    size_t small_path_size = 1;
    char small_path[1]{};
    CHECK_FALSE(storage.get_service_path(DRAXUL_PLUGIN_PATH_DATA,
        small_path, &small_path_size));
    CHECK(small_path_size == path_size);
    std::string path_value(path_size, '\0');
    REQUIRE(storage.get_service_path(DRAXUL_PLUGIN_PATH_DATA,
        path_value.data(), &path_size));
    CHECK(std::filesystem::path(path_value.c_str())
        == root / "data" / "dev.draxul.storage");
    CHECK_FALSE(storage.get_service_path(999, nullptr, &path_size));

    constexpr std::string_view key = "state";
    constexpr std::string_view json = R"({"value":1})";
    size_t size = 0;
    CHECK(storage.read_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, key.data(),
        key.size(), nullptr, &size) == DRAXUL_PLUGIN_STORAGE_NOT_FOUND);
    CHECK(storage.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, "../bad", 6,
        json.data(), json.size()) == DRAXUL_PLUGIN_STORAGE_INVALID_KEY);
    CHECK(storage.write_json(999, key.data(), key.size(), "bad", 3)
        == DRAXUL_PLUGIN_STORAGE_INVALID_JSON);
    CHECK(storage.write_json(999, key.data(), key.size(), json.data(),
        json.size()) == DRAXUL_PLUGIN_STORAGE_SCOPE_UNAVAILABLE);
    CHECK(storage.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, key.data(),
        key.size(), nullptr, 0) == DRAXUL_PLUGIN_STORAGE_INVALID_JSON);
    std::string oversized(DRAXUL_PLUGIN_MAX_STORAGE_JSON_BYTES + 1, ' ');
    CHECK(storage.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, key.data(),
        key.size(), oversized.data(), oversized.size())
        == DRAXUL_PLUGIN_STORAGE_TOO_LARGE);

    REQUIRE(storage.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, key.data(),
        key.size(), json.data(), json.size()) == DRAXUL_PLUGIN_STORAGE_OK);
    REQUIRE(storage.read_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, key.data(),
        key.size(), nullptr, &size) == DRAXUL_PLUGIN_STORAGE_OK);
    CHECK(size == json.size() + 1);
    size_t small_size = 1;
    char small[1]{};
    CHECK(storage.read_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, key.data(),
        key.size(), small, &small_size)
        == DRAXUL_PLUGIN_STORAGE_BUFFER_TOO_SMALL);
    CHECK(small_size == json.size() + 1);
    CHECK(read_storage(storage, DRAXUL_PLUGIN_STORAGE_PLUGIN, key) == json);

    const auto path = storage.storage_path(DRAXUL_PLUGIN_STORAGE_PLUGIN, key);
    {
        std::ofstream invalid(path, std::ios::binary | std::ios::trunc);
        invalid << "bad";
    }
    CHECK(storage.read_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, key.data(),
        key.size(), nullptr, &size) == DRAXUL_PLUGIN_STORAGE_INVALID_JSON);
    {
        std::ofstream too_large(path, std::ios::binary | std::ios::trunc);
        too_large << oversized;
    }
    CHECK(storage.read_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, key.data(),
        key.size(), nullptr, &size) == DRAXUL_PLUGIN_STORAGE_TOO_LARGE);

    draxul::PluginStorage unavailable(root);
    unavailable.initialize("dev.draxul.storage", "../bad-pane", resources);
    CHECK(unavailable.write_json(DRAXUL_PLUGIN_STORAGE_PANE, key.data(),
        key.size(), json.data(), json.size())
        == DRAXUL_PLUGIN_STORAGE_SCOPE_UNAVAILABLE);
}

TEST_CASE("PluginStorage overlays stage visibility, tombstones, and commits",
    "[plugin][storage][reload]")
{
    TempPlugins temp;
    draxul::PluginStorage storage(temp.root / "storage");
    storage.initialize("dev.draxul.storage", "pane", temp.root / "module");
    constexpr std::string_view key = "state";
    constexpr std::string_view first = R"({"value":1})";
    constexpr std::string_view second = R"({"value":2})";
    REQUIRE(storage.write_json(DRAXUL_PLUGIN_STORAGE_PANE, key.data(),
        key.size(), first.data(), first.size()) == DRAXUL_PLUGIN_STORAGE_OK);

    storage.begin_overlay();
    CHECK(read_storage(storage, DRAXUL_PLUGIN_STORAGE_PANE, key) == first);
    REQUIRE(storage.write_json(DRAXUL_PLUGIN_STORAGE_PANE, key.data(),
        key.size(), second.data(), second.size())
        == DRAXUL_PLUGIN_STORAGE_OK);
    CHECK(read_storage(storage, DRAXUL_PLUGIN_STORAGE_PANE, key) == second);
    storage.discard_overlay();
    CHECK(read_storage(storage, DRAXUL_PLUGIN_STORAGE_PANE, key) == first);

    storage.begin_overlay();
    REQUIRE(storage.remove(DRAXUL_PLUGIN_STORAGE_PANE, key.data(),
        key.size()) == DRAXUL_PLUGIN_STORAGE_OK);
    size_t size = 0;
    CHECK(storage.read_json(DRAXUL_PLUGIN_STORAGE_PANE, key.data(),
        key.size(), nullptr, &size) == DRAXUL_PLUGIN_STORAGE_NOT_FOUND);
    storage.discard_overlay();
    CHECK(read_storage(storage, DRAXUL_PLUGIN_STORAGE_PANE, key) == first);

    storage.begin_overlay();
    REQUIRE(storage.write_json(DRAXUL_PLUGIN_STORAGE_PANE, key.data(),
        key.size(), second.data(), second.size())
        == DRAXUL_PLUGIN_STORAGE_OK);
    std::string error;
    REQUIRE(storage.commit_overlay(error));
    CHECK(error.empty());
    CHECK(read_storage(storage, DRAXUL_PLUGIN_STORAGE_PANE, key) == second);

    storage.begin_overlay();
    REQUIRE(storage.remove(DRAXUL_PLUGIN_STORAGE_PANE, key.data(),
        key.size()) == DRAXUL_PLUGIN_STORAGE_OK);
    REQUIRE(storage.commit_overlay(error));
    CHECK(storage.read_json(DRAXUL_PLUGIN_STORAGE_PANE, key.data(),
        key.size(), nullptr, &size) == DRAXUL_PLUGIN_STORAGE_NOT_FOUND);
}

TEST_CASE("PluginStorage preserves primary temporary-write failures",
    "[plugin][storage][reload]")
{
    using Failure = FaultInjectingStorageFiles::Failure;
    for (const auto failure : { Failure::Open, Failure::Write,
             Failure::Flush, Failure::Replace })
    {
        DYNAMIC_SECTION(static_cast<int>(failure))
        {
            TempPlugins temp;
            auto files
                = std::make_shared<FaultInjectingStorageFiles>(failure);
            draxul::PluginStorage storage(temp.root / "storage", files);
            storage.initialize("dev.draxul.storage", "pane",
                temp.root / "module");
            constexpr std::string_view key = "state";
            constexpr std::string_view json = R"({"value":1})";
            storage.begin_overlay();
            REQUIRE(storage.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN,
                key.data(), key.size(), json.data(), json.size())
                == DRAXUL_PLUGIN_STORAGE_OK);
            std::string error;
            CHECK_FALSE(storage.commit_overlay(error));
            CHECK(error.find(FaultInjectingStorageFiles::injected_error()
                    .message())
                != std::string::npos);
            CHECK(error.find("Success") == std::string::npos);
            CHECK(files->remove_calls
                == (failure == Failure::Open ? 0 : 1));
            if (failure != Failure::Open)
                CHECK(files->last_removed == files->last_temporary);
            CHECK_FALSE(std::filesystem::exists(files->last_temporary));
            CHECK_FALSE(storage.overlay_active());
        }
    }
}

TEST_CASE("PluginStorage exclusive temporary creation preserves abandoned files",
    "[plugin][storage]")
{
    TempPlugins temp;
    const auto existing = temp.root / "abandoned.tmp";
    {
        std::ofstream marker(existing, std::ios::binary);
        marker << "abandoned writer";
    }
    auto native = draxul::native_plugin_storage_file_operations();
    std::error_code error;
    CHECK_FALSE(native->open_output(existing, error));
    CHECK(error == std::errc::file_exists);
    {
        std::ifstream marker(existing, std::ios::binary);
        CHECK(std::string(std::istreambuf_iterator<char>(marker),
                  std::istreambuf_iterator<char>()) == "abandoned writer");
    }

    auto files = std::make_shared<FaultInjectingStorageFiles>(
        FaultInjectingStorageFiles::Failure::None);
    files->inject_collision_once = true;
    draxul::PluginStorage storage(temp.root / "storage", files);
    storage.initialize("dev.draxul.storage", "pane", temp.root / "module");
    constexpr std::string_view key = "state";
    constexpr std::string_view json = R"({"value":7})";
    REQUIRE(storage.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN,
        key.data(), key.size(), json.data(), json.size())
        == DRAXUL_PLUGIN_STORAGE_OK);
    CHECK(files->collision_temporary != files->last_temporary);
    CHECK(files->remove_calls == 0);
    CHECK(read_storage(storage, DRAXUL_PLUGIN_STORAGE_PLUGIN, key) == json);
    {
        std::ifstream marker(files->collision_temporary, std::ios::binary);
        CHECK(std::string(std::istreambuf_iterator<char>(marker),
                  std::istreambuf_iterator<char>()) == "abandoned writer");
    }

    files->inject_collision_once = true;
    files->failure = FaultInjectingStorageFiles::Failure::Replace;
    CHECK(storage.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN,
        key.data(), key.size(), R"({"value":8})", 11)
        == DRAXUL_PLUGIN_STORAGE_IO_ERROR);
    CHECK(files->remove_calls == 1);
    CHECK(files->last_removed == files->last_temporary);
    CHECK(files->last_removed != files->collision_temporary);
    CHECK_FALSE(std::filesystem::exists(files->last_temporary));
    CHECK(std::filesystem::exists(files->collision_temporary));
    CHECK(read_storage(storage, DRAXUL_PLUGIN_STORAGE_PLUGIN, key) == json);
}

TEST_CASE("PluginStorage concurrent writers publish complete documents",
    "[plugin][storage]")
{
    TempPlugins temp;
    constexpr std::string_view key = "shared";
    constexpr int writer_count = 8;
    constexpr int saves_per_writer = 16;
    std::vector<std::string> documents;
    documents.reserve(writer_count);
    for (int writer = 0; writer < writer_count; ++writer)
    {
        documents.push_back("{\"writer\":" + std::to_string(writer)
            + ",\"payload\":\"" + std::string(16 * 1024,
                static_cast<char>('a' + writer)) + "\"}");
    }
    std::atomic<bool> start{ false };
    std::atomic<int> ready{ 0 };
    std::atomic<int> failures{ 0 };
    std::atomic<int> first_replace_error{ 0 };
    std::vector<std::thread> writers;
    for (int writer = 0; writer < writer_count; ++writer)
    {
        writers.emplace_back([&, writer] {
            auto files = std::make_shared<FaultInjectingStorageFiles>(
                FaultInjectingStorageFiles::Failure::None);
            draxul::PluginStorage storage(temp.root / "storage", files);
            storage.initialize("dev.draxul.storage", "pane",
                temp.root / "module");
            ready.fetch_add(1);
            while (!start.load())
                std::this_thread::yield();
            const auto& document = documents[writer];
            for (int save = 0; save < saves_per_writer; ++save)
            {
                if (storage.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN,
                        key.data(), key.size(), document.data(),
                        document.size()) != DRAXUL_PLUGIN_STORAGE_OK)
                {
                    failures.fetch_add(1);
                    if (files->last_replace_error)
                    {
                        int expected = 0;
                        first_replace_error.compare_exchange_strong(expected,
                            files->last_replace_error.value());
                    }
                }
            }
        });
    }
    while (ready.load() != writer_count)
        std::this_thread::yield();
    start.store(true);
    for (auto& writer : writers)
        writer.join();
    INFO("first native replace error: " << first_replace_error.load());
    CHECK(failures.load() == 0);
    draxul::PluginStorage reader(temp.root / "storage");
    reader.initialize("dev.draxul.storage", "pane", temp.root / "module");
    const auto final = read_storage(reader, DRAXUL_PLUGIN_STORAGE_PLUGIN, key);
    CHECK(std::find(documents.begin(), documents.end(), final)
        != documents.end());
}

#ifdef _WIN32
TEST_CASE("PluginStorage child process writer", "[.][plugin][storage]")
{
    wchar_t root_buffer[32768]{};
    wchar_t writer_buffer[32]{};
    REQUIRE(GetEnvironmentVariableW(L"DRAXUL_STORAGE_PROCESS_TEST_ROOT",
                root_buffer, 32768) > 0);
    REQUIRE(GetEnvironmentVariableW(L"DRAXUL_STORAGE_PROCESS_TEST_WRITER",
                writer_buffer, 32) > 0);
    const auto root = std::filesystem::path(root_buffer);
    const int writer = std::stoi(writer_buffer);
    {
        std::ofstream ready(root / ("ready-" + std::to_string(writer)));
        REQUIRE(ready.good());
    }
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(20);
    while (!std::filesystem::exists(root / "start")
        && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(std::filesystem::exists(root / "start"));

    draxul::PluginStorage storage(root / "storage");
    storage.initialize("dev.draxul.storage", "pane", root / "module");
    const std::string document = "{\"writer\":" + std::to_string(writer)
        + ",\"payload\":\"" + std::string(16 * 1024,
            static_cast<char>('a' + writer)) + "\"}";
    constexpr std::string_view key = "shared";
    for (int save = 0; save < 16; ++save)
        REQUIRE(storage.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN,
                    key.data(), key.size(), document.data(),
                    document.size()) == DRAXUL_PLUGIN_STORAGE_OK);
}

TEST_CASE("PluginStorage separate processes publish complete documents",
    "[plugin][storage][process]")
{
    TempPlugins temp;
    wchar_t module_buffer[32768]{};
    const DWORD module_length = GetModuleFileNameW(nullptr, module_buffer,
        32768);
    REQUIRE(module_length > 0);
    REQUIRE(module_length < 32768);
    const std::wstring executable(module_buffer, module_length);
    REQUIRE(SetEnvironmentVariableW(L"DRAXUL_STORAGE_PROCESS_TEST_ROOT",
                temp.root.c_str()));

    std::vector<PROCESS_INFORMATION> children;
    for (int writer = 0; writer < 8; ++writer)
    {
        const auto index = std::to_wstring(writer);
        REQUIRE(SetEnvironmentVariableW(L"DRAXUL_STORAGE_PROCESS_TEST_WRITER",
                    index.c_str()));
        auto command = L"\"" + executable
            + L"\" \"PluginStorage child process writer\" --reporter compact";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        const bool started = CreateProcessW(executable.c_str(), command.data(),
            nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
            &startup, &child) != 0;
        if (!started)
        {
            FAIL_CHECK("could not launch storage writer process: "
                << GetLastError());
            break;
        }
        children.push_back(child);
    }
    SetEnvironmentVariableW(L"DRAXUL_STORAGE_PROCESS_TEST_ROOT", nullptr);
    SetEnvironmentVariableW(L"DRAXUL_STORAGE_PROCESS_TEST_WRITER", nullptr);

    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline)
    {
        bool all_ready = children.size() == 8;
        for (size_t writer = 0; writer < children.size(); ++writer)
            all_ready &= std::filesystem::exists(temp.root
                / ("ready-" + std::to_string(writer)));
        if (all_ready)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    {
        std::ofstream start(temp.root / "start");
        REQUIRE(start.good());
    }
    for (size_t writer = 0; writer < children.size(); ++writer)
    {
        auto& child = children[writer];
        const DWORD waited = WaitForSingleObject(child.hProcess, 30000);
        if (waited != WAIT_OBJECT_0)
        {
            TerminateProcess(child.hProcess, 1);
            WaitForSingleObject(child.hProcess, INFINITE);
        }
        DWORD exit_code = 0;
        GetExitCodeProcess(child.hProcess, &exit_code);
        INFO("writer process " << writer);
        CHECK(waited == WAIT_OBJECT_0);
        CHECK(exit_code == 0);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
    }
    REQUIRE(children.size() == 8);
    draxul::PluginStorage reader(temp.root / "storage");
    reader.initialize("dev.draxul.storage", "pane", temp.root / "module");
    constexpr std::string_view key = "shared";
    const auto final = read_storage(reader, DRAXUL_PLUGIN_STORAGE_PLUGIN, key);
    bool matches_complete_writer = false;
    for (int writer = 0; writer < 8; ++writer)
    {
        const std::string document = "{\"writer\":" + std::to_string(writer)
            + ",\"payload\":\"" + std::string(16 * 1024,
                static_cast<char>('a' + writer)) + "\"}";
        matches_complete_writer |= final == document;
    }
    CHECK(matches_complete_writer);
}
#endif

TEST_CASE("PluginStorage reports filesystem failures and partial commit",
    "[plugin][storage][reload]")
{
    TempPlugins temp;
    auto files = std::make_shared<FaultInjectingStorageFiles>(
        FaultInjectingStorageFiles::Failure::CreateDirectories);
    draxul::PluginStorage storage(temp.root / "storage", files);
    storage.initialize("dev.draxul.storage", "pane", temp.root / "module");
    size_t path_size = 0;
    CHECK_FALSE(storage.get_service_path(DRAXUL_PLUGIN_PATH_DATA, nullptr,
        &path_size));
    constexpr std::string_view first_key = "first";
    constexpr std::string_view partial_first_key = "partial-first";
    constexpr std::string_view partial_second_key = "partial-second";
    constexpr std::string_view json = R"({"value":1})";
    CHECK(storage.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, first_key.data(),
        first_key.size(), json.data(), json.size())
        == DRAXUL_PLUGIN_STORAGE_IO_ERROR);

    files->failure = FaultInjectingStorageFiles::Failure::None;
    REQUIRE(storage.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN,
        first_key.data(), first_key.size(), json.data(), json.size())
        == DRAXUL_PLUGIN_STORAGE_OK);
    files->failure = FaultInjectingStorageFiles::Failure::FileSize;
    size_t size = 0;
    CHECK(storage.read_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, first_key.data(),
        first_key.size(), nullptr, &size) == DRAXUL_PLUGIN_STORAGE_IO_ERROR);
    files->failure = FaultInjectingStorageFiles::Failure::Read;
    CHECK(storage.read_json(DRAXUL_PLUGIN_STORAGE_PLUGIN, first_key.data(),
        first_key.size(), nullptr, &size) == DRAXUL_PLUGIN_STORAGE_IO_ERROR);
    files->failure = FaultInjectingStorageFiles::Failure::Remove;
    CHECK(storage.remove(DRAXUL_PLUGIN_STORAGE_PLUGIN, first_key.data(),
        first_key.size()) == DRAXUL_PLUGIN_STORAGE_IO_ERROR);

    files->failure = FaultInjectingStorageFiles::Failure::None;
    files->fail_replace_call = files->replace_calls + 2;
    storage.begin_overlay();
    REQUIRE(storage.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN,
        partial_first_key.data(), partial_first_key.size(), json.data(),
        json.size())
        == DRAXUL_PLUGIN_STORAGE_OK);
    REQUIRE(storage.write_json(DRAXUL_PLUGIN_STORAGE_PLUGIN,
        partial_second_key.data(), partial_second_key.size(), json.data(),
        json.size())
        == DRAXUL_PLUGIN_STORAGE_OK);
    std::string error;
    CHECK_FALSE(storage.commit_overlay(error));
    CHECK(error.find(FaultInjectingStorageFiles::injected_error().message())
        != std::string::npos);
    const bool first_exists = std::filesystem::exists(
        storage.storage_path(DRAXUL_PLUGIN_STORAGE_PLUGIN,
            partial_first_key));
    const bool second_exists = std::filesystem::exists(
        storage.storage_path(DRAXUL_PLUGIN_STORAGE_PLUGIN,
            partial_second_key));
    CHECK(first_exists != second_exists);
}

#ifdef DRAXUL_MEGACITY_PLUGIN_PATH
TEST_CASE("MegaCity module creates the real City and Biology products",
    "[plugin][megacity][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "megacity", "dev.draxul.megacity",
        DRAXUL_MEGACITY_PLUGIN_PATH, {}, "MegaCity / BioView", "0.1.0");
    const auto manager = draxul::PluginManager::discover(bundled, user);
    const auto source = temp.root / "source";
    std::filesystem::create_directories(source);
    {
        std::ofstream file(source / "widget.cpp");
        file << "namespace demo { class Widget { public: void run(); "
                "int value = 0; }; void Widget::run() {} }\n";
    }
    const std::string source_json = source.generic_string();

    for (const auto& [mode, expected_name] : {
             std::pair{ "city", "MegaCity" },
             std::pair{ "biology", "BioView" } })
    {
        draxul::HostContext context;
        context.launch_options.kind = draxul::HostKind::Plugin;
        context.launch_options.client_plugin_id = "dev.draxul.megacity";
        context.launch_options.client_plugin_config_json =
            std::string("{\"mode\":\"") + mode
            + "\",\"source\":\"" + source_json + "\"}";
        context.pane_id = std::string("megacity-") + mode;
        context.initial_viewport.pixel_size = { 640, 360 };

        draxul::PluginHost host(manager, temp.root / "storage");
        draxul::tests::TestHostCallbacks callbacks;
        REQUIRE(host.initialize(context, callbacks));
        CHECK(host.display_name() == expected_name);
        CHECK(host.runtime_state().content_ready);

        const auto scan_deadline = std::chrono::steady_clock::now()
            + std::chrono::seconds(10);
        while (host.status_text().find(" objects") == std::string::npos
            && std::chrono::steady_clock::now() < scan_deadline)
        {
            host.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        INFO(host.status_text());
        CHECK(host.status_text().find(" objects") != std::string::npos);
        CHECK(host.status_text().find(" | 0 objects") == std::string::npos);
        host.set_presentation_visible(false);
        CHECK(host.status_text().find("hidden") != std::string::npos);
        host.shutdown();

        const std::filesystem::path preferences_path
            = temp.root / "storage" / "config" / "dev.draxul.megacity"
            / (std::string(mode) == "biology"
                    ? "bioview-preferences.toml"
                    : "megacity-preferences.toml");
        REQUIRE(std::filesystem::exists(preferences_path));
        std::ifstream preferences_input(preferences_path);
        std::string preferences(std::istreambuf_iterator<char>{ preferences_input }, {});
        preferences_input.close();
        CHECK(preferences.find("camera_state_valid = true")
            != std::string::npos);
        const std::string enabled = "show_ui_panels = true";
        const size_t setting = preferences.find(enabled);
        REQUIRE(setting != std::string::npos);
        preferences.replace(setting, enabled.size(), "show_ui_panels = false");
        {
            std::ofstream preferences_output(preferences_path, std::ios::trunc);
            REQUIRE(preferences_output.good());
            preferences_output << preferences;
        }

        draxul::PluginHost reopened(manager, temp.root / "storage");
        REQUIRE(reopened.initialize(context, callbacks));
        std::string reload_error;
        const auto candidate = reopened.prepare_reload(reload_error);
        INFO(reload_error);
        REQUIRE(candidate);
        std::string reload_warning;
        reopened.quiesce_for_reload(reload_warning);
        REQUIRE(reopened.reload(candidate, reload_warning, reload_error));
        reopened.shutdown();
        std::ifstream restored_input(preferences_path);
        const std::string restored(std::istreambuf_iterator<char>{ restored_input }, {});
        CHECK(restored.find("show_ui_panels = false") != std::string::npos);
    }
}
#endif

#ifdef DRAXUL_SCOREVIEW_PLUGIN_PATH
TEST_CASE("ScoreView module creates its real runtime through the public ABI",
    "[plugin][scoreview][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "scoreview", "dev.draxul.scoreview",
        DRAXUL_SCOREVIEW_PLUGIN_PATH, {}, "ScoreView", "0.1.0");
    const auto manager = draxul::PluginManager::discover(bundled, user);

    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.client_plugin_id = "dev.draxul.scoreview";
    context.launch_options.client_plugin_config_json = R"({"mode":"paged"})";
    context.pane_id = "scoreview-real-module";
    context.initial_viewport.pixel_size = { 640, 360 };

    draxul::PluginHost host(manager, temp.root / "storage");
    draxul::tests::TestHostCallbacks callbacks;
    REQUIRE(host.initialize(context, callbacks));
    CHECK(host.display_name() == "ScoreView");
    CHECK(host.status_text().find("ScoreView plugin failed")
        == std::string::npos);

    auto resized = context.initial_viewport;
    resized.pixel_size = { 800, 450 };
    host.set_viewport(resized);
    host.set_presentation_visible(false);
    host.set_presentation_visible(true);
    host.on_focus_gained();
    host.on_key({ 44, 32, draxul::kModNone, true });
    host.on_focus_lost();
    host.shutdown();
}
#endif

TEST_CASE("spinning triangle persists pane-local state through host services",
    "[plugin][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "triangle", "dev.draxul.spinning-triangle",
        DRAXUL_TRIANGLE_PLUGIN_PATH, {}, "Spinning Triangle", "0.2.0");
    const auto manager = draxul::PluginManager::discover(bundled, user);

    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.client_plugin_id
        = "dev.draxul.spinning-triangle";
    context.launch_options.client_plugin_config_json
        = R"({"remember_state":true})";
    context.pane_id = "pane-a";
    context.initial_viewport.pixel_size = { 640, 360 };

    const auto first_ui_root = temp.root / "ui-a";
    {
        draxul::PluginHost host(manager, first_ui_root);
        draxul::tests::TestHostCallbacks callbacks;
        REQUIRE(host.initialize(context, callbacks));
        CHECK(host.status_text().find("running clockwise")
            != std::string::npos);
        CHECK(host.status_text().find("remembered") != std::string::npos);
        CHECK(host.status_text().find("paths ready") != std::string::npos);
        CHECK(host.dispatch_action("reverse"));
        CHECK(host.dispatch_action("toggle_pause"));
        CHECK(host.status_text().find("paused counter-clockwise")
            != std::string::npos);

        auto resized = context.initial_viewport;
        resized.pixel_size = { 800, 450 };
        host.set_viewport(resized);
        CHECK(host.status_text().find("800x450") != std::string::npos);

        host.set_presentation_visible(false);
        CHECK(host.status_text().find("hidden") != std::string::npos);
        host.set_presentation_visible(true);
        host.on_focus_gained();
        CHECK(host.status_text().find("focused") != std::string::npos);

        // Exercise SDK-owned input against the independently built product
        // boundary, then restore the durable state asserted below.
        host.on_key({ 44, 32, draxul::kModNone, true });
        CHECK(host.status_text().find("running counter-clockwise")
            != std::string::npos);
        host.on_key({ 44, 32, draxul::kModNone, true });
        host.on_mouse_button({ 1, true, draxul::kModNone,
            { 20, 20 }, 1 });
        CHECK(host.status_text().find("paused clockwise")
            != std::string::npos);
        host.on_mouse_button({ 1, true, draxul::kModNone,
            { 20, 20 }, 1 });
        CHECK(host.status_text().find("paused counter-clockwise")
            != std::string::npos);
        host.on_focus_lost();
        host.shutdown();
    }

    // Recreating the client-local host for the same server pane restores its
    // state without changing the pane's shared launch configuration.
    {
        draxul::PluginHost host(manager, first_ui_root);
        draxul::tests::TestHostCallbacks callbacks;
        REQUIRE(host.initialize(context, callbacks));
        CHECK(host.status_text().find("paused counter-clockwise")
            != std::string::npos);
        host.shutdown();
    }

    // Another attached UI resolves the same topology against independent
    // local storage and therefore begins with the launch defaults.
    {
        draxul::PluginHost host(manager, temp.root / "ui-b");
        draxul::tests::TestHostCallbacks callbacks;
        REQUIRE(host.initialize(context, callbacks));
        CHECK(host.status_text().find("running clockwise")
            != std::string::npos);
        host.shutdown();
    }

    // Corrupt local state is an instance-local warning, not a failed shared
    // pane. The plugin remains interactive and can replace the bad document.
    const auto corrupt_root = temp.root / "ui-corrupt";
    const auto corrupt_file = corrupt_root / "config"
        / "dev.draxul.spinning-triangle" / "panes" / "pane-a"
        / "state.json";
    std::filesystem::create_directories(corrupt_file.parent_path());
    {
        std::ofstream output(corrupt_file);
        output << "not-json";
    }
    {
        draxul::PluginHost host(manager, corrupt_root);
        draxul::tests::TestHostCallbacks callbacks;
        REQUIRE(host.initialize(context, callbacks));
        CHECK(host.status_text().find("saved state is corrupt")
            != std::string::npos);
        CHECK(host.dispatch_action("toggle_pause"));
        host.shutdown();
    }
}

TEST_CASE("PluginHost saves pane state during close and reload quiescence",
    "[plugin][storage][reload][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    install_plugin(bundled, "fixture", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    const auto manager = draxul::PluginManager::discover(
        bundled, temp.root / "user", temp.root / "runtime");
    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.client_plugin_id = "dev.draxul.fixture";
    context.launch_options.client_plugin_config_json
        = R"({"quiesce_save":true,"retirement_race":true})";
    context.pane_id = "quiesce-pane";
    context.initial_viewport.pixel_size = { 640, 360 };
    draxul::tests::FakeTermRenderer renderer;
    context.grid_renderer = &renderer;
    const auto saved = temp.root / "storage" / "config"
        / "dev.draxul.fixture" / "panes" / context.pane_id
        / "quiesce-save.json";
    const auto stale = saved.parent_path() / "stale-save.json";

    draxul::PluginHost host(manager, temp.root / "storage");
    draxul::tests::TestHostCallbacks callbacks;
    REQUIRE(host.initialize(context, callbacks));
    const auto old_plugin = host.loaded_plugin();
    REQUIRE(old_plugin);
#ifdef _WIN32
    HMODULE fixture = LoadLibraryW(old_plugin->manifest().library_path.c_str());
    REQUIRE(fixture);
    const auto symbol = [&](const char* name) {
        return reinterpret_cast<void*>(GetProcAddress(fixture, name));
    };
#else
    void* fixture = dlopen(old_plugin->manifest().library_path.c_str(),
        RTLD_NOW | RTLD_LOCAL);
    REQUIRE(fixture);
    const auto symbol = [&](const char* name) { return dlsym(fixture, name); };
#endif
    const auto quiesce_result = reinterpret_cast<uint32_t (*)()>(
        symbol("draxul_fixture_quiesce_storage_result"));
    const auto worker_result = reinterpret_cast<uint32_t (*)()>(
        symbol("draxul_fixture_quiesce_worker_storage_result"));
    const auto stale_write = reinterpret_cast<uint32_t (*)()>(
        symbol("draxul_fixture_write_storage_from_quiesced"));
    const auto retirement_attempts = reinterpret_cast<size_t (*)()>(
        symbol("draxul_fixture_retirement_worker_attempts"));
    REQUIRE(quiesce_result);
    REQUIRE(worker_result);
    REQUIRE(stale_write);
    REQUIRE(retirement_attempts);
    CHECK_FALSE(std::filesystem::exists(saved));

    std::string error;
    const auto candidate = host.prepare_reload(error);
    REQUIRE(candidate);
    std::string warning;
    host.quiesce_for_reload(warning);
    CHECK(warning.empty());
    CHECK(retirement_attempts() >= 64);
    CHECK(quiesce_result() == DRAXUL_PLUGIN_STORAGE_OK);
    CHECK(worker_result() == DRAXUL_PLUGIN_STORAGE_WRONG_THREAD);
    CHECK(std::filesystem::exists(saved));
    CHECK(stale_write() == DRAXUL_PLUGIN_STORAGE_WRONG_THREAD);
    CHECK_FALSE(std::filesystem::exists(stale));

    REQUIRE(host.reload(candidate, warning, error));
    CHECK(warning.empty());
    CHECK(stale_write() == DRAXUL_PLUGIN_STORAGE_WRONG_THREAD);
    CHECK_FALSE(std::filesystem::exists(stale));
    host.shutdown();
#ifdef _WIN32
    FreeLibrary(fixture);
#else
    dlclose(fixture);
#endif

    // Removing the saved file proves the final close writes it again rather
    // than merely preserving the reload write.
    draxul::PluginHost reopened(manager, temp.root / "storage");
    REQUIRE(reopened.initialize(context, callbacks));
    REQUIRE(std::filesystem::remove(saved));
    reopened.shutdown();
    CHECK(std::filesystem::exists(saved));
}

#ifdef DRAXUL_TEST_SATVIEW_PACKAGE_ROOT
TEST_CASE("SatView preferences survive real plugin close and reload quiescence",
    "[plugin][satview][storage][reload][integration]")
{
    TempPlugins temp;
    const auto manager = draxul::PluginManager::discover(
        DRAXUL_TEST_SATVIEW_PACKAGE_ROOT,
        temp.root / "user", temp.root / "runtime");
    REQUIRE(manager->find("dev.draxul.satview"));

    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.client_plugin_id = "dev.draxul.satview";
    context.launch_options.client_plugin_config_json
        = R"({"remember_state":true})";
    context.pane_id = "satview-preferences";
    context.initial_viewport.pixel_size = { 640, 360 };
    draxul::tests::FakeTermRenderer renderer;
    context.grid_renderer = &renderer;
    draxul::tests::TestHostCallbacks callbacks;
    const auto storage_root = temp.root / "storage";
    const auto state_file = storage_root / "config"
        / "dev.draxul.satview" / "panes" / context.pane_id
        / "state.json";

    const auto saved_speed = [&]() -> float {
        std::ifstream input(state_file, std::ios::binary);
        REQUIRE(input.good());
        const std::string json(std::istreambuf_iterator<char>(input), {});
        const auto start = json.find("time_speed = ");
        REQUIRE(start != std::string::npos);
        return std::stof(json.substr(start + std::strlen("time_speed = ")));
    };

    {
        draxul::PluginHost host(manager, storage_root);
        REQUIRE(host.initialize(context, callbacks));
        REQUIRE(host.dispatch_action("satview_time_faster"));
        REQUIRE(std::filesystem::remove(state_file));
        host.shutdown();
    }
    CHECK(saved_speed() == 120.0f);

    {
        draxul::PluginHost host(manager, storage_root);
        REQUIRE(host.initialize(context, callbacks));
        REQUIRE(host.dispatch_action("satview_time_faster"));
        CHECK(saved_speed() == 240.0f);
        REQUIRE(std::filesystem::remove(state_file));

        std::string error;
        const auto candidate = host.prepare_reload(error);
        INFO(error);
        REQUIRE(candidate);
        std::string warning;
        host.quiesce_for_reload(warning);
        INFO(warning);
        CHECK(warning.empty());
        CHECK(saved_speed() == 240.0f);
        REQUIRE(host.reload(candidate, warning, error));
        REQUIRE(host.dispatch_action("satview_time_faster"));
        CHECK(saved_speed() == 480.0f);
        REQUIRE(std::filesystem::remove(state_file));
        host.shutdown();
    }
    CHECK(saved_speed() == 480.0f);

    const auto saved_pause = [&]() -> bool {
        std::ifstream input(state_file, std::ios::binary);
        REQUIRE(input.good());
        const nlohmann::json state = nlohmann::json::parse(input);
        REQUIRE(state.contains("paused"));
        return state["paused"].get<bool>();
    };
    {
        draxul::PluginHost host(manager, storage_root);
        REQUIRE(host.initialize(context, callbacks));
        REQUIRE(host.dispatch_action("satview_toggle_pause"));
        CHECK(saved_pause());
        host.shutdown();
    }
    {
        draxul::PluginHost host(manager, storage_root);
        REQUIRE(host.initialize(context, callbacks));
        REQUIRE(host.dispatch_action("satview_toggle_pause"));
        CHECK_FALSE(saved_pause());
        host.shutdown();
    }
}
#endif

TEST_CASE("PluginHost publishes non-ASCII resource directories as UTF-8",
    "[plugin][integration]")
{
    TempPlugins temp;
    const auto unicode_root = temp.root
        / std::filesystem::u8path(u8"caf\u00e9-\u732b");
    const auto bundled = unicode_root / "bundled";
    install_plugin(bundled, "fixture", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    const auto asset = bundled / "fixture" / "generations" / "build-1"
        / "assets" / "probe.txt";
    std::filesystem::create_directories(asset.parent_path());
    {
        std::ofstream file(asset);
        file << "resource found";
    }
    const auto manager = draxul::PluginManager::discover(
        bundled, temp.root / "user", unicode_root / "runtime");
    draxul::PluginHost host(manager, temp.root / "storage");
    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.client_plugin_id = "dev.draxul.fixture";
    context.pane_id = "utf8-pane";
    context.initial_viewport.pixel_size = { 640, 360 };
    draxul::tests::FakeTermRenderer renderer;
    context.grid_renderer = &renderer;
    draxul::tests::TestHostCallbacks callbacks;
    const bool initialized = host.initialize(context, callbacks);
    INFO(host.init_error());
    REQUIRE(initialized);
    const auto plugin = host.loaded_plugin();
    REQUIRE(plugin);
    CHECK(plugin->manifest().directory.u8string().find(
        u8"caf\u00e9-\u732b") != std::u8string::npos);
#ifdef _WIN32
    HMODULE fixture = LoadLibraryW(plugin->manifest().library_path.c_str());
    REQUIRE(fixture);
    const auto probe = reinterpret_cast<int (*)()>(
        GetProcAddress(fixture, "draxul_fixture_resource_probe_found"));
#else
    void* fixture = dlopen(plugin->manifest().library_path.c_str(),
        RTLD_NOW | RTLD_LOCAL);
    REQUIRE(fixture);
    const auto probe = reinterpret_cast<int (*)()>(
        dlsym(fixture, "draxul_fixture_resource_probe_found"));
#endif
    REQUIRE(probe);
    CHECK(probe() == 1);
    host.shutdown();
#ifdef _WIN32
    FreeLibrary(fixture);
#else
    dlclose(fixture);
#endif
}

TEST_CASE("PluginHost translates SDK-owned input through a real module",
    "[plugin][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "input", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    const auto manager = draxul::PluginManager::discover(bundled, user);
    std::string error;
    const auto loaded = manager->load("dev.draxul.fixture", error);
    REQUIRE(loaded);
#ifdef _WIN32
    HMODULE fixture = LoadLibraryW(loaded->manifest().library_path.c_str());
    REQUIRE(fixture);
    const auto symbol = [&](const char* name) {
        return reinterpret_cast<void*>(GetProcAddress(fixture, name));
    };
#else
    void* fixture = dlopen(loaded->manifest().library_path.c_str(),
        RTLD_NOW | RTLD_LOCAL);
    REQUIRE(fixture);
    const auto symbol = [&](const char* name) { return dlsym(fixture, name); };
#endif
    const auto reset = reinterpret_cast<void (*)()>(
        symbol("draxul_fixture_reset_events"));
    const auto count = reinterpret_cast<size_t (*)()>(
        symbol("draxul_fixture_event_count"));
    const auto event_at = reinterpret_cast<int (*)(size_t,
        DraxulPluginInputEventV2*)>(
        symbol("draxul_fixture_event_at"));
    const auto fixture_request_tick = reinterpret_cast<void (*)()>(
        symbol("draxul_fixture_request_tick"));
    const auto fixture_tick_count = reinterpret_cast<size_t (*)()>(
        symbol("draxul_fixture_tick_count"));
    const auto fixture_quiesce_count = reinterpret_cast<size_t (*)()>(
        symbol("draxul_fixture_quiesce_count"));
    const auto fixture_action_count = reinterpret_cast<size_t (*)()>(
        symbol("draxul_fixture_action_dispatch_count"));
    const auto fixture_query_service = reinterpret_cast<int (*)(const char*,
        size_t, uint32_t, void*, size_t)>(
        symbol("draxul_fixture_query_host_service"));
    const auto support_path = reinterpret_cast<int (*)(uint32_t, char*, size_t*)>(
        symbol("draxul_fixture_support_path"));
    const auto support_write_json = reinterpret_cast<uint32_t (*)(uint32_t,
        const char*, size_t, const char*, size_t)>(
        symbol("draxul_fixture_support_write_json"));
    const auto support_read_json = reinterpret_cast<uint32_t (*)(uint32_t,
        const char*, size_t, char*, size_t*)>(
        symbol("draxul_fixture_support_read_json"));
    const auto support_remove = reinterpret_cast<uint32_t (*)(uint32_t,
        const char*, size_t)>(symbol("draxul_fixture_support_remove"));
    const auto support_ui_style = reinterpret_cast<int (*)(char*, size_t*,
        float*, float*, uint64_t*)>(
        symbol("draxul_fixture_support_ui_style"));
    REQUIRE(reset);
    REQUIRE(count);
    REQUIRE(event_at);
    REQUIRE(fixture_request_tick);
    REQUIRE(fixture_tick_count);
    REQUIRE(fixture_quiesce_count);
    REQUIRE(fixture_action_count);
    REQUIRE(fixture_query_service);
    REQUIRE(support_path);
    REQUIRE(support_write_json);
    REQUIRE(support_read_json);
    REQUIRE(support_remove);
    REQUIRE(support_ui_style);
    reset();

    draxul::tests::FakeTermRenderer renderer;
    draxul::PluginHost host(manager, temp.root / "storage");
    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.client_plugin_id = "dev.draxul.fixture";
    context.launch_options.client_plugin_config_json = "{}";
    context.pane_id = "fixture-pane";
    context.initial_viewport.pixel_pos = { 100, 50 };
    context.initial_viewport.pixel_size = { 640, 360 };
    context.initial_viewport.pixel_scale = 1.5f;
    context.display_ppi = 144.0f;
    context.grid_renderer = &renderer;
    draxul::tests::TestHostCallbacks callbacks;
    REQUIRE(host.initialize(context, callbacks));

    DraxulPluginPathServiceV2 paths{};
    REQUIRE(fixture_query_service(DRAXUL_PLUGIN_PATH_SERVICE_ID,
        std::strlen(DRAXUL_PLUGIN_PATH_SERVICE_ID),
        DRAXUL_PLUGIN_PATH_SERVICE_VERSION, &paths, sizeof(paths)));
    size_t path_size = 0;
    REQUIRE(paths.get_path(paths.service_context, DRAXUL_PLUGIN_PATH_DATA,
        nullptr, &path_size));
    REQUIRE(path_size > 1);
    std::string data_path(path_size, '\0');
    REQUIRE(paths.get_path(paths.service_context, DRAXUL_PLUGIN_PATH_DATA,
        data_path.data(), &path_size));
    CHECK(std::filesystem::exists(std::filesystem::u8path(data_path.c_str())));

    size_t support_path_size = 0;
    REQUIRE(support_path(DRAXUL_PLUGIN_PATH_DATA, nullptr,
        &support_path_size));
    std::string support_data_path(support_path_size, '\0');
    REQUIRE(support_path(DRAXUL_PLUGIN_PATH_DATA,
        support_data_path.data(), &support_path_size));
    CHECK(std::string_view(support_data_path.c_str())
        == std::string_view(data_path.c_str()));

    DraxulPluginStorageServiceV2 storage{};
    REQUIRE(fixture_query_service(DRAXUL_PLUGIN_STORAGE_SERVICE_ID,
        std::strlen(DRAXUL_PLUGIN_STORAGE_SERVICE_ID),
        DRAXUL_PLUGIN_STORAGE_SERVICE_VERSION, &storage, sizeof(storage)));

    DraxulPluginUiStyleServiceV2 ui_style{};
    REQUIRE(fixture_query_service(DRAXUL_PLUGIN_UI_STYLE_SERVICE_ID,
        std::strlen(DRAXUL_PLUGIN_UI_STYLE_SERVICE_ID),
        DRAXUL_PLUGIN_UI_STYLE_SERVICE_VERSION,
        &ui_style, sizeof(ui_style)));
    size_t recommended_path_size = 0;
    float recommended_size = 0.0f;
    float display_scale = 0.0f;
    uint64_t initial_style_generation = 0;
    REQUIRE(ui_style.get_recommended_font(ui_style.service_context, nullptr,
        &recommended_path_size, &recommended_size, &display_scale,
        &initial_style_generation));
    CHECK(recommended_path_size == 1);
    CHECK(recommended_size == 13.0f);
    CHECK(display_scale == 1.5f);

    host.set_imgui_font("fixture-font.ttf", 17.0f);
    uint64_t updated_style_generation = 0;
    REQUIRE(ui_style.get_recommended_font(ui_style.service_context, nullptr,
        &recommended_path_size, &recommended_size, &display_scale,
        &updated_style_generation));
    REQUIRE(updated_style_generation > initial_style_generation);
    std::string recommended_path(recommended_path_size, '\0');
    REQUIRE(ui_style.get_recommended_font(ui_style.service_context,
        recommended_path.data(), &recommended_path_size, &recommended_size,
        &display_scale, &updated_style_generation));
    CHECK(std::string_view(recommended_path.c_str()) == "fixture-font.ttf");
    CHECK(recommended_size == 17.0f);
    size_t support_font_path_size = 0;
    float support_font_size = 0.0f;
    float support_display_scale = 0.0f;
    uint64_t support_style_generation = 0;
    REQUIRE(support_ui_style(nullptr, &support_font_path_size,
        &support_font_size, &support_display_scale,
        &support_style_generation));
    std::string support_font_path(support_font_path_size, '\0');
    REQUIRE(support_ui_style(support_font_path.data(),
        &support_font_path_size, &support_font_size,
        &support_display_scale, &support_style_generation));
    CHECK(std::string_view(support_font_path.c_str()) == "fixture-font.ttf");
    CHECK(support_font_size == 17.0f);
    CHECK(support_display_scale == 1.5f);
    CHECK(support_style_generation == updated_style_generation);
    host.draw(renderer.frame_context);
    REQUIRE(renderer.recorded_render_viewports.size() == 1);
    CHECK(renderer.recorded_render_viewports[0].x == 100);
    CHECK(renderer.recorded_render_viewports[0].y == 50);
    CHECK(renderer.recorded_render_viewports[0].width == 640);
    CHECK(renderer.recorded_render_viewports[0].height == 360);
    constexpr std::string_view state_key = "fixture-state";
    constexpr std::string_view first_json = R"({"value":1})";
    constexpr std::string_view second_json = R"({"value":2})";
    CHECK(storage.write_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
        first_json.data(), first_json.size()) == DRAXUL_PLUGIN_STORAGE_OK);
    CHECK(storage.write_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
        second_json.data(), second_json.size()) == DRAXUL_PLUGIN_STORAGE_OK);
    size_t state_size = 0;
    REQUIRE(storage.read_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
        nullptr, &state_size) == DRAXUL_PLUGIN_STORAGE_OK);
    std::string saved_state(state_size, '\0');
    REQUIRE(storage.read_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
        saved_state.data(), &state_size) == DRAXUL_PLUGIN_STORAGE_OK);
    CHECK(std::string_view(saved_state.c_str()) == second_json);
    constexpr std::string_view support_key = "support-state";
    CHECK(support_write_json(DRAXUL_PLUGIN_STORAGE_PANE,
        support_key.data(), support_key.size(), second_json.data(),
        second_json.size()) == DRAXUL_PLUGIN_STORAGE_OK);
    size_t support_state_size = 0;
    REQUIRE(support_read_json(DRAXUL_PLUGIN_STORAGE_PANE,
        support_key.data(), support_key.size(), nullptr,
        &support_state_size) == DRAXUL_PLUGIN_STORAGE_OK);
    std::string support_state(support_state_size, '\0');
    REQUIRE(support_read_json(DRAXUL_PLUGIN_STORAGE_PANE,
        support_key.data(), support_key.size(), support_state.data(),
        &support_state_size) == DRAXUL_PLUGIN_STORAGE_OK);
    CHECK(std::string_view(support_state.c_str()) == second_json);
    CHECK(support_remove(DRAXUL_PLUGIN_STORAGE_PANE,
        support_key.data(), support_key.size()) == DRAXUL_PLUGIN_STORAGE_OK);
    CHECK(storage.write_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, "../escape", 9,
        second_json.data(), second_json.size())
        == DRAXUL_PLUGIN_STORAGE_INVALID_KEY);
    CHECK(storage.write_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
        "bad", 3) == DRAXUL_PLUGIN_STORAGE_INVALID_JSON);
    std::string oversized(DRAXUL_PLUGIN_MAX_STORAGE_JSON_BYTES + 1, ' ');
    CHECK(storage.write_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
        oversized.data(), oversized.size())
        == DRAXUL_PLUGIN_STORAGE_TOO_LARGE);
    uint32_t worker_result = DRAXUL_PLUGIN_STORAGE_OK;
    std::thread worker([&] {
        size_t size = 0;
        worker_result = storage.read_json(storage.service_context,
            DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
            nullptr, &size);
    });
    worker.join();
    CHECK(worker_result == DRAXUL_PLUGIN_STORAGE_WRONG_THREAD);
    CHECK(storage.remove(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size())
        == DRAXUL_PLUGIN_STORAGE_OK);
    state_size = 0;
    CHECK(storage.read_json(storage.service_context,
        DRAXUL_PLUGIN_STORAGE_PANE, state_key.data(), state_key.size(),
        nullptr, &state_size) == DRAXUL_PLUGIN_STORAGE_NOT_FOUND);
    CHECK(host.display_name() == "Fixture instance");
    CHECK(host.status_text() == "ready");
    CHECK(host.runtime_state().content_ready);
    CHECK(host.default_background().r == 0.1f);
    CHECK(host.mouse_cursor_at(10, 10) == draxul::MouseCursor::Pointer);
    CHECK(host.print_hint().content_pos == glm::ivec2(1, 2));
    CHECK(host.print_hint().content_size == glm::ivec2(30, 40));
    CHECK(host.print_hint().paper_white);

    // The initial logic deadline invokes the real module's tick callback and
    // converts its redraw result into a host frame request.
    REQUIRE(host.next_deadline().has_value());
    const int frames_before_initial_tick = callbacks.request_frame_calls;
    host.pump();
    CHECK(fixture_tick_count() == 1);
    CHECK(callbacks.request_frame_calls == frames_before_initial_tick + 1);
    CHECK_FALSE(host.next_deadline().has_value());

    // A module callback requests logic work without directly requesting a
    // render. PluginHost wakes, ticks on the main thread, then honors the
    // tick result's redraw bit.
    const int wakes_before_tick = callbacks.wake_window_calls;
    fixture_request_tick();
    CHECK(callbacks.wake_window_calls == wakes_before_tick + 1);
    host.pump();
    CHECK(fixture_tick_count() == 2);

    // Render scheduling remains independent from logic scheduling.
    host.accept_render_result({ sizeof(DraxulPluginRenderResultV2),
        1'000'000, 1, nullptr });
    CHECK(host.next_deadline().has_value());
    host.set_presentation_visible(false);
    CHECK(host.status_text() == "hidden");
    REQUIRE(host.next_deadline().has_value());
    host.pump();
    CHECK_FALSE(host.next_deadline().has_value());
    const int frames_before_show = callbacks.request_frame_calls;
    host.set_presentation_visible(true);
    CHECK(callbacks.request_frame_calls > frames_before_show);
    REQUIRE(host.next_deadline().has_value());
    host.pump();
    CHECK_FALSE(host.next_deadline().has_value());
    host.accept_render_result({ sizeof(DraxulPluginRenderResultV2),
        0, 1, nullptr });
    const int frames_before_deadline = callbacks.request_frame_calls;
    REQUIRE(host.next_deadline().has_value());
    host.pump();
    CHECK(callbacks.request_frame_calls
        == frames_before_deadline + 1);
    CHECK_FALSE(host.next_deadline().has_value());
    CHECK(host.dispatch_action("fixture_action"));
    CHECK_FALSE(host.dispatch_action("unknown"));
    CHECK(fixture_action_count() == 1);
    REQUIRE(host.palette_actions().size() == 1);
    CHECK(host.palette_actions().front().id == "fixture_action");
    CHECK(host.palette_actions().front().display_name == "Fixture Action");
    const int frames_before_metadata = callbacks.request_frame_calls;
    host.pump();
    CHECK(callbacks.request_frame_calls == frames_before_metadata + 1);
    host.on_focus_gained();
    CHECK(host.status_text().find("focused") != std::string::npos);
    host.on_key({ 44, 32, draxul::kModCtrl, true });
    host.on_text_input({ "hello" });
    host.on_text_editing({ "compose", 2, 3 });
    host.on_mouse_button({ 1, true, draxul::kModShift,
        { 125, 90 }, 2 });
    host.on_mouse_move({ draxul::kModAlt, { 130, 100 },
        { 5.0f, 10.0f }, 1 });
    host.on_mouse_wheel({ { 1.0f, -2.0f }, draxul::kModNone,
        { 140, 110 } });
    host.on_focus_lost();

    REQUIRE(count() == 8);
    DraxulPluginInputEventV2 event{};
    REQUIRE(event_at(1, &event));
    CHECK(event.kind == DRAXUL_PLUGIN_INPUT_KEY);
    CHECK(event.physical_key == 44);
    CHECK(event.logical_key == 32);
    CHECK(event.modifiers == draxul::kModCtrl);
    REQUIRE(event_at(2, &event));
    CHECK(std::string(event.text_utf8, event.text_length) == "hello");
    REQUIRE(event_at(3, &event));
    CHECK(event.kind == DRAXUL_PLUGIN_INPUT_COMPOSITION);
    CHECK(event.composition_start == 2);
    CHECK(event.composition_length == 3);
    REQUIRE(event_at(4, &event));
    CHECK(event.kind == DRAXUL_PLUGIN_INPUT_POINTER_BUTTON);
    CHECK(event.x == 25);
    CHECK(event.y == 40);
    CHECK(event.clicks == 2);
    REQUIRE(event_at(5, &event));
    CHECK(event.kind == DRAXUL_PLUGIN_INPUT_POINTER_MOVE);
    CHECK(event.delta_x == 5.0f);
    CHECK(event.buttons == 1);
    REQUIRE(event_at(6, &event));
    CHECK(event.kind == DRAXUL_PLUGIN_INPUT_WHEEL);
    CHECK(event.delta_y == -2.0f);
    host.shutdown();
    CHECK(fixture_quiesce_count() == 1);
    CHECK(renderer.wait_idle_calls == 1);
#ifdef _WIN32
    FreeLibrary(fixture);
#else
    dlclose(fixture);
#endif
}

TEST_CASE("plugin manager loads and caches a real native module",
    "[plugin][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "valid", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    const auto manager = draxul::PluginManager::discover(bundled, user);
    std::string error;
    const auto first = manager->load("dev.draxul.fixture", error);
    REQUIRE(first);
    REQUIRE(error.empty());
    const auto second = manager->load("dev.draxul.fixture", error);
    CHECK(second == first);
    CHECK(first->api().supported_backends
        == (DRAXUL_PLUGIN_BACKEND_VULKAN
            | DRAXUL_PLUGIN_BACKEND_METAL));
}

TEST_CASE("PluginHost rejects a module unsupported by the active backend",
    "[plugin][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "unsupported", "dev.draxul.fixture",
        DRAXUL_FIXTURE_UNSUPPORTED_PATH);
    const auto manager = draxul::PluginManager::discover(bundled, user);
    draxul::PluginHost host(manager);
    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.client_plugin_id = "dev.draxul.fixture";
    draxul::tests::TestHostCallbacks callbacks;
    CHECK_FALSE(host.initialize(context, callbacks));
    CHECK(host.init_error().find("does not support")
        != std::string::npos);
}

TEST_CASE("plugin manager reports malformed dynamic modules",
    "[plugin][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "missing", "dev.draxul.missing", {},
        "missing-plugin.dll");
    install_plugin(bundled, "symbol", "dev.draxul.no-symbol",
        DRAXUL_FIXTURE_NO_SYMBOL_PATH);
    install_plugin(bundled, "abi", "dev.draxul.fixture",
        DRAXUL_FIXTURE_BAD_ABI_PATH);
    install_plugin(bundled, "identity", "dev.draxul.expected",
        DRAXUL_FIXTURE_WRONG_ID_PATH);
    const auto manager = draxul::PluginManager::discover(bundled, user);
    std::string error;
    CHECK_FALSE(manager->load("dev.draxul.missing", error));
    CHECK(error.find("missing") != std::string::npos);
    error.clear();
    CHECK_FALSE(manager->load("dev.draxul.no-symbol", error));
    CHECK(error.find("draxul_plugin_query") != std::string::npos);
    error.clear();
    CHECK_FALSE(manager->load("dev.draxul.fixture", error));
    CHECK(error.find("ABI") != std::string::npos);
    error.clear();
    CHECK_FALSE(manager->load("dev.draxul.expected", error));
    CHECK(error.find("identity") != std::string::npos);
}

TEST_CASE("user plugin tier overrides bundled plugin and duplicate ids are invalid",
    "[plugin][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "bundled", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    install_plugin(user, "override", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    auto manager = draxul::PluginManager::discover(bundled, user);
    REQUIRE(manager->find("dev.draxul.fixture"));
    CHECK(manager->find("dev.draxul.fixture")->user_installed);

    install_plugin(user, "duplicate", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    manager = draxul::PluginManager::discover(bundled, user);
    REQUIRE(manager->find("dev.draxul.fixture"));
    CHECK(manager->find("dev.draxul.fixture")->error.find("Duplicate")
        != std::string::npos);
}

TEST_CASE("plugin manager prepares a distinct shadow-copied generation",
    "[plugin][reload][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    const auto runtime = temp.root / "runtime";
    install_plugin(bundled, "fixture", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    const auto manager = draxul::PluginManager::discover(
        bundled, user, runtime);
    std::string error;
    const auto first = manager->load("dev.draxul.fixture", error);
    REQUIRE(first);
    const auto candidate = manager->prepare_reload(
        "dev.draxul.fixture", error);
    REQUIRE(candidate);
    CHECK(first->generation() != candidate->generation());
    CHECK(first->manifest().directory != candidate->manifest().directory);
    CHECK(first->manifest().directory.string().find(runtime.string()) == 0);
    CHECK(candidate->manifest().directory.string().find(runtime.string()) == 0);
    CHECK(manager->activate(candidate));
    CHECK(manager->load("dev.draxul.fixture", error) == candidate);
}

TEST_CASE("two UI clients keep shared-server plugin generations local",
    "[plugin][reload][integration][shared-server]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "fixture", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    const auto package_library = bundled / "fixture"
        / "generations" / "build-1"
        / std::filesystem::path(DRAXUL_FIXTURE_VALID_PATH).filename();

    // Each attached UI owns its plugin manager and resolves the same stable
    // server pane launch descriptor into a local host instance.
    const auto manager_a = draxul::PluginManager::discover(
        bundled, user, temp.root / "runtime-a");
    const auto manager_b = draxul::PluginManager::discover(
        bundled, user, temp.root / "runtime-b");
    draxul::HostContext shared_context;
    shared_context.launch_options.kind = draxul::HostKind::Plugin;
    shared_context.launch_options.client_plugin_id = "dev.draxul.fixture";
    shared_context.launch_options.client_plugin_config_json = "{}";
    shared_context.pane_id = "shared-server-plugin-pane";
    shared_context.initial_viewport.pixel_size = { 640, 360 };

    draxul::tests::FakeTermRenderer renderer_a;
    draxul::tests::FakeTermRenderer renderer_b;
    auto context_a = shared_context;
    auto context_b = shared_context;
    context_a.grid_renderer = &renderer_a;
    context_b.grid_renderer = &renderer_b;
    draxul::PluginHost host_a(manager_a, temp.root / "ui-a");
    draxul::PluginHost host_b(manager_b, temp.root / "ui-b");
    draxul::tests::TestHostCallbacks callbacks_a;
    draxul::tests::TestHostCallbacks callbacks_b;
    REQUIRE(host_a.initialize(context_a, callbacks_a));
    REQUIRE(host_b.initialize(context_b, callbacks_b));

    const auto original_a = host_a.loaded_plugin();
    const auto original_b = host_b.loaded_plugin();
    REQUIRE(original_a);
    REQUIRE(original_b);
    CHECK(std::string_view(original_a->api().plugin_version) == "1.0.0");
    CHECK(std::string_view(original_b->api().plugin_version) == "1.0.0");
    CHECK(original_a->manifest().library_path != package_library);
    CHECK(original_b->manifest().library_path != package_library);
    CHECK(original_a->manifest().library_path
        != original_b->manifest().library_path);

    // Publishing a rebuilt image replaces only the package source. Both
    // already-loaded images remain resident at their private staged paths.
    std::filesystem::copy_file(DRAXUL_FIXTURE_REPLACEMENT_PATH,
        package_library, std::filesystem::copy_options::overwrite_existing);
    install_plugin(bundled, "fixture", "dev.draxul.fixture", {},
        package_library.filename().string(), "Fixture", "2.0.0");
    std::string error;
    const auto candidate = host_a.prepare_reload(error);
    INFO(error);
    REQUIRE(candidate);
    CHECK(error.empty());
    CHECK(std::string_view(candidate->api().plugin_version) == "2.0.0");
    CHECK(candidate->manifest().library_path != package_library);
    CHECK(candidate->manifest().library_path
        != original_a->manifest().library_path);
    CHECK(&candidate->api() != &original_a->api());

    std::string warning;
    REQUIRE(host_a.reload(candidate, warning, error));
    CHECK(warning.empty());
    CHECK(error.empty());
    REQUIRE(host_a.loaded_plugin() == candidate);
    CHECK(std::string_view(host_a.loaded_plugin()->api().plugin_version)
        == "2.0.0");
    CHECK(std::string_view(original_a->api().plugin_version) == "1.0.0");

    // Reload is local to UI A. UI B keeps its old resident image and remains
    // interactive against the unchanged shared pane descriptor.
    CHECK(host_b.loaded_plugin() == original_b);
    CHECK(std::string_view(host_b.loaded_plugin()->api().plugin_version)
        == "1.0.0");
    REQUIRE(host_b.dispatch_action("fixture_action"));
    CHECK(host_b.status_text().find("reload=1") != std::string::npos);
    CHECK(context_a.pane_id == context_b.pane_id);
    CHECK(context_a.launch_options.client_plugin_id
        == context_b.launch_options.client_plugin_id);
    CHECK(context_a.launch_options.client_plugin_config_json
        == context_b.launch_options.client_plugin_config_json);

    host_a.shutdown();
    host_b.shutdown();
}

TEST_CASE("plugin discovery follows the atomic publication pointer",
    "[plugin][reload][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "fixture", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH, {}, "Fixture", "0.9.0");
    install_plugin_generation(bundled / "fixture" / "generations", "build-2",
        "dev.draxul.fixture", DRAXUL_FIXTURE_VALID_PATH);
    {
        std::ofstream pointer(bundled / "fixture" / "current.json");
        pointer << R"({"schema_version":1,"generation":"build-2"})";
    }

    const auto manager = draxul::PluginManager::discover(bundled, user,
        temp.root / "runtime");
    const auto* manifest = manager->find("dev.draxul.fixture");
    REQUIRE(manifest);
    CHECK(manifest->version == "1.0.0");
    CHECK(manifest->package_generation == "build-2");
}

TEST_CASE("plugin first load refreshes a pruned publication generation",
    "[plugin][reload][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    const auto package = bundled / "fixture";
    install_plugin_generation(package / "generations", "build-1",
        "dev.draxul.fixture", DRAXUL_FIXTURE_VALID_PATH);
    {
        std::ofstream pointer(package / "current.json");
        pointer << R"({"schema_version":1,"generation":"build-1"})";
    }

    const auto manager = draxul::PluginManager::discover(
        bundled, user, temp.root / "runtime");
    REQUIRE(manager->find("dev.draxul.fixture"));
    CHECK(manager->find("dev.draxul.fixture")->package_generation
        == "build-1");

    install_plugin_generation(package / "generations", "build-2",
        "dev.draxul.fixture", DRAXUL_FIXTURE_VALID_PATH);
    std::filesystem::remove_all(package / "generations" / "build-1");
    {
        std::ofstream pointer(package / "current.json");
        pointer << R"({"schema_version":1,"generation":"build-2"})";
    }

    std::string error;
    REQUIRE(manager->load("dev.draxul.fixture", error));
    CHECK(error.empty());
    REQUIRE(manager->find("dev.draxul.fixture"));
    CHECK(manager->find("dev.draxul.fixture")->package_generation
        == "build-2");
}

TEST_CASE("PluginHost reloads a prepared generation and restores transient state",
    "[plugin][reload][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "fixture", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    const auto manager = draxul::PluginManager::discover(
        bundled, user, temp.root / "runtime");

    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.client_plugin_id = "dev.draxul.fixture";
    context.launch_options.client_plugin_config_json
        = R"({"reload_import":"write-storage"})";
    context.pane_id = "reload-pane";
    context.initial_viewport.pixel_size = { 640, 360 };
    draxul::tests::FakeTermRenderer renderer;
    context.grid_renderer = &renderer;
    draxul::PluginHost host(manager, temp.root / "storage");
    draxul::tests::TestHostCallbacks callbacks;
    REQUIRE(host.initialize(context, callbacks));
    const auto old_plugin = host.loaded_plugin();
    REQUIRE(old_plugin);
#ifdef _WIN32
    HMODULE old_module = LoadLibraryW(
        old_plugin->manifest().library_path.c_str());
    REQUIRE(old_module);
    const auto request_from_quiesced = reinterpret_cast<void (*)()>(
        GetProcAddress(old_module,
            "draxul_fixture_request_tick_from_quiesced"));
#else
    void* old_module = dlopen(old_plugin->manifest().library_path.c_str(),
        RTLD_NOW | RTLD_LOCAL);
    REQUIRE(old_module);
    const auto request_from_quiesced = reinterpret_cast<void (*)()>(
        dlsym(old_module, "draxul_fixture_request_tick_from_quiesced"));
#endif
    REQUIRE(request_from_quiesced);
    REQUIRE(host.dispatch_action("fixture_action"));
    CHECK(host.status_text().find("reload=1") != std::string::npos);

    std::string error;
    const auto candidate = host.prepare_reload(error);
    REQUIRE(candidate);
    std::string warning;
    REQUIRE(host.reload(candidate, warning, error));
    CHECK(warning.empty());
    CHECK(host.status_text().find("reload=1") != std::string::npos);
    CHECK(renderer.wait_idle_calls == 1);
    CHECK(std::filesystem::exists(temp.root / "storage" / "config"
        / "dev.draxul.fixture" / "panes" / "reload-pane"
        / "reload-import.json"));
    const int wakes_before_stale_callback = callbacks.wake_window_calls;
    request_from_quiesced();
    CHECK(callbacks.wake_window_calls == wakes_before_stale_callback);
    host.shutdown();
#ifdef _WIN32
    FreeLibrary(old_module);
#else
    dlclose(old_module);
#endif
}

TEST_CASE("PluginHost treats reload-state failures as fresh-state warnings",
    "[plugin][reload][integration]")
{
    struct Scenario
    {
        const char* config;
        const char* warning;
    };
    for (const auto& scenario : {
             Scenario{ R"({"reload_extension":false})", "" },
             Scenario{ R"({"reload_export":"invalid"})", "valid JSON" },
             Scenario{ R"({"reload_export":"oversized"})", "export failed" },
             Scenario{ R"({"reload_import":"reject"})", "incompatible" } })
    {
        DYNAMIC_SECTION(scenario.config)
        {
            TempPlugins temp;
            const auto bundled = temp.root / "bundled";
            const auto user = temp.root / "user";
            install_plugin(bundled, "fixture", "dev.draxul.fixture",
                DRAXUL_FIXTURE_VALID_PATH);
            const auto manager = draxul::PluginManager::discover(
                bundled, user, temp.root / "runtime");
            draxul::HostContext context;
            context.launch_options.kind = draxul::HostKind::Plugin;
            context.launch_options.client_plugin_id = "dev.draxul.fixture";
            context.launch_options.client_plugin_config_json = scenario.config;
            context.pane_id = "reload-state-pane";
            context.initial_viewport.pixel_size = { 640, 360 };
            draxul::tests::FakeTermRenderer renderer;
            context.grid_renderer = &renderer;
            draxul::PluginHost host(manager, temp.root / "storage");
            draxul::tests::TestHostCallbacks callbacks;
            REQUIRE(host.initialize(context, callbacks));
            REQUIRE(host.dispatch_action("fixture_action"));

            std::string error;
            const auto candidate = host.prepare_reload(error);
            REQUIRE(candidate);
            std::string warning;
            REQUIRE(host.reload(candidate, warning, error));
            CHECK(host.status_text() == "ready");
            if (*scenario.warning)
                CHECK(warning.find(scenario.warning) != std::string::npos);
            else
                CHECK(warning.empty());
            host.shutdown();
        }
    }
}

TEST_CASE("PluginHost rolls back a failed candidate and drops storage writes",
    "[plugin][reload][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "fixture", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    const auto manager = draxul::PluginManager::discover(
        bundled, user, temp.root / "runtime");
    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.client_plugin_id = "dev.draxul.fixture";
    context.pane_id = "reload-rollback-pane";
    context.initial_viewport.pixel_size = { 640, 360 };
    draxul::tests::FakeTermRenderer renderer;
    context.grid_renderer = &renderer;
    draxul::PluginHost host(manager, temp.root / "storage");
    draxul::tests::TestHostCallbacks callbacks;
    REQUIRE(host.initialize(context, callbacks));
    const auto working = host.loaded_plugin();

    std::filesystem::copy_file(DRAXUL_FIXTURE_FAIL_CREATE_PATH,
        bundled / "fixture"
            / "generations" / "build-1"
            / std::filesystem::path(DRAXUL_FIXTURE_VALID_PATH).filename(),
        std::filesystem::copy_options::overwrite_existing);
    std::string error;
    const auto candidate = host.prepare_reload(error);
    REQUIRE(candidate);
    std::string warning;
    CHECK_FALSE(host.reload(candidate, warning, error));
    CHECK(error.find("Previous generation restored") != std::string::npos);
    CHECK(host.loaded_plugin() == working);
    CHECK(host.status_text() == "ready");
    CHECK_FALSE(std::filesystem::exists(temp.root / "storage" / "config"
        / "dev.draxul.fixture" / "panes" / "reload-rollback-pane"
        / "candidate-create.json"));
    host.shutdown();
}

TEST_CASE("PluginHost rejects an incompatible candidate before quiescing",
    "[plugin][reload][integration]")
{
    TempPlugins temp;
    const auto bundled = temp.root / "bundled";
    const auto user = temp.root / "user";
    install_plugin(bundled, "fixture", "dev.draxul.fixture",
        DRAXUL_FIXTURE_VALID_PATH);
    const auto manager = draxul::PluginManager::discover(
        bundled, user, temp.root / "runtime");

    draxul::HostContext context;
    context.launch_options.kind = draxul::HostKind::Plugin;
    context.launch_options.client_plugin_id = "dev.draxul.fixture";
    context.pane_id = "reload-preflight-pane";
    context.initial_viewport.pixel_size = { 640, 360 };
    draxul::tests::FakeTermRenderer renderer;
    context.grid_renderer = &renderer;
    draxul::PluginHost host(manager, temp.root / "storage");
    draxul::tests::TestHostCallbacks callbacks;
    REQUIRE(host.initialize(context, callbacks));
    const auto working = host.loaded_plugin();

    std::filesystem::copy_file(DRAXUL_FIXTURE_UNSUPPORTED_PATH,
        bundled / "fixture"
            / "generations" / "build-1"
            / std::filesystem::path(DRAXUL_FIXTURE_VALID_PATH).filename(),
        std::filesystem::copy_options::overwrite_existing);
    std::string error;
    CHECK_FALSE(host.prepare_reload(error));
    CHECK(error.find("renderer backend") != std::string::npos);
    CHECK(host.loaded_plugin() == working);
    CHECK(renderer.wait_idle_calls == 0);
    CHECK(host.status_text() == "ready");
    host.shutdown();
}
