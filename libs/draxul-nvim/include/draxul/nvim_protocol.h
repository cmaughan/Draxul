#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <draxul/result.h>

namespace draxul
{

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
        if (auto* v = std::get_if<uint64_t>(&storage))
        {
            if (*v > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
                throw std::range_error("uint64 value exceeds int64 range in as_int()");
            return static_cast<int64_t>(*v);
        }
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

    static MpackValue make_int(int64_t value)
    {
        MpackValue result;
        result.storage = value;
        return result;
    }

    static MpackValue make_uint(uint64_t value)
    {
        MpackValue result;
        result.storage = value;
        return result;
    }

    static MpackValue make_str(std::string value)
    {
        MpackValue result;
        result.storage = std::move(value);
        return result;
    }

    static MpackValue make_bool(bool value)
    {
        MpackValue result;
        result.storage = value;
        return result;
    }

    static MpackValue make_array(ArrayStorage value)
    {
        MpackValue result;
        result.storage = std::move(value);
        return result;
    }

    static MpackValue make_map(MapStorage value)
    {
        MpackValue result;
        result.storage = std::move(value);
        return result;
    }

    static MpackValue make_nil()
    {
        return {};
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

// RPC requests return Result<MpackValue, Error>. Two failure modes are
// distinguished by Error::kind: transport failures and Neovim RPC failures.
using RpcResult = Result<MpackValue, Error>;

// Protocol-facing request/notification seam. Implementations may use a local
// child process, a socket, or an in-memory fake without changing UI/input code.
class IRpcChannel
{
public:
    virtual ~IRpcChannel() = default;
    virtual RpcResult request(const std::string& method, const std::vector<MpackValue>& params) = 0;
    virtual void notify(const std::string& method, const std::vector<MpackValue>& params) = 0;
};

} // namespace draxul
