
#include <catch2/catch_all.hpp>
#include <draxul/nvim_rpc.h>

using namespace draxul;

// Static assertions pin each variant alternative index to its expected C++ type.
// If the Storage declaration order is ever changed, these will fail at compile time.
static_assert(std::is_same_v<std::variant_alternative_t<0, MpackValue::Storage>, std::monostate>,
    "MpackValue::Storage index 0 must be std::monostate (Nil)");
static_assert(std::is_same_v<std::variant_alternative_t<1, MpackValue::Storage>, bool>,
    "MpackValue::Storage index 1 must be bool");
static_assert(std::is_same_v<std::variant_alternative_t<2, MpackValue::Storage>, int64_t>,
    "MpackValue::Storage index 2 must be int64_t");
static_assert(std::is_same_v<std::variant_alternative_t<3, MpackValue::Storage>, uint64_t>,
    "MpackValue::Storage index 3 must be uint64_t");
static_assert(std::is_same_v<std::variant_alternative_t<4, MpackValue::Storage>, double>,
    "MpackValue::Storage index 4 must be double");
static_assert(std::is_same_v<std::variant_alternative_t<5, MpackValue::Storage>, std::string>,
    "MpackValue::Storage index 5 must be std::string");
static_assert(std::is_same_v<std::variant_alternative_t<6, MpackValue::Storage>, MpackValue::ArrayStorage>,
    "MpackValue::Storage index 6 must be ArrayStorage");
static_assert(std::is_same_v<std::variant_alternative_t<7, MpackValue::Storage>, MpackValue::MapStorage>,
    "MpackValue::Storage index 7 must be MapStorage");
static_assert(std::is_same_v<std::variant_alternative_t<8, MpackValue::Storage>, MpackValue::ExtValue>,
    "MpackValue::Storage index 8 must be ExtValue");

TEST_CASE("MpackValue default-constructed type() is Nil", "[rpc]")
{
    MpackValue val;
    INFO("default MpackValue reports Nil type");
    REQUIRE(val.type() == MpackValue::Nil);
}

TEST_CASE("MpackValue nil type() is Nil", "[rpc]")
{
    MpackValue val = NvimRpc::make_nil();
    INFO("nil MpackValue reports Nil type");
    REQUIRE(val.type() == MpackValue::Nil);
}

TEST_CASE("MpackValue bool type() is Bool", "[rpc]")
{
    MpackValue val_true = NvimRpc::make_bool(true);
    INFO("bool(true) MpackValue reports Bool type");
    REQUIRE(val_true.type() == MpackValue::Bool);

    MpackValue val_false = NvimRpc::make_bool(false);
    INFO("bool(false) MpackValue reports Bool type");
    REQUIRE(val_false.type() == MpackValue::Bool);
}

TEST_CASE("MpackValue int type() is Int", "[rpc]")
{
    MpackValue val = NvimRpc::make_int(42);
    INFO("int MpackValue reports Int type");
    REQUIRE(val.type() == MpackValue::Int);

    MpackValue val_neg = NvimRpc::make_int(-1);
    INFO("negative int MpackValue reports Int type");
    REQUIRE(val_neg.type() == MpackValue::Int);
}

TEST_CASE("MpackValue uint type() is UInt", "[rpc]")
{
    MpackValue val = NvimRpc::make_uint(99);
    INFO("uint MpackValue reports UInt type");
    REQUIRE(val.type() == MpackValue::UInt);
}

TEST_CASE("MpackValue float type() is Float", "[rpc]")
{
    MpackValue val;
    val.storage = 3.14;
    INFO("double MpackValue reports Float type");
    REQUIRE(val.type() == MpackValue::Float);
}

TEST_CASE("MpackValue string type() is String", "[rpc]")
{
    MpackValue val = NvimRpc::make_str("hello");
    INFO("string MpackValue reports String type");
    REQUIRE(val.type() == MpackValue::String);
}

TEST_CASE("MpackValue array type() is Array", "[rpc]")
{
    MpackValue val = NvimRpc::make_array({ NvimRpc::make_int(1), NvimRpc::make_int(2) });
    INFO("array MpackValue reports Array type");
    REQUIRE(val.type() == MpackValue::Array);
}

TEST_CASE("MpackValue map type() is Map", "[rpc]")
{
    MpackValue val = NvimRpc::make_map({ { NvimRpc::make_str("key"), NvimRpc::make_int(1) } });
    INFO("map MpackValue reports Map type");
    REQUIRE(val.type() == MpackValue::Map);
}

TEST_CASE("MpackValue ext type() is Ext", "[rpc]")
{
    MpackValue val;
    val.storage = MpackValue::ExtValue{ 5, 42 };
    INFO("ext MpackValue reports Ext type");
    REQUIRE(val.type() == MpackValue::Ext);
}

TEST_CASE("MpackValue type() does not conflate Bool with Int", "[rpc]")
{
    MpackValue bool_val = NvimRpc::make_bool(true);
    MpackValue int_val = NvimRpc::make_int(1);
    INFO("Bool and Int types must be distinct");
    REQUIRE(bool_val.type() != int_val.type());
}

TEST_CASE("MpackValue type() does not conflate Int with UInt", "[rpc]")
{
    MpackValue int_val = NvimRpc::make_int(1);
    MpackValue uint_val = NvimRpc::make_uint(1);
    INFO("Int and UInt types must be distinct");
    REQUIRE(int_val.type() != uint_val.type());
}
