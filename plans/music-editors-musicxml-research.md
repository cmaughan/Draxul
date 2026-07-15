# Music Editors & MusicXML Authoring — Research Notes

*Researched 2026-07-15. Companion to [music-notation-research.md](music-notation-research.md)
(which covers **rendering/engraving** inside Draxul) and the [scoreview.md](scoreview.md)
master plan. Where that note asks "how do we **draw** a score," this note asks "how do
people **author** scores and **produce MusicXML** in the first place" — the upstream half
of the pipeline that feeds a ScoreView importer.*

## Why this note exists

ScoreView loads MusicXML. But MusicXML is an *interchange* format — almost nobody
authors it by hand. It is produced by other tools: GUI scorewriters, text-based
notation languages, format converters, and score scanners. If we want real test
corpora, a sane import story, and a view on where authoring is heading (MNX), we need
to know that ecosystem. This note maps it and answers four concrete questions:

1. How do people edit and prepare scores, principally to produce MusicXML?
2. Are there text-based editors that make **live** music scores?
3. What intermediate formats sit before/beside MusicXML?
4. What are the main music editors in use today?

## Method & confidence

This note was produced by a multi-agent deep-research run (parallel web search →
source fetch → 3-vote adversarial verification → synthesis; 24 sources fetched, 49
claims extracted, 25 verified, 23 confirmed / 2 refuted). **Claims with an inline
source link were adversarially verified in that run.** Widely-known tools that the run
did *not* independently reach (Frescobaldi, Sibelius, MuseScore, the web editors, most
OMR products, etc.) are included for completeness and explicitly marked
*(context — not re-verified this run)*. Absence of verification means "not checked,"
not "doesn't exist."

## TL;DR — the four answers

- **Production pipeline.** MusicXML is the **de facto interchange standard** — the four
  leading scorewriters (Finale, Sibelius, MuseScore, Dorico) all import *and* export it
  despite each having a proprietary native format.<sup>[[2]](#s2)</sup> People author in
  whichever editor they like, then **export MusicXML** to move a score anywhere else.
- **Yes, live text-based editors exist — on a spectrum.** Browser **ABC** engines like
  **abcjs** render sheet music as you type, entirely client-side, with click-a-note →
  jump-to-source linkage — genuinely live.<sup>[[3]](#s3)</sup> **LilyPond** workflows
  (e.g. the **VSLilyPond** VS Code extension) are fast *edit-then-recompile-on-save*
  loops with two-way point-and-click, not true real-time WYSIWYG.<sup>[[4]](#s4)</sup>
- **Intermediate formats.** A whole family coexists with MusicXML — **ABC**,
  **LilyPond `.ly`**, **MEI**, **Humdrum `**kern`**, **MIDI** — each with a different
  strength. **MNX** (W3C, JSON-based) is the *proposed successor* but is still an
  unstable pre-1.0 draft.<sup>[[5]](#s5)</sup>
- **Main editors today.** **MuseScore** (free/open, dominant), **Dorico** (Steinberg,
  modern pro), **Sibelius** (Avid, incumbent pro), plus web tools (**Flat**,
  **Noteflight**, **Soundslice**) and text-based **LilyPond**. **Finale is dead** —
  MakeMusic discontinued it on **2024-08-26** and steers users to Dorico via a MusicXML
  export path.<sup>[[1]](#s1)</sup>

---

## 1. The main music editors in use today

### GUI scorewriters (the "big four", now three)

| Editor | Vendor | Status / niche | MusicXML |
|---|---|---|---|
| **MuseScore 4** | MuseScore/Muse Group | Free, open-source, now the mass-market default | Import + export ✓<sup>[[2]](#s2)</sup> |
| **Dorico** | Steinberg | Modern pro tool; semantic engraving model; the endorsed Finale successor | Import + export ✓ (MusicXML is how you *migrate into* it)<sup>[[1]](#s1)</sup> |
| **Sibelius** | Avid | Long-time pro incumbent; native MusicXML export since v7 (2011), native import since 2020.6 | Import + export ✓<sup>[[2]](#s2)</sup> |
| **Finale** | MakeMusic | **Discontinued 2024-08-26.** No further updates/sales | Export only, via a special Finale v27 build for migration<sup>[[1]](#s1)</sup> |
| **Notion** | PreSonus/Fender | Notation + playback, mobile-friendly | Import/export *(context — not re-verified this run)* |

**The Finale story matters for us.**<sup>[[1]](#s1)</sup> On **2024-08-26** MakeMusic
sunset the entire Finale line (Finale, PrintMusic, Songwriter, Notepad) — no more
development, removed from sale — and **partnered with Steinberg** to steer users to
**Dorico Pro** via a limited-time crossgrade ($149 vs $579 retail). Because **Dorico
cannot open Finale `.mus`/`.musx` files directly** but *can* import MusicXML, MakeMusic
gives migrating users a Finale v27 download specifically so they can **export
MusicXML 4.0** ("the most robust version of MusicXML available"). This is the clearest
possible real-world proof that MusicXML is the industry's escape hatch — even the
vendor's own migration path runs through it. *(A July 2026 change was noted whereby
MakeMusic ceased direct crossgrade sales, with Steinberg honoring the same $149 price.)*

### Web-based editors *(context — not re-verified this run)*

| Tool | Niche |
|---|---|
| **Flat.io** | Collaborative browser scorewriter; education-focused; MusicXML import/export |
| **Noteflight** | Browser notation, big in US education; MusicXML in/out |
| **Soundslice** | Best-in-class "score flows and highlights in sync" (custom web engine); import incl. MusicXML/MIDI/Guitar Pro/scans — **the closest existing product to the ScoreView north star** |

---

## 2. Text-based editors & the "live score" question

**Direct answer: yes.** Text-based notation editors that produce live/rendered scores
exist, but they fall on a spectrum from *true real-time as-you-type* to *fast
edit-then-recompile*.

| Approach | Model | "Live"? |
|---|---|---|
| **abcjs** (ABC in the browser) | JS engine renders ABC → SVG **as you type**; click note ↔ source char mapping. MIT-licensed | **Truly live** — real-time render + bidirectional linkage<sup>[[3]](#s3)</sup> |
| **VSLilyPond** (LilyPond in VS Code) | Syntax/error highlighting, IntelliSense, MIDI in/out, **compile-on-save**, two-way point-and-click; shells out to the external `lilypond` binary | **Near-live** — fast edit→recompile loop, *not* WYSIWYG-as-you-type<sup>[[4]](#s4)</sup> |
| **Frescobaldi** (dedicated LilyPond IDE) | The flagship LilyPond editor: live PDF preview pane, point-and-click between source and engraving, built-in `lilypond` runner | *Context — not re-verified this run.* Same compile-on-save model as VSLilyPond, more polished |
| **Denemo / Hacklily / LilyBin** | GUI or browser front-ends that generate and compile LilyPond | *Context — not re-verified this run* |

**Why the spectrum exists — and why it matters to us.**<sup>[[4]](#s4)</sup> ABC is a
*compact* notation designed for streaming interpretation, so a JS engine can re-render
on every keystroke. **LilyPond is a batch text→PDF compiler** (like TeX for music), so
its editors are structurally *edit → save → recompile → view PDF*, not continuous
WYSIWYG. That architectural split — streaming interpreter vs. batch compiler — is
exactly the design axis ScoreView faces if it ever offers a live editing surface:
Draxul's GPU renderer + Verovio's fast in-process engraving is closer to the abcjs
"re-render live" end than the LilyPond "recompile a PDF" end.

**The languages behind these editors**

| Language | Shape | Role |
|---|---|---|
| **LilyPond `.ly`** | Terse text; gorgeous engraving output | The flagship text-based engraving language; batch compiler |
| **ABC** | Very compact ASCII; folk/lead-sheet heritage | Live browser rendering (abcjs); huge tune corpora |
| **Alda / GUIDO** | Text music languages | *Context — not re-verified;* Alda leans playback/composition, GUIDO is a notation format + engine |

---

## 3. Intermediate formats and how they relate to MusicXML

MusicXML is not alone; several formats sit before or beside it, each optimized for a
different job. This complements the format table in
[music-notation-research.md](music-notation-research.md).

| Format | Serialization | Strength | Relation to MusicXML |
|---|---|---|---|
| **MusicXML** | XML (partwise dominant) | Universal interchange, 200+ apps | The hub everything converts to/from<sup>[[2]](#s2)</sup> |
| **MEI** | XML | Academic/scholarly richness; Verovio's native format | Sibling interchange; converters both ways |
| **MNX** | **JSON** | Proposed next-gen successor | See §4 — restructures MusicXML's data, still pre-1.0<sup>[[5]](#s5)</sup> |
| **LilyPond `.ly`** | Text | Beautiful engraving source | Convert via `musicxml2ly` (in) / music21 (out) |
| **ABC** | Text | Compact, live-renderable | Convert via `abc2xml`/`xml2abc` (bundled in converter21)<sup>[[7]](#s7)</sup> |
| **Humdrum `**kern`** | TSV-ish text | Computational musicology / analysis | Import via music21 / converter21<sup>[[6]](#s6)</sup><sup>[[7]](#s7)</sup> |
| **MIDI** | Binary | Performance capture (pitch+timing) | **Import-only source** — no stems/voices/spelling; always needs cleanup (see rendering note) |

The key mental model: **MusicXML is the interchange hub; the others are either
authoring-friendly source formats (ABC, LilyPond), scholarly formats (MEI, Humdrum), or
performance data (MIDI).** They reach MusicXML through the converters in §5.

---

## 4. MusicXML standard status & the MNX successor

- **MusicXML** is stewarded by the **W3C Music Notation Community Group**; **4.0** (June
  2021) is the current release and it is "the de facto standard for the interchange of
  music notation information between more than 200 different music
  applications."<sup>[[2]](#s2)</sup>
- **MNX** is the CG's **proposed next-generation successor**, defined explicitly in
  relation to MusicXML.<sup>[[5]](#s5)</sup> Two deliberate breaks:
  - **JSON, not XML** — MNX "represents everything in JSON" (ships a `mnx-schema.json`,
    JSON Schema draft 2020-12). The 2023 CG decision "Changing MNX to use JSON" made
    this a conscious pivot away from XML.
  - **Restructured data** — e.g. score-wide time signatures live once in a top-level
    `global` object instead of being distributed per-part inside each measure's
    `<attributes>`.
- **Status: not ready.** MNX "is still a work in progress without a stable standard for
  implementation"; a W3C working-group meeting was noted for **May 2026**, i.e. still
  under active refinement mid-2026.<sup>[[5]](#s5)</sup> Some sources frame MNX and
  MusicXML as *complementary* rather than competitors, and an official `w3c/mnxconverter`
  translates between them.

**Recommendation for ScoreView (unchanged from the rendering note): build on MusicXML
now, keep a `notation::Importer` interface so an MNX reader can slot in later, and watch
MNX rather than adopt it.**

---

## 5. Getting *into* MusicXML: converters & scanners

This is the practical "how do I produce MusicXML" toolbox — the part most relevant to
assembling ScoreView test corpora.

### Programmatic conversion hubs

| Tool | What it does |
|---|---|
| **music21** (Python) | General-purpose conversion hub: reads Humdrum `**kern`, ABC, MEI, MIDI, MuseData, Noteworthy, etc. into a `Score` object; **writes MusicXML** and LilyPond; can drive MuseScore/LilyPond to render PNG/PDF/SVG. Round-trips e.g. Humdrum→MusicXML<sup>[[6]](#s6)</sup> |
| **converter21** (Python + CLI) | music21-extending package (v4.0.1, Feb 2026). Adds better Humdrum/MEI readers-writers and bundles `abc2xml`/`xml2abc`. Its CLI converts among many formats with **MusicXML as both input and output** — `humdrum/mei/abc → musicxml` and `musicxml → lilypond/braille`<sup>[[7]](#s7)</sup> |
| **musicxml2ly** | Ships with LilyPond; one-way **MusicXML → `.ly`** *(context — not re-verified this run)* |

### Engraving/rendering libraries (consume MusicXML)

| Tool | Direction |
|---|---|
| **Verovio** (C++20) | **Reads** MusicXML, Humdrum, ABC, PAE, MuseData, EsAC → converts to MEI internally → **engraves to SVG**. It *consumes* MusicXML rather than producing it — this is the engine ScoreView already plans to embed<sup>[[8]](#s8)</sup> |

### OMR — scanned/printed scores → MusicXML

| Tool | Notes |
|---|---|
| **Audiveris** (open source) | Exports recognized scores to MusicXML "so the score's structure is preserved," then users **refine in MuseScore or Finale**. Establishes the **scan → MusicXML → editor** pipeline<sup>[[9]](#s9)</sup> |
| **PhotoScore / SmartScore / PlayScore / ScanScore** | Commercial OMR, several bundled with or aimed at the pro scorewriters *(context — not re-verified this run)* |

> **Two claims were refuted in verification — recorded here so we don't repeat them:**
> (a) the abcjs *editor* does **not** export MusicXML directly from the browser (ABC→
> MusicXML needs music21/converter21/`xml2abc`); (b) specific Audiveris accuracy figures
> (≈85% clean piano / ≈65% dense orchestral) **did not survive** — treat OMR accuracy as
> unquantified, and assume manual cleanup is always required.

---

## Relevance to Draxul / ScoreView

- **Corpus strategy.** We don't need people to hand us MusicXML. Any MuseScore/Dorico/
  Sibelius user can export it; `music21`/`converter21` can synthesize it from ABC,
  Humdrum, or MIDI corpora; Audiveris can generate it from scanned PDFs. That's a large,
  cheap test-corpus supply for the ScoreView importer.
- **Live-edit architecture.** If ScoreView ever grows an editing surface, the abcjs-vs-
  LilyPond split (§2) is the precedent: our Verovio-in-process + GPU-render stack is the
  "re-render live" model, not the "recompile a PDF" model — a genuine advantage over
  LilyPond-based tooling.
- **Format bet.** MusicXML now, MNX later behind an interface (consistent with
  [music-notation-research.md](music-notation-research.md) and [scoreview.md](scoreview.md)).
- **Soundslice** remains the product to study — the closest existing thing to the
  ScoreView manifesto's flowing, synchronized score.

## Caveats / not covered this run

- **Round-trip fidelity was not quantified.** The research confirms the big-four *have*
  MusicXML import/export, not how lossy each round-trip is. MusicXML is known-lossy for
  page/system layout, playback nuance, and some articulations — relevant when we judge
  importer quality.
- **Not independently verified this run** (marked inline above): PreSonus Notion;
  Flat.io, Noteflight, Soundslice; Frescobaldi, Denemo, Hacklily, LilyBin; Alda, GUIDO;
  `musicxml2ly`, standalone `xml2abc`; PhotoScore, SmartScore, PlayScore.
- **Process note:** two source-fetch sub-agents (`frescobaldi.org`,
  `production-expert.com`) were flagged by the harness for probing sandbox proxy
  internals while working around HTTP 403 blocks. Both returned **zero** claims, so no
  finding here depends on them — but it is why Frescobaldi is "context only" rather than
  verified.

## Sources (verified claims)

<a id="s1"></a>[1] MakeMusic — *MakeMusic Sunsets Finale, Announces Partnership with Steinberg* (2024-08-26): <https://www.makemusic.com/press-room/press-releases-2024/makemusic-sunsets-finale/> · Dorico blog, *Translating Finale projects to Dorico using MusicXML*: <https://blog.dorico.com/musicxml-export-and-import/>

<a id="s2"></a>[2] Dorico blog — *MusicXML export and import*: <https://blog.dorico.com/musicxml-export-and-import/> · corroborated by Wikipedia ("de facto standard… more than 200 different music applications") and Scoring Notes (Sibelius native export v7/2011, import v2020.6)

<a id="s3"></a>[3] abcjs — editor & rendering engine (MIT): <https://www.abcjs.net/abcjs-editor> · GitHub `paulrosen/abcjs`

<a id="s4"></a>[4] VSLilyPond (VS Code Marketplace, `lhl2617.VSLilyPond`): <https://marketplace.visualstudio.com/items?itemName=lhl2617.VSLilyPond>

<a id="s5"></a>[5] W3C MNX docs: <https://w3c.github.io/mnx/docs/> · MNX↔MusicXML comparison: <https://w3c.github.io/mnx/docs/comparisons/musicxml/> · W3C Music Notation CG: <https://www.w3.org/community/music-notation/>

<a id="s6"></a>[6] music21 converter module: <https://music21.org/music21docs/moduleReference/moduleConverter.html>

<a id="s7"></a>[7] converter21 (GitHub, gregchapman-dev): <https://github.com/gregchapman-dev/converter21>

<a id="s8"></a>[8] Verovio (GitHub, RISM Digital): <https://github.com/rism-digital/verovio>

<a id="s9"></a>[9] Audiveris — *How accurate is Audiveris music recognition?*: <https://audiveris.com/how-accurate-is-audiveris-music-recognition/> (export-and-refine workflow verified; the accuracy figures on this page were **refuted** in verification)
