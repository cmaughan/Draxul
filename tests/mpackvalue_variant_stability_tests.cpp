
#include <catch2/catch_all.hpp>
#include <draxul/nvim_protocol.h>

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
    MpackValue val = MpackValue::make_nil();
    INFO("nil MpackValue reports Nil type");
    REQUIRE(val.type() == MpackValue::Nil);
}

TEST_CASE("MpackValue bool type() is Bool", "[rpc]")
{
    MpackValue val_true = MpackValue::make_bool(true);
    INFO("bool(true) MpackValue reports Bool type");
    REQUIRE(val_true.type() == MpackValue::Bool);

    MpackValue val_false = MpackValue::make_bool(false);
    INFO("bool(false) MpackValue reports Bool type");
    REQUIRE(val_false.type() == MpackValue::Bool);
}

TEST_CASE("MpackValue int type() is Int", "[rpc]")
{
    MpackValue val = MpackValue::make_int(42);
    INFO("int MpackValue reports Int type");
    REQUIRE(val.type() == MpackValue::Int);

    MpackValue val_neg = MpackValue::make_int(-1);
    INFO("negative int MpackValue reports Int type");
    REQUIRE(val_neg.type() == MpackValue::Int);
}

TEST_CASE("MpackValue uint type() is UInt", "[rpc]")
{
    MpackValue val = MpackValue::make_uint(99);
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
    MpackValue val = MpackValue::make_str("hello");
    INFO("string MpackValue reports String type");
    REQUIRE(val.type() == MpackValue::String);
}

TEST_CASE("MpackValue array type() is Array", "[rpc]")
{
    MpackValue val = MpackValue::make_array({ MpackValue::make_int(1), MpackValue::make_int(2) });
    INFO("array MpackValue reports Array type");
    REQUIRE(val.type() == MpackValue::Array);
}

TEST_CASE("MpackValue map type() is Map", "[rpc]")
{
    MpackValue val = MpackValue::make_map({ { MpackValue::make_str("key"), MpackValue::make_int(1) } });
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
    MpackValue bool_val = MpackValue::make_bool(true);
    MpackValue int_val = MpackValue::make_int(1);
    INFO("Bool and Int types must be distinct");
    REQUIRE(bool_val.type() != int_val.type());
}

TEST_CASE("MpackValue type() does not conflate Int with UInt", "[rpc]")
{
    MpackValue int_val = MpackValue::make_int(1);
    MpackValue uint_val = MpackValue::make_uint(1);
    INFO("Int and UInt types must be distinct");
    REQUIRE(int_val.type() != uint_val.type());
}

TEST_CASE("MpackValue as_int() converts small uint64 to int64", "[rpc]")
{
    MpackValue val = MpackValue::make_uint(42);
    INFO("small uint64 should convert to int64 without error");
    REQUIRE(val.as_int() == 42);
}

TEST_CASE("MpackValue as_int() converts max-representable uint64 to int64", "[rpc]")
{
    uint64_t max_safe = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    MpackValue val = MpackValue::make_uint(max_safe);
    INFO("INT64_MAX stored as uint64 should convert to int64");
    REQUIRE(val.as_int() == std::numeric_limits<int64_t>::max());
}

TEST_CASE("MpackValue as_int() throws for uint64 exceeding INT64_MAX", "[rpc]")
{
    uint64_t too_large = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1;
    MpackValue val = MpackValue::make_uint(too_large);
    INFO("uint64 > INT64_MAX must throw std::range_error, not silently wrap to negative");
    REQUIRE_THROWS_AS(val.as_int(), std::range_error);
}

TEST_CASE("MpackValue as_int() throws for UINT64_MAX", "[rpc]")
{
    MpackValue val = MpackValue::make_uint(std::numeric_limits<uint64_t>::max());
    INFO("UINT64_MAX must throw std::range_error");
    REQUIRE_THROWS_AS(val.as_int(), std::range_error);
}
