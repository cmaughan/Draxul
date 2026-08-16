# Find glslc from Vulkan SDK
find_program(GLSLC_EXECUTABLE glslc HINTS $ENV{VULKAN_SDK}/Bin)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Install the Vulkan SDK.")
endif()

set(SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS
    ${SHADER_SOURCE_DIR}/*.vert
    ${SHADER_SOURCE_DIR}/*.frag
    # The NanoVG shaders live beside their backend (single home, shared with
    # product plugins through the staged draxul-nanovg tree).
    ${CMAKE_SOURCE_DIR}/libs/draxul-nanovg/shaders/*.vert
    ${CMAKE_SOURCE_DIR}/libs/draxul-nanovg/shaders/*.frag
)

file(GLOB SHADER_GLSL_INCLUDES CONFIGURE_DEPENDS ${SHADER_SOURCE_DIR}/*.glsl ${SHADER_SOURCE_DIR}/*.h)

set(SHADER_OUTPUTS "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${GLSLC_EXECUTABLE} -I ${SHADER_SOURCE_DIR} ${SHADER} -o ${SPIRV_OUTPUT}
        DEPENDS ${SHADER} ${SHADER_GLSL_INCLUDES}
        COMMENT "Compiling shader: ${SHADER_NAME}"
    )
    list(APPEND SHADER_OUTPUTS ${SPIRV_OUTPUT})
endforeach()

add_custom_target(compile_shaders DEPENDS ${SHADER_OUTPUTS})
