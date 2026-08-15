# Compile Metal shaders to .metallib
set(SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

set(METAL_SOURCE ${SHADER_SOURCE_DIR}/grid.metal)
set(METAL_AIR ${SHADER_OUTPUT_DIR}/grid.air)
set(METAL_LIB ${SHADER_OUTPUT_DIR}/grid.metallib)

# Compile .metal -> .air
add_custom_command(
    OUTPUT ${METAL_AIR}
    COMMAND xcrun -sdk macosx metal -c ${METAL_SOURCE} -I ${SHADER_SOURCE_DIR} -o ${METAL_AIR}
    DEPENDS ${METAL_SOURCE} ${SHADER_SOURCE_DIR}/decoration_constants_shared.h
    COMMENT "Compiling Metal shader: grid.metal"
)

# Link .air -> .metallib
add_custom_command(
    OUTPUT ${METAL_LIB}
    COMMAND xcrun -sdk macosx metallib ${METAL_AIR} -o ${METAL_LIB}
    DEPENDS ${METAL_AIR}
    COMMENT "Linking Metal shader library: grid.metallib"
)

add_custom_target(compile_metal_shaders DEPENDS ${METAL_LIB})

# GUI shader
set(GUI_METAL_SOURCE ${SHADER_SOURCE_DIR}/gui.metal)
set(GUI_METAL_AIR    ${SHADER_OUTPUT_DIR}/gui.air)
set(GUI_METAL_LIB    ${SHADER_OUTPUT_DIR}/gui.metallib)

add_custom_command(
    OUTPUT ${GUI_METAL_AIR}
    COMMAND xcrun -sdk macosx metal -c ${GUI_METAL_SOURCE} -o ${GUI_METAL_AIR}
    DEPENDS ${GUI_METAL_SOURCE}
    COMMENT "Compiling Metal shader: gui.metal"
)

add_custom_command(
    OUTPUT ${GUI_METAL_LIB}
    COMMAND xcrun -sdk macosx metallib ${GUI_METAL_AIR} -o ${GUI_METAL_LIB}
    DEPENDS ${GUI_METAL_AIR}
    COMMENT "Linking Metal shader library: gui.metallib"
)

add_custom_target(compile_gui_shaders DEPENDS ${GUI_METAL_LIB})

# Markdown shader
set(MARKDOWN_METAL_SOURCE ${SHADER_SOURCE_DIR}/markdown.metal)
set(MARKDOWN_METAL_AIR    ${SHADER_OUTPUT_DIR}/markdown.air)
set(MARKDOWN_METAL_LIB    ${SHADER_OUTPUT_DIR}/markdown.metallib)

add_custom_command(
    OUTPUT ${MARKDOWN_METAL_AIR}
    COMMAND xcrun -sdk macosx metal -c ${MARKDOWN_METAL_SOURCE} -I ${SHADER_SOURCE_DIR} -o ${MARKDOWN_METAL_AIR}
    DEPENDS
        ${MARKDOWN_METAL_SOURCE}
        ${SHADER_SOURCE_DIR}/decoration_constants_shared.h
        ${SHADER_SOURCE_DIR}/quad_offsets_shared.h
    COMMENT "Compiling Metal shader: markdown.metal"
)

add_custom_command(
    OUTPUT ${MARKDOWN_METAL_LIB}
    COMMAND xcrun -sdk macosx metallib ${MARKDOWN_METAL_AIR} -o ${MARKDOWN_METAL_LIB}
    DEPENDS ${MARKDOWN_METAL_AIR}
    COMMENT "Linking Metal shader library: markdown.metallib"
)

add_custom_target(compile_markdown_shaders DEPENDS ${MARKDOWN_METAL_LIB})
