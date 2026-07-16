# ScoreView — Research on Optimal / Minimum-Time Learning of a Piano Piece

Evidence review feeding the adaptive piano-learning runner (see
[scoreview-manifesto.md](scoreview-manifesto.md)). Produced by **two** deep-research passes
(pass 1: 6 angles → 27 sources → 118 claims; pass 2: focused re-verification of the
sleep / slow-practice / imagery / feedback findings + a dedicated search on hands-separate
practice and backward chaining → 25 sources → 103 claims). Both auto-synthesis steps aborted
on a session limit, so this note is the human-assembled synthesis of the verified + extracted
claim sets. Pass 2 upgraded most Part-1 motor-learning findings from [P] to [V] with exact
statistics and canonical URLs, and filled the Part-2 gaps (hands-separate, backward chaining).

## Confidence legend

- **[V]** — adversarially verified (2–3 independent skeptic votes to keep). Highest confidence.
- **[P]** — quoted from a primary source but *not* adversarially verified (verification queue was cut off). Solid-but-single-checked.
- **[?]** — extracted from a primary source; verification errored before completing. Directional only.

---

## Bottom line (the highest-leverage findings)

1. **How you practice beats how much you practice.** In the canonical Duke/Simmons piano
   study, retention quality had *no* significant correlation with practice time or number of
   repetitions, but strong correlation with practice *accuracy* and how errors were handled. **[V]**
2. **Errors during practice are corrosive, not neutral.** The single strongest predictor of
   next-day retention was the percentage of *complete* run-throughs played correctly (r = −.71);
   more incorrect trials predicted worse retention. Practising a passage wrong trains the wrong thing. **[V]**
3. **Segment by musical structure, not by fixed windows.** Expert practice starts and stops at
   section/phrase boundaries far more than elsewhere (p < .001), and the piece's formal hierarchy
   becomes the memory-retrieval scheme. Chunk boundaries should map to section/phrase boundaries. **[V]**
4. **The tail of each section is where difficulty lives.** Within a section, later bars are
   repeated more and recalled far worse (recall ≈ .97 at a section's first bar → ≈ .28 at positions
   5–8). Section openings are cheap; interiors/ends need over-scheduling. **[V]**
5. **Interleaving vs. blocked is genuinely contested for real repertoire** — do not treat "always
   interleave" as settled. Lab tasks show a large interleaving retention benefit; applied/musical
   settings show it shrink to near-zero or even reverse. **[V]**
6. **Slow practice and sleep do real work.** Practising *below* target tempo (500 ms/keystroke,
   slower than the player's own max) **doubled** attainable speed over 4 days and held for ~2
   months; overnight sleep *selectively* consolidates a sequence's hardest transition (**+17.8%**,
   P < .001) while barely touching easy ones (**+1.4%**, n.s.). Both now verified with exact stats. **[V]**
7. **Hands-separate practice is effector-specific — it builds each hand but not the integration.**
   Piano speed gains transfer *within* a hand (~1.5×) but **not across hands** (F(1,10)=0.3, p=.60),
   and two-hand sequences are stored as an integrated whole that one-hand practice transfers to only
   ~10%. Use hands-alone to build each hand; train hands-together for the coordinated timing. **[V/P]**
8. **Backward chaining is *not* supported for keyboard — a common piano belief that the data
   contradicts.** The one direct keyboard experiment found it no faster than forward/whole-task and
   *significantly more error-prone* after a week; its parent (ABA) literature finds it merely equal
   to forward chaining, with zero music evidence. Don't default the runner to end-first sequencing. **[P]**

---

## Part 1 — What makes practice efficient (results per unit time)

### Deliberate practice: necessary, but a weaker raw predictor than folklore claims
- A music-specific meta-analysis (13 studies, N = 788) found a **large** corrected correlation
  between accumulated deliberate practice and musical achievement, **rc = 0.61, 95% CI [0.54, 0.67]**.
  ([Platz et al. 2014](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC4073287/)) **[V]**
- But measurement method inflates that link. In the broader Macnamara meta-analysis, studies using
  objective **logs** (what an app actually collects) attributed only **~5% of performance variance
  (r = .22)** to practice quantity, vs. 12% for questionnaires and 20% for interviews. Self-reported
  practice hours overstate the effect.
  ([Macnamara et al. 2014](https://hhs.purdue.edu/skill-learning-and-performance-lab/wp-content/uploads/sites/43/2024/08/macnamara-et-al-2014-deliberate-practice-and-performance-in-music-games-sports-education-and-professions-a-meta-analysis.pdf)) **[P]**
- **Takeaway:** hours are a floor, not the lever. The lever is what happens inside the hour.

### Quality of practice (the Duke, Simmons & Cash 2009 study — the direct answer to "what do efficient practicers do")
17 advanced piano majors learned a hard 3-bar Shostakovich passage to a fixed target tempo, tested next day. ([Duke, Simmons & Cash 2009, JRME](https://journals.sagepub.com/doi/abs/10.1177/0022429408328851))
- **No** significant relationship between retention and practice time, total trials, or complete trials. **[V]**
- **Strong** relationships with *how* they practiced (rankings ordered best-first, so negative r = better):
  - % of all trials correct: **r = −.51**
  - % of *complete* run-throughs correct: **r = −.71** (strongest single predictor)
  - number of incorrect trials during practice: **r = +.48** (more errors → worse retention) **[V]**
- Conclusion in the authors' words: strategies "were more determinative of performance quality at
  retention than was how much or how long the pianists practiced." **[V]**
- What the top practicers actually did (from this and the related Duke corpus): played *slowly enough
  to be accurate*, isolated the exact hard spot rather than restarting from the top, corrected errors
  immediately and didn't play past them, and repeated the *corrected* version — not the whole passage.

### Interleaving / contextual interference — real, but weaker for music than the textbooks imply
This is the most nuanced area; the evidence genuinely splits.
- **Classic CI paradox (motor tasks):** random/interleaved practice hurts *during acquisition* but
  helps *retention and transfer*. ([2024 meta-analysis, Nature Sci Rep](https://www.nature.com/articles/s41598-024-65753-3)) **[V]**
- **But the benefit is context-dependent:** that same meta-analysis found the retention benefit
  **large in lab tasks (SMD ≈ 0.92)** yet **negligible and non-significant in applied settings
  (SMD ≈ 0.23, p = .24)**. Learning a real piece is an applied setting. **[V]**
- **Music evidence disagrees with itself:**
  - *For* interleaving: 10 clarinetists in an ecological context — interleaved pieces rated better
    than blocked whenever ratings differed. ([Carter & Grahn 2016](https://pmc.ncbi.nlm.nih.gov/articles/PMC4989027/)) **[V]**
  - *Against* interleaving: 20 advanced violinists — no acquisition difference, and at 24-hour
    retention the **blocked** schedule was *best*. ([Mathias & Goldman 2025, JRME](https://journals.sagepub.com/doi/10.1177/00224294231222801)) **[V]**
  - Serial-RT lab work lands pro-interleaving: interleaved trainees improved overnight while blocked
    trainees got worse, and interleaving transferred to novel sequences regardless of test order.
    ([PMC8476370](https://pmc.ncbi.nlm.nih.gov/articles/PMC8476370/)) **[V]**
- **Acquisition ≠ retention.** The strategy that maximizes *in-session* fluency is not necessarily
  the one that maximizes durable skill. Do not tune scheduling to same-session gains. **[V]**
- **The fluency trap:** learners prefer blocked practice because repetition *feels* fluent, even
  though the harder-feeling schedule often retains better. An app should expect users to resist
  interleaving and may need to nudge. **[P]**
- **Design reading for ScoreView:** interleave *across* chunks/pieces (revisit earlier chunks, don't
  drill one to death), but don't over-randomize a single hard passage — for one passage, correct
  repetition dominates. This straddles the split evidence rather than betting the disputed direction.

### Spacing / distributed practice — real over days, unreliable over minutes
- **Over long horizons:** the optimal gap between sessions **grows with how long you need to retain**
  the material (nonmonotonic lag effect): sub-minute gaps for sub-minute retention; ~1-month gaps for
  6-month retention. ([Cepeda et al. 2006 meta-analysis](https://augmentingcognition.com/assets/Cepeda2006.pdf)) **[V]**
- **Over short horizons:** inserting 1/5/10/15-minute gaps between two piano sessions produced **no**
  spacing benefit over massed practice — because no forgetting occurred in the gap.
  ([Piano novice study, PLOS ONE](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0182986)) **[P]**
- **Expanding schedules** (progressively widening review gaps) beat fixed intervals across 18 studies,
  but the evidence is weak (large SEs). **[P]**
- **Design reading:** schedule review of a mastered chunk *across days/sessions with widening gaps*,
  not within a single sitting. Space to induce a little forgetting, then retrieve.

### Slow practice / tempo ramping — verified, with hard numbers
- Four days practising a 12-note sequence at a **fixed submaximal** tempo (500 ms inter-keystroke
  interval — significantly slower than participants' own pre-practice max of 345 ± 54 ms) **doubled**
  attainable maximum speed (session×group F(7,70)=13.7, p=5×10⁻¹¹), and slow practice alone was
  *sufficient* to raise the ceiling. ([BMC Neuroscience 2013, 14:133](https://link.springer.com/article/10.1186/1471-2202-14-133) · [PMC4228459](https://pmc.ncbi.nlm.nih.gov/articles/PMC4228459/)) **[V]**
- The doubled speed **persisted ~2 months** with no intervening practice (Day-4 vs 2-month: paired
  t(5)=−1.73, p=0.14, i.e. no significant decline). **[V]**
- Gains were **effector-specific** — they transferred *within* the trained hand (untrained sequence
  ~1.5× faster) but **not across** to the other hand (see Hands-separate, Part 2). **[V]**
- **Design reading:** a "start slow, ramp tempo only when accuracy gates are met" loop is
  evidence-backed. Speed is an *output* of accurate slow reps, not something to chase directly.

### Mental practice / motor imagery — genuine, engages motor circuits, best when varied
- Mental practice recruits real motor circuitry: fMRI of 12 pianists imagining vs. playing a Bartók
  passage found **both** engaged a bilateral fronto-parietal/premotor network (precuneus, medial
  BA40) — but primary motor cortex fired **only during actual playing**. Imagery trains *planning*,
  not execution. ([Meister et al. 2004, *Cogn. Brain Res.*](https://www.sciencedirect.com/science/article/abs/pii/S0926641004000023)) **[V]**
- The canonical piano five-finger study (Pascual-Leone et al. 1995, TMS) compared *mental* vs
  *physical* practice of the same exercise; physical practice measurably enlarged the finger muscles'
  cortical maps. ([PMID 7500130](https://pubmed.ncbi.nlm.nih.gov/7500130/)) **[V]**
  *(The oft-repeated "imagery produced identical cortical reorganization" claim could **not** be
  corroborated from the accessible abstract — treat that stronger version as unverified.)*
- **Variable** motor-imagery practice produced further gains *after a night's sleep* and best transfer
  to novel sequences; **constant/repetitive** imagery produced no delayed gains. Vary the mentally
  rehearsed order. ([PMID 25562401](https://pubmed.ncbi.nlm.nih.gov/25562401/) · review [PMC4923126](https://pmc.ncbi.nlm.nih.gov/articles/PMC4923126/)) **[V]**

### Sleep / consolidation — verified; treat overnight as part of the learning curve
- Overnight sleep yields large speed gains with **no** extra practice (unimanual-5 **+17.7%**,
  t(14)=6.23, P<0.0001; up to **+28.9%** for the complex bimanual-9 task), whereas an equal ~8-hour
  span of daytime **wake yields +0.9%** (t(13)=0.35, P=0.72) and slight accuracy *decline*. Harder/
  denser sequences gain *more*. ([Kuriyama, Stickgold & Walker 2004, *Learn. Mem.* 11(6):705](https://learnmem.cshlp.org/content/11/6/705.full) · [PMC534699](https://pmc.ncbi.nlm.nih.gov/articles/PMC534699/); orig. [Walker et al. 2002, *Neuron*](https://www.sciencedirect.com/science/article/pii/S0896627302007468), ~20% overnight) **[V]**
- Sleep is **selective**: it preferentially resolves a sequence's slowest, hardest transition
  ("problem point", **+17.8%**, P<0.001) while barely improving easy transitions (**+1.4%**, n.s.);
  the dissociation is significant (ANOVA F(1,53)=19.8, P<0.0001) and does **not** appear during
  waking training. **[V]**
- **Design reading:** identify each piece's "problem-point" junctions; expect them to improve after
  sleep even without more reps. Don't expect all improvement within one session; schedule a re-test
  *after* a night rather than grinding the same night.

---

## Part 2 — How to break a piece into teachable units

### Whole vs. part vs. whole-part-whole
- The decision is driven by **task complexity and organization** (Naylor–Briggs) or skill
  classification (Schmidt–Wrisberg): use **whole** practice when sub-parts are highly *interdependent*
  (high organization) and the task isn't too complex; use **part** practice when the task is complex
  but its parts are relatively *independent*. A meta-analysis found effect sizes generally support
  these textbook rules. ([Fontana et al. 2009, whole/part meta-analysis](https://scholarworks.uni.edu/facpub/2208/)) **[V]**
- Music maps naturally onto this: a piece is complex (favouring parts) but a phrase is internally
  interdependent (favouring whole-phrase practice) — hence **phrase-sized chunks, practised whole,
  then reassembled** is the theory-consistent unit.

### Chunk boundaries should follow musical structure
- Expert practice segments **begin and end at formal section boundaries** significantly more than at
  other bars (p < .001 across learning periods). ([Chaffin 2002, *Practicing Perfection*](https://production.wordpress.uconn.edu/musiclab/wp-content/uploads/sites/290/2013/10/Practicing-Perfection-Chaffin-2002.pdf)) **[V]**
- The piece is learned as a **hierarchy** (movement → section → subsection → bar), and that structure
  *is* the retrieval scheme — structural position predicted unaided recall two years later
  (R² = .76). Not an undifferentiated motor stream. **[V]**
- **Section-opening bars act as privileged retrieval cues**: advanced pianists recognized the first
  bar of a section significantly faster (~1010 ms, 89.9% accurate) than interior bars.
  ([Williamon & Egner 2004](http://www.brainmusic.org/EducationalActivities/Williamon_memory2004.pdf)) **[V]**
- **Design reading:** ScoreView should parse a piece into nested musical structure and make chunk
  boundaries = phrase/section boundaries, with section openings as "anchor" checkpoints.

### Serial position: over-schedule the interior/tail of each chunk
- Later bars in a section are repeated more (harder to learn) and recalled far worse (≈ .97 → ≈ .28
  across positions). Memory hesitations concentrate at section ends and designated cue points — and
  these are *predictable in advance*. **[V]**
- **Design reading:** front of a chunk is cheap; weight repetition and feedback toward the *back half*
  of each chunk and the *seams between chunks*.

### Difficulty-based prioritization
- Professional pianists isolate the hardest passages and **drill them repeatedly at slow tempo before
  moving on**. Difficulty-first + slow-repetition is a reported expert strategy. **[?]**
- Consistent with sleep's selective consolidation of "problem points" — the hard junctions are exactly
  what deserve isolation *and* an overnight window.

### Chunking mechanics (and a genre caveat)
- Skill acquisition moves from element-by-element execution toward holistic "Gestalt" processing by
  grouping into meaningful chunks. ([review, PMC10883160](https://pmc.ncbi.nlm.nih.gov/articles/PMC10883160/)) **[P]**
- In **tonal** music chunks are conceptual/harmonic groupings; in **atonal/contemporary** repertoire
  they're often **physical hand-shapes / intervallic models** instead. Segmentation logic may need to
  differ by idiom. **[?]**
- Incremental ("add elements as you go") vs. global ("whole sequence every session") strategies engage
  a shared striatal/fronto-parietal network; global additionally recruits cerebellar + hippocampal
  regions — the two approaches are neurally distinct, not interchangeable.
  ([PMC4141721](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC4141721/)) **[P/?]**

### Gaps in the evidence pulled here
- **Hands-separate vs. hands-together** and **backward chaining** are staples of piano pedagogy but did
  *not* surface with strong empirical backing in this source set. They remain reasonable conventions,
  not verified findings — flagged for a dedicated follow-up search.

---

## Part 3 — What efficient practicers measurably do differently
(Consolidated from Duke/Simmons, Chaffin, Williamon, and the pro-pianist interview study.)

- **Accuracy over volume** — they maximize the proportion of *correct complete* run-throughs, not the
  count of run-throughs. **[V]**
- **Immediate, localized error correction** — they stop at the error, fix it, and repeat the corrected
  fragment rather than playing on. Uncorrected errors predicted worse retention. **[V]**
- **Structure-anchored practice** — they start/stop at section boundaries; as expertise grows,
  practice increasingly starts on *structural* bars and less on merely *difficult* bars (structure
  becomes the retrieval scaffold). **[V for boundaries; ? for the expertise-shift]**
- **Deliberate mental preparation** — more experienced players spend as long imagining the target as
  performing it before they play. **[P]**
- **Difficulty triage** — they identify the few hardest passages and give them disproportionate, slow,
  isolated practice. **[?]**

---

## Part 4 — Feedback & adaptive software (direct evidence for ScoreView)

- **Augmented feedback works, with a large effect.** An AI practice app giving real-time error
  detection + automated scoring beat traditional practice by **d = 1.01** (+3.1 points), and raised
  self-efficacy (time×group η²p = 0.349). It scaffolded a full self-regulated-learning loop:
  forethought/goal-setting → real-time monitoring → self-reflection.
  ([Frontiers in Psychology 2025](https://www.frontiersin.org/journals/psychology/articles/10.3389/fpsyg.2025.1675762/full)) **[P]**
- **Visual > audio feedback for technique.** Trial-by-trial *visual* feedback of movement discrepancy
  vs. an expert significantly improved key-movement similarity (Δ 0.141, CI above zero); **audio-only
  feedback did not** (Δ 0.038, CI includes zero). Visual feedback also drove more motor *exploration*
  (the proposed mechanism for breaking plateaus), and blind listeners heard the difference (53.2% vs.
  44.0% judged closer to target). Caveat: frequent/simple feedback can be *useless or harmful* for
  experts — feedback must add information the player can't already perceive.
  ([bioRxiv 2025](https://www.biorxiv.org/content/10.1101/2025.10.23.683818.full.pdf)) **[P]**
- **Give the target model up front.** Hearing a correct model of the melody *before* practising
  produced larger gains both during training and across the overnight retention interval — a cheap,
  high-leverage move (10 reps at fixed tempo before hands-on, n = 32). **[P]**
- **Adaptive task selection is being done with RL.** A Deep Q-Network agent that ingests
  multi-dimensional performance metrics (technical proficiency, expressiveness, sight-reading,
  interpretation) to pick next tasks + personalize feedback is a concrete design pattern.
  ([PeerJ CS 2025](https://peerj.com/articles/cs-3464/)) **[P]**
- **Gamification helps engagement/knowledge** (children, 8-week module, r = 0.87 on a music-knowledge
  test, and *more uniform* outcomes) — but that outcome was written theory, **not** motor
  proficiency/timing/accuracy. Don't over-read it as evidence for technical skill.
  ([JMIR Serious Games 2026](https://games.jmir.org/2026/1/e80766/PDF)) **[P]**

---

## Design implications for ScoreView (synthesis)

1. **Parse to musical structure first.** Represent a piece as movement → section → phrase → bar and
   make every practice chunk a phrase/section unit. Never chunk by fixed note-count windows. (Ch.
   3–4, Part 2.)
2. **Anchor on section openings, over-weight section tails.** Use first-bars-of-sections as
   checkpoints/anchors; schedule extra reps + feedback on the back half of chunks and the seams
   between them.
3. **Accuracy-gated tempo ramp.** Hold tempo low until the learner plays *complete correct*
   run-throughs of a chunk; only then raise tempo. Track "% complete-correct," the strongest retention
   predictor — not total attempts.
4. **Don't reward playing-through-errors.** Detect errors and steer the learner back to fix-and-repeat
   the corrected fragment; a run with an uncorrected error should count *against*, not toward, mastery.
5. **Interleave across chunks, correct-repeat within a hard chunk.** Rotate among chunks/pieces and
   revisit earlier material (spacing + mild interleaving), but for one hard passage prioritize correct
   repetition over randomization — matching where the music evidence actually agrees.
6. **Space reviews across sessions with widening gaps**, not within a sitting. Schedule a mastered
   chunk to resurface tomorrow, then later, expanding the interval.
7. **Treat sleep as a stage.** Re-test after an overnight gap; expect "problem-point" junctions to
   improve without more reps. Surface this to the user ("come back tomorrow — the hard bar will feel
   easier") rather than encouraging one-session grinding.
8. **Feedback: visual, target-relative, segment-level.** Show discrepancy vs. a target model per
   chunk and per run; present the goal (audio/animation) *before* the chunk. Keep feedback
   informative (what to change), and dial it back as a player nears mastery of a chunk.
9. **Adapt task selection on multi-dimensional state** (accuracy, timing, tempo reached, error
   locations) — an RL/bandit selector fits the "endless runner" framing.
10. **Beware the fluency trap.** Users will *prefer* easy blocked repetition that feels fluent but
    retains poorly. The runner should sometimes make them do the harder-but-better thing.

---

## Caveats & what to research next

- Much of the "how-to-practice" gold (Duke/Simmons, Chaffin, Williamon) rests on **small N** and, for
  Chaffin, an **N = 1 expert case study** (corroborated by cited multi-subject work). Directionally
  strong, precisely calibrated less so.
- The **interleaving** question is genuinely unresolved for real repertoire — resist a hard "always
  interleave" rule in the product.
- **Sleep, slow-practice, mental-imagery, and feedback** findings are [P]/[?] here (verification was
  truncated). Worth a targeted second pass to lock exact effect sizes and pin a couple of source URLs.
- **Not covered by this pass:** hands-separate vs. hands-together, backward chaining, and the optimal
  *number* of reps per session — open follow-ups.

## Source set (27 primary sources; those cited above)
Deliberate practice: Platz 2014 (PMC4073287) · Macnamara 2014 (Purdue). Practice quality: Duke,
Simmons & Cash 2009 (JRME 0022429408328851). Interleaving/CI: Nature Sci Rep 2024
(s41598-024-65753-3) · Carter & Grahn 2016 (PMC4989027) · Mathias & Goldman 2025 (JRME
00224294231222801) · PMC8476370. Spacing: Cepeda 2006 · PLOS ONE (pone.0182986). Whole/part:
Fontana 2009 (scholarworks 2208). Structure/segmentation: Chaffin 2002 · Williamon & Egner 2004 ·
Music & Science 2022 (20592043221132932) · PMC10883160 · PMC4141721. Slow practice / sleep / imagery:
Walker 2004 (learnmem 11/6/705) + motor-learning sources in set. Software/feedback: Frontiers Psych
2025 (fpsyg.2025.1675762) · bioRxiv 2025 (683818) · PeerJ CS (cs-3464) · JMIR Serious Games 2026
(e80766).
