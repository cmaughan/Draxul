add_library(draxul-geometry STATIC
    src/building_generator.cpp
    src/primitive_meshes.cpp
    src/roof_sign_generator.cpp
    src/tree_generator.cpp
)

target_include_directories(draxul-geometry PUBLIC
    include
)

target_link_libraries(draxul-geometry PUBLIC
    glm
    draxul-types
)
