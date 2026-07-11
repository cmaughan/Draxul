# Real piano fixtures (plans/scoreview-ear.md, E3)

Recordings of a real acoustic piano for tuning and regression-testing the
ScoreView listener. The synthetic piano (`tests/support/synthetic_piano.h`)
covers CI until these arrive — after that, both run forever.

## Format

- **WAV** (PCM16 or float32), any common sample rate, mono or stereo — the
  test reader (`tests/support/wav_reader.h`) mono-mixes automatically.
- macOS voice memos / QuickTime save `.m4a`; convert with:

  ```bash
  afconvert -f WAVE -d LEI16@44100 recording.m4a target-name.wav
  ```

## Recording tips (musicality does not matter — ground truth does)

- Ordinary mic, ordinary room: phone or laptop 1–2 m from the piano, wherever
  you'd naturally practice. Honest conditions beat clean ones — the product
  must survive real rooms.
- Leave **~2 seconds of silence at the start** of every take (the listener's
  warm-up baseline).
- **Slow and deliberate is better than fluent.** Clearly separated notes make
  the truth schedule easy to annotate; hesitations are fine.
- Wrong notes, uneven tempo, and pedal are all *useful data*, not mistakes.

## The takes

All six are engraved in [recording-script.musicxml](recording-script.musicxml)
— put it on the stand via ScoreView itself:

```bash
./build/draxul.app/Contents/MacOS/draxul --host score \
  --source tests/fixtures/audio/recording-script.musicxml
```

(Sections 4–5 embed the Grieg fixture's opening 8 measures verbatim; the
bold red-ish "play a WRONG note near here" marks in section 5 are
suggestions, not obligations.)

| File | What to play |
| --- | --- |
| `chromatic-a1-c6.wav` | Slow chromatic scale, one note at a time, A1 up to C6 (or whatever range is comfortable — rename to match, e.g. `chromatic-c2-c5.wav`) |
| `scale-c-major-hands-together.wav` | C-major scale, hands together in octaves, up and down, unhurried |
| `chords-common.wav` | 5–6 isolated chords with a pause between each: e.g. C major triad, G major, an octave C3+C4, a wide C3+E4+G4, F major, a bass fifth C2+G2 |
| `grieg-first-phrase.wav` | The opening phrase of the Grieg waltz (the repo fixture piece) at practice tempo — slow is perfect |
| `grieg-wrong-notes.wav` | The same phrase again with 2–3 deliberate wrong notes (remember roughly where!) |
| `room-silence.wav` | ~10 s of the room doing nothing (maybe a cough, a chair creak) |

If a take goes badly, keep it anyway under a `-take2` suffix — messy data is
exactly what the tuning session needs.
