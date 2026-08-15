# ScoreView audio DSP (kanban 22 item 4): the metronome/tone synths, the
# soundfont voice, and the acoustic note listener. All pure and
# offline-testable — SDL stays at the host layer; this library isolates the
# tinysoundfont and kissfft dependencies. Depends on draxul-score-input for
# the player-input seam the listener emits into (PlayerNoteEvent).
add_library(draxul-score-audio STATIC
    src/metronome_synth.cpp
    src/note_listener.cpp
    src/soundfont_synth.cpp
)

target_include_directories(draxul-score-audio PUBLIC
    include
)

target_link_libraries(draxul-score-audio
    PUBLIC draxul-score-input
    PRIVATE draxul-score-runtime-support kissfft::kissfft tinysoundfont
)
