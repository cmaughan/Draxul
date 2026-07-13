# The ScoreView Manifesto — an endless runner for learning the piano

*Stored 2026-07-11. This is the end goal of the score module. The
[scoreview.md](scoreview.md) phases are the road; this is the destination.
Piano only, for now.*

## The one-liner

**The best tool ever made for learning to play a piece on the piano** — a
game you play at your instrument that teaches you the piece you chose, in the
shortest scientifically-defensible time, without you ever feeling like you're
practicing.

## The vision

You give it a goal: a real piece of music — the Grieg waltz, an exam piece,
anything. The software shows a **single flowing row** of notation — the
current place in the music, at least two bars visible ahead (configurable
later). Not pages. A stream.

The stream begins, and it **keeps rolling — it never stops to wait**
(vision sharpened 2026-07-13: this is Guitar Hero for piano). You play
along on your real piano; it hears you through the microphone and judges
each note — right pitch, near-enough timing — as the playhead crosses it.
Correct notes light up as you land them; the tempo you are at is
maintained, easing *down* when you're missing enough (pitch or timing)
and creeping back *up* toward the target as you land them. You may be
fluffing everything and the music still carries you forward — and the
stream will manufacture material that lets you catch up. A running score
ticks along — tempo and accuracy, the feel of a game, not a grade.

The stream **never stops and never ends**. Wrong notes are shown — you see
exactly what happened — but the music moves on. No scolding, no rewinding, no
"try that bar again" interruptions. The parts you fumbled quietly come back
around later, the way a spaced repetition system resurfaces the cards you
miss. Never boring; never punishing.

And here is the heart of it: **the stream is not just the piece.** The
software dynamically chooses what to put in front of you, always *related* to
the goal:

- left hand alone; right hand alone
- simplified reductions of the hard passages
- the scales and arpeggios the piece actually lives in
- **manufactured motifs and fragments** — invented exercises engineered from
  the piece's own material to teach the exact skills the piece demands, and
  to build genuine sight reading along the way
- and, as fluency grows, longer and truer runs of the real thing

As you approach fluency, the stream converges on the final piece. The endless
runner *becomes* the performance. Later, it fades the notation to take you
from reading to memory.

## Tempo is the throttle

- It starts **slow**. Playable-by-anyone slow.
- It adapts continuously: doing well speeds it up, struggling slows it down —
  including adapting to the tempo *you* actually play, averaged over recent
  playing, so the system works at the student's pace rather than dragging the
  student at the system's pace.
- It is capped at roughly **10–20% above the piece's marking** — fast enough
  to build a safety margin for performance day, never a speed circus.
- Tempo (with accuracy) is the score. Getting faster *because you earned it*
  is the game's core reward loop.

## Hearing the player

The system listens to the **real piano through the microphone**. No cables,
no special keyboard required — the instrument you'll perform on is the
instrument you practice on. We will find the best, most robust way to
understand what is being played (polyphonic transcription is a researched
field — Onsets & Frames, Basic Pitch, and successors) and — crucially — we
exploit that we are not transcribing blind: **we know what the music expects
next.** Matching against a known target is a far easier and more reliable
problem than open transcription, and it's what lets the judgment feel
instant and fair. (A MIDI input path may exist as a development-time ground
truth and a power-user option, but the microphone is the product.)

## The science it stands on

The design is not a vibe; each pillar is load-bearing:

- **Spaced repetition** (Ebbinghaus; Leitner; SuperMemo lineage) — weak
  fragments return on a forgetting-curve schedule instead of being drilled to
  death in the moment.
- **Deliberate practice at the edge of ability** (Ericsson) — the adaptive
  stream keeps difficulty in the narrow band where learning is fastest:
  never comfortable, never overwhelmed.
- **The flow channel** (Csikszentmihalyi) — challenge tracks skill
  continuously; that is the "forgot I was practicing" state, and it is the
  product's defining feeling.
- **Desirable difficulties & interleaving** (Bjork) — hands-separate,
  transposed motifs, related scales, and fragment variation beat massed
  repetition of the same bars for retention.
- **Chunking** (Chase & Simon; Gobet) — motifs and fragments are chosen to
  build the pattern vocabulary the piece is made of, which is also exactly
  what sight reading *is*.
- **Immediate, low-stakes feedback** — notes light up, the stream continues;
  errors are information, not events.

## Principles (the non-negotiables)

1. **Never stop.** The stream flows through mistakes. Momentum is sacred.
2. **Never bore.** If the player is cruising, escalate; if they're drowning,
   simplify — within seconds, not sessions.
3. **Invisible learning.** The player experiences playing/enjoying/gaming.
   The pedagogy is in the selection engine, not in lectures.
4. **Always real notation.** The game *is* engraved music. Sight reading is
   not a side effect; it is the medium.
5. **The piece is the boss level.** Every generated fragment exists because
   it moves the player measurably toward the goal piece.
6. **Meet the player's pace.** The software adapts to the human, then gently
   leads; not the reverse.
7. **Accurate teaching.** Exam pieces must be learned *correctly* — right
   notes, right rhythms, right tempo trajectory. The fun is not allowed to
   compromise the truth of the music.

## What this is not

- Not a score editor that grew a game mode (editing serves the goal, not
  vice versa).
- Not a metronome drill or a stop-and-scold tutor.
- Not a falling-blocks piano game divorced from real notation.
- Not a transcription showcase — hearing exists to serve judgment of the
  known target.

## How the codebase already points here

The phases built so far are this manifesto's foundations, not a detour:

- **Semantic model with exact rational onsets and stable note ids** — the
  substrate for fragment generation, hand splitting, simplification, and
  per-note judgment.
- **Draw list carrying a source element id on every op** — per-note light-up
  is a color lookup away.
- **Verovio behind `ILayoutEngine`** — `breaks: none` gives the single-row
  flowing view; timemaps give score-time ↔ note-id mapping for the runner.
- **0.16s full-piece relayout** — regenerating the stream's notation on the
  fly (the selection engine's output is just MusicXML) is already cheap.
- **NanoVG real-time replay** — recoloring and scrolling at animation rates
  is the pipeline's native mode.

## The measure of success

A player picks an exam piece they cannot play. Weeks later they perform it —
accurately, at tempo, with margin — and when asked about the practice, they
talk about the game: the streaks, the tempo climbing, the score. They never
noticed the curriculum. **That** is the product.
