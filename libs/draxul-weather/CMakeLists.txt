add_library(draxul-weather STATIC
    src/weather_parsing.cpp
    src/weather_service.cpp
)

target_include_directories(draxul-weather PUBLIC include)

target_link_libraries(draxul-weather
    PUBLIC draxul-http
    PRIVATE
        draxul-types
        nlohmann_json::nlohmann_json
)

# White-box provider-decoding tests consume private synchronous helpers. Keep
# this access test-only; production callers use WeatherService.
add_library(draxul-weather-test-internals INTERFACE)
target_include_directories(draxul-weather-test-internals INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(draxul-weather-test-internals INTERFACE draxul-weather)

draxul_configure_internal_targets_in_directory("${CMAKE_CURRENT_SOURCE_DIR}")
