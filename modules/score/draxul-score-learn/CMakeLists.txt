# The ScoreView learning core (plans/scoreview-stream.md S0/S1/S3): the
# player model, piece analysis, source slicing, the composer seam and its
# adaptive-stream implementation, the stream program, the measure writer, and
# progress-file IO. A leaf library: it reaches no transport, layout, audio,
# input, or GPU code — composer purity is enforced by the linker, not by
# convention. Headers keep the draxul/scoreview include prefix and namespace;
# the boundary is a link-time contract, not a renaming exercise.
add_library(draxul-score-learn STATIC
    src/measure_xml.cpp
    src/piece_analysis.cpp
    src/player_model.cpp
    src/progress_store.cpp
    src/source_slicer.cpp
    src/stream_composer.cpp
    src/stream_program.cpp
)

target_include_directories(draxul-score-learn PUBLIC
    include
)

target_link_libraries(draxul-score-learn
    PRIVATE tinyxml2::tinyxml2 nlohmann_json::nlohmann_json
)
