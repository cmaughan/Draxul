#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace draxul
{

// --- NvimProcess ---

class NvimProcess
{
public:
    NvimProcess();
    ~NvimProcess();

    bool spawn(const std::string& nvim_path = "nvim", const std::vector<std::string>& extra_args = {}, const std::string& working_dir = {});
    void shutdown();

    bool write(const uint8_t* data, size_t len) const;
    int read(uint8_t* buffer, size_t max_len) const;

    bool is_running() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// --- RPC types ---

struct MpackValue
{
    enum Type
    {
        Nil,
        Bool,
        Int,
        UInt,
        Float,
        String,
        Array,
        Map,
        Ext
    };

    struct ExtValue
    {
        int8_t type = 0;
        int64_t data = 0;
    };

    using ArrayStorage = std::vector<MpackValue>;
    using MapStorage = std::vector<std::pair<MpackValue, MpackValue>>;
    using Storage = std::variant<std::monostate, bool, int64_t, uint64_t, double, std::string, ArrayStorage, MapStorage, ExtValue>;

    Storage storage = std::monostate{};

    Type type() const
    {
        // Compile-time map: variant index → MpackType enum.
        // Matches the declaration order of the variant alternatives above.
        // static_assert below ensures this stays in sync if alternatives change.
        using StorageType = decltype(storage);
        constexpr std::array<Type, std::variant_size_v<StorageType>> kTypeMap = {
            Nil, // std::monostate
            Bool, // bool
            Int, // int64_t
            UInt, // uint64_t
            Float, // double
            String, // std::string
            Array, // ArrayStorage
            Map, // MapStorage
            Ext, // ExtValue
        };
        static_assert(kTypeMap.size() == std::variant_size_v<StorageType>,
            "kTypeMap must have one entry per variant alternative");
        if (storage.valueless_by_exception()) // NOSONAR cpp:S836 — storage always initialized via default member init
            return Nil;
        return kTypeMap[storage.index()];
    }

    bool is_nil() const
    {
        return std::holds_alternative<std::monostate>(storage);
    }

    int64_t as_int() const
    {
        if (auto value = std::get_if<int64_t>(&storage))
            return *value;
        if (auto value = std::get_if<uint64_t>(&storage))
            return (int64_t)*value;
        throw std::bad_variant_access();
    }

    const std::string& as_str() const
    {
        return std::get<std::string>(storage);
    }

    bool as_bool() const
    {
        return std::get<bool>(storage);
    }

    const ArrayStorage& as_array() const
    {
        return std::get<ArrayStorage>(storage);
    }

    const MapStorage& as_map() const
    {
        return std::get<MapStorage>(storage);
    }

    const ExtValue& as_ext() const
    {
        return std::get<ExtValue>(storage);
    }
};

struct RpcNotification
{
    std::string method;
    std::vector<MpackValue> params;
};

struct RpcResponse
{
    uint32_t msgid;
    MpackValue error;
    MpackValue result;
};

struct RpcResult
{
    MpackValue result;
    MpackValue error;
    bool transport_ok = false;

    bool is_error() const
    {
        return !error.is_nil();
    }

    bool ok() const
    {
        return transport_ok && !is_error();
    }
};

class IRpcChannel
{
public:
    virtual ~IRpcChannel() = default;
    virtual RpcResult request(const std::string& method, const std::vector<MpackValue>& params) = 0;
    virtual void notify(const std::string& method, const std::vector<MpackValue>& params) = 0;
};

// --- NvimRpc ---

class NvimRpc : public IRpcChannel
{
public:
    NvimRpc();
    ~NvimRpc();

    bool initialize(NvimProcess& process);
    void close();
    void shutdown();

    RpcResult request(const std::string& method, const std::vector<MpackValue>& params) override;
    void notify(const std::string& method, const std::vector<MpackValue>& params) override;

    std::vector<RpcNotification> drain_notifications();

    // Returns the number of notifications currently sitting in the queue.
    // Thread-safe; acquires the internal notification mutex.
    size_t notification_queue_depth() const;

    // Returns true if the reader thread detected an unexpected pipe close
    // (i.e. nvim exited without us requesting shutdown).
    bool connection_failed() const;

    static MpackValue make_int(int64_t v);
    static MpackValue make_uint(uint64_t v);
    static MpackValue make_str(const std::string& v);
    static MpackValue make_bool(bool v);
    static MpackValue make_array(std::vector<MpackValue> v);
    static MpackValue make_map(std::vector<std::pair<MpackValue, MpackValue>> v);
    static MpackValue make_nil();

    // Call once from the main thread after initialization is complete.
    // Asserts the ID has not been set before, then records it.
    // After this point NvimRpc::request() will assert it is not called from that thread.
    void set_main_thread_id(std::thread::id id);

    std::function<void()> on_notification_available;
    // Called synchronously from the reader thread when nvim sends an rpcrequest.
    // Must return the result MpackValue; called with write_mutex_ NOT held.
    std::function<MpackValue(const std::string& method, const std::vector<MpackValue>& params)> on_request;

private:
    void reader_thread_func();
    void reply_to_request(uint32_t msgid, const MpackValue& error, const MpackValue& result);
    void dispatch_rpc_message(const MpackValue& msg);
    void dispatch_rpc_response(const std::vector<MpackValue>& msg_array);
    void dispatch_rpc_request(const std::vector<MpackValue>& msg_array);
    void dispatch_rpc_notification(const std::vector<MpackValue>& msg_array);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::thread::id main_thread_id_{};
};

} // namespace draxul
