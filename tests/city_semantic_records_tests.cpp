#include <catch2/catch_test_macros.hpp>

#include <draxul/city_semantic_records.h>
#include <draxul/city_semantic_source.h>

TEST_CASE("semantic city records are available without citydb", "[megacity][citymodel]")
{
    draxul::CityClassRecord row;
    row.name = "Widget";
    row.qualified_name = "Widget";
    row.module_path = "src";
    row.entity_kind = "building";
    row.base_size = 2;
    row.building_functions = 1;
    row.function_sizes = { 5 };

    REQUIRE(row.name == "Widget");
    REQUIRE(row.function_sizes.size() == 1);
}
