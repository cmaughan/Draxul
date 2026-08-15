# ScoreView (plans/scoreview.md): draxul-score-learn is the GPU-free learning
# core (player model, analysis, slicer, composer); draxul-scoreview is the
# layout/transport pipeline (Verovio engine wrapper, SVG interpreter, flow
# judge, audio synths, device inputs); draxul-scoreview-runtime is the
# plugin-owned orchestration and NanoVG presentation layer.
add_library(draxul-scoreview STATIC
    src/bot_player_input.cpp
    src/engraved_window.cpp
    src/flow_controller.cpp
    src/keyboard_layout.cpp
    src/score_highlight.cpp
    src/score_timemap.cpp
    src/svg_score_interpreter.cpp
    src/verdict_archive.cpp
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

# draxul-score-learn and draxul-score-input are PUBLIC: flow_controller.h
# includes note_outcomes.h and player_input.h; the host layer reaches the
# composer/program/model and device-input headers through us. score-audio is
# PUBLIC for the synth/listener headers the host's audio rig uses.
target_link_libraries(draxul-scoreview
    PUBLIC glm::glm draxul-score-learn draxul-score-input draxul-score-audio
    PRIVATE draxul-score-runtime-support verovio tinyxml2::tinyxml2
        nlohmann_json::nlohmann_json
)

add_library(draxul-scoreview-runtime STATIC
    src/analysis_overlay.cpp
    src/keyboard_render_nvg.cpp
    src/mic_player_input.cpp
    src/score_audio_controller.cpp
    src/score_device_lease.cpp
    src/score_presentation.cpp
    src/score_session_controller.cpp
    src/score_stream_controller.cpp
    src/player_input_rig.cpp
    src/score_runtime_inspector.cpp
    src/score_runtime.cpp
    src/score_render_nvg.cpp
)

target_include_directories(draxul-scoreview-runtime PUBLIC
    include
)

# SDL stays at the runtime layer: the core listener above is pure DSP and
# offline-testable; only the microphone front-end touches a device.
# DRAXUL_SCOREVIEW_SDL_TARGET is headers-only in bundled macOS builds, where
# the plugin module resolves SDL symbols from the host executable at load.
target_link_libraries(draxul-scoreview-runtime PUBLIC
    draxul-scoreview
    draxul-notation
    draxul-score-canvas
    draxul-score-runtime-support
    PRIVATE ${DRAXUL_SCOREVIEW_SDL_TARGET} imgui
)

# Microphone permission pre-flight: TCC via AVFoundation on macOS (SDL must
# never be the one to trigger the consent dialog — see mic_permission.h).
if(APPLE)
    target_sources(draxul-scoreview-runtime PRIVATE src/mic_permission.mm)
    set_source_files_properties(src/mic_permission.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    target_link_libraries(draxul-scoreview-runtime PRIVATE ${AVFOUNDATION_FRAMEWORK})
else()
    target_sources(draxul-scoreview-runtime PRIVATE src/mic_permission_stub.cpp)
endif()

# Runtime orchestration tests need controller internals, but those controllers are
# not public product API. Keep the exception named and confined to tests.
add_library(draxul-scoreview-runtime-test-internals INTERFACE)
target_include_directories(draxul-scoreview-runtime-test-internals INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(draxul-scoreview-runtime-test-internals INTERFACE
    draxul-scoreview-runtime
)
