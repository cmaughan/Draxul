#include <catch2/catch_test_macros.hpp>

#include "semantic_city_layout.h"

TEST_CASE("city presentation records are local to megacity", "[megacity]")
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
