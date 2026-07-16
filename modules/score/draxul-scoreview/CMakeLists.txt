# ScoreView (plans/scoreview.md): draxul-score-learn is the GPU-free learning
# core (player model, analysis, slicer, composer); draxul-scoreview is the
# layout/transport pipeline (Verovio engine wrapper, SVG interpreter, flow
# judge, audio synths, device inputs); draxul-scoreview-host is the IHost +
# NanoVG presentation layer.
add_library(draxul-scoreview STATIC
    src/bot_player_input.cpp
    src/engraved_window.cpp
    src/flow_controller.cpp
    src/keyboard_layout.cpp
    src/metronome_synth.cpp
    src/midi_player_input.cpp
    src/note_listener.cpp
    src/score_highlight.cpp
    src/score_timemap.cpp
    src/soundfont_synth.cpp
    src/svg_score_interpreter.cpp
    src/verovio_layout_engine.cpp
    src/window_engraver.cpp
)

target_include_directories(draxul-scoreview PUBLIC
    include
)

# Verovio's library target does not export interface include dirs; mirror the
# include layout its own build uses. The NO_*_SUPPORT definitions must match
# the ones its build was configured with, since vrv headers branch on them.
target_include_directories(draxul-scoreview SYSTEM PRIVATE
    ${verovio_SOURCE_DIR}/include
    ${verovio_SOURCE_DIR}/include/vrv
    ${verovio_SOURCE_DIR}/include/json
    ${verovio_SOURCE_DIR}/include/midi
    ${verovio_SOURCE_DIR}/include/pugi
    ${verovio_SOURCE_DIR}/include/tuning-library
    ${verovio_SOURCE_DIR}/include/zip
    ${verovio_SOURCE_DIR}/libmei/dist
    ${verovio_SOURCE_DIR}/libmei/addons
)
target_compile_definitions(draxul-scoreview PRIVATE
    NO_HUMDRUM_SUPPORT
    NO_ABC_SUPPORT
    NO_PAE_SUPPORT
    NO_GABC_SUPPORT
    NO_DARMS_SUPPORT
)

# draxul-score-learn is PUBLIC: flow_controller.h includes note_outcomes.h,
# and the host layer reaches the composer/program/model headers through us.
target_link_libraries(draxul-scoreview
    PUBLIC draxul-types draxul-score-learn
    PRIVATE verovio tinyxml2::tinyxml2 nlohmann_json::nlohmann_json kissfft::kissfft
            rtmidi tinysoundfont
)
# RtMidi's in-build target does not export its header directory.
target_include_directories(draxul-scoreview SYSTEM PRIVATE ${rtmidi_SOURCE_DIR})

add_library(draxul-scoreview-host STATIC
    src/keyboard_render_nvg.cpp
    src/mic_player_input.cpp
    src/score_host.cpp
    src/score_render_nvg.cpp
)

target_include_directories(draxul-scoreview-host PUBLIC
    include
)

# SDL stays at the host layer: the core listener above is pure DSP and
# offline-testable; only the microphone front-end touches a device.
target_link_libraries(draxul-scoreview-host PUBLIC
    draxul-scoreview
    draxul-notation
    draxul-host
    draxul-renderer
    draxul-nanovg
    PRIVATE SDL3::SDL3 draxul-config imgui draxul-ui
)

# Microphone permission pre-flight: TCC via AVFoundation on macOS (SDL must
# never be the one to trigger the consent dialog — see mic_permission.h).
if(APPLE)
    target_sources(draxul-scoreview-host PRIVATE src/mic_permission.mm)
    set_source_files_properties(src/mic_permission.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    target_link_libraries(draxul-scoreview-host PRIVATE ${AVFOUNDATION_FRAMEWORK})
else()
    target_sources(draxul-scoreview-host PRIVATE src/mic_permission_stub.cpp)
endif()
