
#include <catch2/catch_all.hpp>
#include <draxul/mpack_codec.h>

using namespace draxul;

namespace
{

const MpackValue::MapStorage& as_map(const MpackValue& value)
{
    return value.as_map();
}

} // namespace

TEST_CASE("mpack codec round-trips nested values", "[rpc]")
{
    MpackValue original = NvimRpc::make_map({
        { NvimRpc::make_str("ok"), NvimRpc::make_bool(true) },
        { NvimRpc::make_str("count"), NvimRpc::make_int(42) },
        { NvimRpc::make_str("items"), NvimRpc::make_array({
                                          NvimRpc::make_nil(),
                                          NvimRpc::make_str("hello"),
                                          NvimRpc::make_uint(7),
                                      }) },
    });

    std::vector<char> encoded;
    INFO("value encodes successfully");
    REQUIRE(encode_mpack_value(original, encoded));

    MpackValue decoded;
    size_t consumed = 0;
    INFO("value decodes successfully");
    REQUIRE(decode_mpack_value(
        { reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size() }, decoded, &consumed));
    INFO("decoder reports full consumption");
    REQUIRE(consumed == encoded.size());
    INFO("decoded value keeps the map type");
    REQUIRE(decoded.type() == MpackValue::Map);
    INFO("decoded map keeps all entries");
    REQUIRE(static_cast<int>(as_map(decoded).size()) == 3);
    INFO("first map key survives");
    REQUIRE(as_map(decoded)[0].first.as_str() == std::string("ok"));
    INFO("bool payload survives");
    REQUIRE(as_map(decoded)[0].second.as_bool() == true);
    INFO("int payload survives");
    REQUIRE(as_map(decoded)[1].second.as_int() == static_cast<int64_t>(42));
    INFO("nested array survives");
    REQUIRE(as_map(decoded)[2].second.as_array()[1].as_str() == std::string("hello"));
}

TEST_CASE("rpc request encoding produces the expected msgpack array shape", "[rpc]")
{
    std::vector<char> encoded;
    INFO("request encodes successfully");
    REQUIRE(encode_rpc_request(99, "nvim_ui_attach", {
                                                         NvimRpc::make_int(80),
                                                         NvimRpc::make_int(24),
                                                     },
        encoded));

    MpackValue decoded;
    INFO("request decodes successfully");
    REQUIRE(decode_mpack_value({ reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size() }, decoded));
    INFO("request frame is an array");
    REQUIRE(decoded.type() == MpackValue::Array);
    INFO("request frame has 4 fields");
    REQUIRE(static_cast<int>(decoded.as_array().size()) == 4);
    INFO("request frame type is 0");
    REQUIRE(decoded.as_array()[0].as_int() == static_cast<int64_t>(0));
    INFO("request msgid is preserved");
    REQUIRE(decoded.as_array()[1].as_int() == static_cast<int64_t>(99));
    INFO("request method survives");
    REQUIRE(decoded.as_array()[2].as_str() == std::string("nvim_ui_attach"));
    INFO("request params survive");
    REQUIRE(decoded.as_array()[3].as_array()[0].as_int() == static_cast<int64_t>(80));
}

TEST_CASE("mpack decoder decodes ext values as MpackValue::Ext", "[rpc]")
{
    // fixext1: 0xd4 <type_byte> <data_byte>
    const uint8_t raw[] = { 0xd4, 0x05, 0x2a };
    MpackValue decoded;
    INFO("ext value decodes successfully");
    REQUIRE(decode_mpack_value({ raw, sizeof(raw) }, decoded));
    INFO("ext value has Ext type");
    REQUIRE(decoded.type() == MpackValue::Ext);
    INFO("ext type byte is preserved");
    REQUIRE((int)decoded.as_ext().type == 5);
    INFO("ext data is preserved");
    REQUIRE(decoded.as_ext().data == static_cast<int64_t>(42));
}

TEST_CASE("mpack decoder rejects truncated frames", "[rpc]")
{
    std::vector<char> encoded;
    INFO("fixture encodes successfully");
    REQUIRE(encode_mpack_value(NvimRpc::make_array({
                                   NvimRpc::make_str("abc"),
                                   NvimRpc::make_int(5),
                               }),
        encoded));
    encoded.pop_back();

    MpackValue decoded;
    INFO("truncated frame is rejected");
    REQUIRE(!decode_mpack_value({ reinterpret_cast<const uint8_t*>(encoded.data()), encoded.size() }, decoded));
}
