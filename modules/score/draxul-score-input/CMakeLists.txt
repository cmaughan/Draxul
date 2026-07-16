# ScoreView device input (kanban 22 item 4): the player-input seam
# (IPlayerInput/PlayerNoteEvent), the dev keyboard scaffold, and the hardware
# MIDI input. Isolates the RtMidi dependency in one library. The bot input
# stays in draxul-scoreview (it judges against FlowController); the
# microphone front-end stays in draxul-scoreview-host (SDL).
add_library(draxul-score-input STATIC
    src/midi_player_input.cpp
)

target_include_directories(draxul-score-input PUBLIC
    include
)

target_link_libraries(draxul-score-input
    PRIVATE draxul-types rtmidi
)
# RtMidi's in-build target does not export its header directory.
target_include_directories(draxul-score-input SYSTEM PRIVATE ${rtmidi_SOURCE_DIR})
