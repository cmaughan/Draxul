if(APPLE)
    set(DRAXUL_HTTP_PLATFORM_SOURCE src/http_client_mac.mm)
elseif(WIN32)
    set(DRAXUL_HTTP_PLATFORM_SOURCE src/http_client_win.cpp)
else()
    set(DRAXUL_HTTP_PLATFORM_SOURCE src/http_client_stub.cpp)
endif()

add_library(draxul-http STATIC
    src/http_client.cpp
    ${DRAXUL_HTTP_PLATFORM_SOURCE}
)

target_include_directories(draxul-http PUBLIC include)

if(APPLE)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    target_link_libraries(draxul-http PRIVATE ${FOUNDATION_FRAMEWORK})
elseif(WIN32)
    target_link_libraries(draxul-http PRIVATE winhttp)
endif()
