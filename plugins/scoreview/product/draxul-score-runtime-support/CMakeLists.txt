add_library(draxul-score-runtime-support STATIC
    src/log.cpp
    src/sdl_imgui_input.cpp
)

target_include_directories(draxul-score-runtime-support PUBLIC include)
target_link_libraries(draxul-score-runtime-support PUBLIC
    glm::glm
    imgui
    SDL3::SDL3
)
