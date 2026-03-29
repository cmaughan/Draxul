cmake_minimum_required(VERSION 3.25)

add_library(draxul-citydb STATIC
    src/citydb.cpp
)

target_include_directories(draxul-citydb
    PUBLIC include
    PRIVATE src
)

target_link_libraries(draxul-citydb PUBLIC
    draxul-treesitter
    draxul-types
    sqlite3_lib
)
