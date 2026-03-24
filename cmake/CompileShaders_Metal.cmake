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

# MegaCity scene shader
set(MEGACITY_METAL_SOURCE ${SHADER_SOURCE_DIR}/megacity_scene.metal)
set(MEGACITY_METAL_AIR    ${SHADER_OUTPUT_DIR}/megacity_scene.air)
set(MEGACITY_METAL_LIB    ${SHADER_OUTPUT_DIR}/megacity_scene.metallib)

add_custom_command(
    OUTPUT ${MEGACITY_METAL_AIR}
    COMMAND xcrun -sdk macosx metal -c ${MEGACITY_METAL_SOURCE} -o ${MEGACITY_METAL_AIR}
    DEPENDS ${MEGACITY_METAL_SOURCE}
    COMMENT "Compiling Metal shader: megacity_scene.metal"
)

add_custom_command(
    OUTPUT ${MEGACITY_METAL_LIB}
    COMMAND xcrun -sdk macosx metallib ${MEGACITY_METAL_AIR} -o ${MEGACITY_METAL_LIB}
    DEPENDS ${MEGACITY_METAL_AIR}
    COMMENT "Linking Metal shader library: megacity_scene.metallib"
)

add_custom_target(compile_megacity_shaders DEPENDS ${MEGACITY_METAL_LIB})
