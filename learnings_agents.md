# Learnings: Building Complex Software with AI Agents

Everything learned about agent-driven development from Draxul — a GPU-accelerated Neovim frontend and 3D code-city visualizer built 100% agentically across Claude, Codex, and Gemini, representing several person-years of equivalent effort in under 3 weeks.

---

**Orchestration & Workflow**

1. Worktree isolation (`isolation: "worktree"`) gives each parallel agent its own repo copy — the integration step is where the main agent earns its keep by reasoning about intent across all diffs.
2. When one agent moves code and another modifies the same code at its original location, the integrating agent can transplant the modification to the new location — don't fear overlapping assignments.
3. The human role distills to: setting direction, triaging agent output, resolving cross-agent conflicts, providing real-app logs for renderer bugs, and pushing back on code smells.
4. Work items as individual markdown files with checkboxes, file lists, and acceptance criteria are effectively machine-readable task specs any agent can pick up and execute without clarification.
5. The three-tier work item lifecycle (active → icebox → complete) with 292 completed, 22 active, and 48 iceboxed items creates a self-documenting project history.
6. Agents should update work item markdown in the same turn as completing the work — ticking checkboxes and moving files to `work-items-complete/` — creating an audit trail.
7. Embedding sub-agent recommendations directly in work items (e.g., "can be done in the same agent pass as WI 41, both mechanical") lets the human batch agent work efficiently.
8. Work items that flag high-conflict files ("app/app.cpp is a high-conflict file — do not combine this WI with other active app/ changes") prevent merge disasters.
9. Saved prompt templates in `plans/prompts/` create a macro system — the phrase "come to consensus" triggers a complex multi-step synthesis operation reproducibly.
10. Number collisions across planning waves are inevitable — always reference work items by full filename, never by number alone.

**Speed Amplification**

11. The multi-agent review process produced 19 confirmed bugs with full work items (investigation steps, fix strategy, acceptance criteria, interdependencies) in a single session.
12. A parallel review consensus produced 22 work items spanning bugs, tests, refactors, and features with a complete interdependency map in one pass.
13. 25 bugs were fixed in a single session — the structured work items gave agents enough context to execute without back-and-forth.
14. 14 refactors were run in parallel using worktree isolation, each agent working on its own copy of the repo, then integrated by the main agent.
15. Cross-cutting architectural changes (new host abstraction, factory pattern, CLI selection, ConPTY terminal emulation, test updates) can be completed in a single agent pass when the work item is well-specified.
16. Agents discovered that expected changes were unnecessary — the ligature feature needed no shader changes, contradicting the review prediction — showing agents adapt to what they find rather than blindly following plans.
17. The icebox (48 items) preserves agent-generated ideas with full implementation plans, preventing both scope creep and lost ideas.
18. Batching related refactors ("param_or dedup + CONFIGURE_DEPENDS — same agent pass, both mechanical, low blast radius") maximizes throughput without increasing risk.
19. Feature implementation that spans config, font pipeline, grid rendering, test scenarios, and documentation can happen end-to-end in one agent pass when protocols are well-specified.
20. The 292 completed work items represent a volume of structured engineering output that would be impractical without agents.

**Multi-Model Reviews & Consensus**

21. Running three independent AI reviews (Claude, GPT, Gemini) then synthesizing a consensus catches more bugs than any single model — each model found bugs the others missed.
22. Separating bug-focused reviews from general code reviews prevents signal dilution — the bug prompt explicitly excludes style, naming, and architecture concerns.
23. The bug review prompt specifies what NOT to report (code smells, style issues, already-tracked items, theoretical issues without trigger scenarios) — negative constraints are as important as positive ones.
24. Consensus synthesis is not concatenation — it identifies agreements, unique contributions, real disagreements, and stale/already-fixed items, reconciled against the current tree.
25. Each confirmed bug in the consensus gets: file path verified against source, severity rating, trigger scenario, suggested fix, and attribution to the agent(s) that reported it.
26. GPT uniquely found a remote OOM/DoS via unbounded `reserve()` in msgpack decoding; Claude uniquely found the scrollback attr compaction bug; Gemini uniquely found the BMP `read_u32` signed shift UB.
27. All three agents agreed on some findings (param_or duplication, App god-class) — convergence increases confidence; it's the disagreements that need human judgment.
28. The consensus correctly filtered stale findings — issues already fixed in the current tree are called out rather than repeated blindly.
29. Claude tends toward architectural analysis, Gemini toward build system and test infrastructure, GPT toward operational/UX concerns — model diversity covers more surface area.
30. The consensus document is itself authored by an agent reading other agents' outputs — meta-agent coordination works when the synthesis prompt is well-specified.

**Architecture Preservation**

31. Agent context files (CLAUDE.md, AGENTS.md, GEMINI.md) containing dependency graphs, threading invariants, and known pitfalls prevent agents from breaking the architecture.
32. The "Known Pitfalls" section captures hard-won rules ("never duplicate a header between src/ and include/draxul/") that agents would otherwise violate repeatedly.
33. `docs/features.md` as a canonical feature registry prevents agents from proposing duplicate work — they must check it before creating new work items.
34. Agents must update `docs/features.md` after implementing features — keeping the registry current prevents future agents from re-proposing existing capabilities.
35. Threading invariants documented in agent context files ("all grid and GPU state only touched by main thread") prevent agents from introducing concurrency bugs.
36. The dependency graph constraint ("libraries only link downward") is stated once and enforced by every agent that touches CMakeLists.txt.
37. SonarCloud caught an agent leaving a duplicate header in both `src/` and `include/draxul/` — external tooling validates what documentation prescribes.

**Tooling as Agent Guardrails**

38. `do.py` with short single-word commands (`smoke`, `basic`, `blessall`, `test`) lowers friction — agents can validate their own work without remembering complex command sequences.
39. The pre-commit clang-format hook means agents cannot ship unformatted code — if formatting fails, the commit is aborted; the agent re-stages and retries.
40. Render snapshot testing with pixel-diff makes rendering regressions agent-detectable — change shaping code, run render tests, see concrete pixel mismatch.
41. The `--smoke-test` CLI flag lets agents verify app startup without manual interaction.
42. ASan/UBSan CI catches memory safety issues that agents introduce — the PTY EINTR and scrollback stale-stride bugs were ASan-detectable.
43. Replay fixtures let agents write UI-parsing regression tests by constructing synthetic msgpack payloads without launching Neovim.
44. `FakeWindow` and `FakeRenderer` enable end-to-end orchestration tests without real hardware — critical for agent-generated tests.
45. CI runs both platforms and installs Neovim — agent-authored tests are validated cross-platform automatically.
46. The `bless` workflow lets agents update reference images when expected changes occur, closing the feedback loop for rendering changes.
47. CMake `CONFIGURE_DEPENDS` was added as a work item after GPT identified that `file(GLOB ...)` without it causes silent test discovery failures — agents improving the build for future agents.
48. Test work items specify exact validation commands ("Run: cmake --build build --target draxul-tests && ctest -R terminal_vt") — no ambiguity about how to verify.

**What Agents Are Good At**

49. Mechanical refactoring: extracting duplicated lambdas, renaming variables, adding type aliases — low risk, clear acceptance criteria, single file touched.
50. Implementing well-specified protocols: VT escape sequences, msgpack-RPC, terminal mouse modes — agents read specs and produce correct implementations.
51. Writing test suites from work items with specific test vectors: "all-zero buffers, valid header with truncated body, nested arrays deeper than expected."
52. Filing structured work items from review findings: severity ratings, file paths, trigger scenarios, fix strategies, interdependency notes.
53. Architecture diagram generation: grepping for inheritance relationships, cross-checking class existence, rendering to SVG.
54. Config plumbing: adding TOML keys, validation ranges, CLI flags, and wiring them through to the subsystem that uses them.
55. Cross-cutting feature implementation when the interfaces are clear: host abstraction + factory + CLI + terminal emulation + tests in one pass.
56. Generating comprehensive documentation from codebase analysis — `docs/features.md` at 280 lines covers every capability.
57. Finding bugs through systematic code review — 19 confirmed bugs from three independent AI reviews is a better hit rate than most human review processes.
58. Dependency graph and module map generation — agents grep CMakeLists.txt and produce accurate library-level dependency visualizations.

**What Agents Struggle With**

59. Renderer debugging: agents speculate from logs while humans can drive the real interaction path — user-supplied logs are more valuable than agent-generated ones.
60. Understanding product intent: all three agents recommended removing MegaCityHost because they saw it as a demo, not understanding it was the core analysis host.
61. Avoiding duplicated logic: agents compute the same value in three places instead of once in the data model — the human must catch and push back immediately.
62. Judging when a feature is "done enough": agents layer suggestions in stages, pushing toward more features, then follow up with criticism of the complexity they introduced.
63. Respecting implicit threading models: agents don't naturally think about which thread owns which data unless explicitly told.
64. Knowing when to use the debugger vs. speculate: the learning notes that an agent "chose on its own to launch lldb" as a positive surprise — it's not the default behavior.
65. Understanding font pipeline complexity: agents initially approached color emoji as a font-selection fix, missing that it requires changes across FreeType, glyph cache, atlas uploads, and shaders.
66. Estimating blast radius of changes: agents sometimes propose "small" refactors that touch 15 files and break transitive dependencies.

**Human Override & Judgment**

67. Persistent memory files (`.claude/projects/.../memory/`) shape future agent behavior — "Use DEBUG not INFO for diagnostics" is enforced across sessions without repeating.
68. The MegaCity purpose correction is a dedicated memory file overriding unanimous agent consensus — agents can be confidently wrong when they misunderstand product intent.
69. The human's observation that "AI builds in stages: suggest features, then refactor/test/improve" is recorded neutrally — understanding the pattern helps work with it rather than against it.
70. Technical preferences like "use GLM for vector/matrix types" are enforced via memory files — agents don't reinvent math structs across sessions.
71. Log level discipline was established after agents left verbose INFO-level logging in the split pane work — feedback memories prevent recurrence.
72. When three agents disagree, the human resolves based on project context the agents lack — App decomposition scope was resolved as "do AppDeps injection first, broader restructuring later."

**Learning Through Agent Collaboration**

73. Working with agents forces explicit documentation of architectural decisions — if it isn't written down, agents will violate it.
74. Agents asking "wrong" questions reveals undocumented assumptions — the font DPI relationship was only documented after agents got confused by it.
75. Multi-model review surfaces different perspectives on the same code — you learn about your codebase by seeing how three different AI models interpret it.
76. Agent-generated work items serve as a structured audit of technical debt — they find and document issues a human would leave as "I'll fix that later."
77. The bug consensus process teaches triage: reading three reviews and deciding which findings are real, stale, or false positives is a skill-building exercise.
78. Watching agents fail at renderer debugging teaches you which parts of your system require interactive debugging vs. static analysis.
79. Agent feature staging mirrors natural development cycles — recognizing this pattern helps you plan agent work in waves rather than fighting the tendency.
80. Reading agent-generated architecture diagrams reveals coupling you didn't notice — the dependency graph made the App god-class problem visually obvious.

**Scaling Insights**

81. The project grew from Neovim frontend to shell hosts + 3D code analysis host using the same pipeline — the host abstraction architecture (documented for agents) enabled this.
82. As the codebase grew, the App class became a god object (~867 lines) — systematic agent-driven decomposition extracted InputDispatcher, GuiActionHandler, and HostManager.
83. Agent context files grow in parallel with the codebase and diverge between models — this creates a maintenance burden (icebox WI 22: agent scripts deduplication).
84. The `docs/features.md` registry becomes critical at scale — once past 100 features, agents will propose duplicates without a canonical check-first reference.
85. Work item interdependency tracking becomes essential as the backlog grows — "WI 38 must land before WI 40" prevents agents from building on unstable foundations.
86. The 4-tier fix order (bugs → tests → refactors → features) prevents agents from building features on top of known bugs.
87. The icebox prevents both idea loss and scope creep — 48 deferred items with full plans are ready for any future session.
88. Work items carrying provenance ("Filed by: claude-sonnet-4-6 — 2026-03-29", "Source: review-latest.claude.md [C]") enables tracing which model's suggestions led to which outcomes.

**Process Design for Agents**

89. Negative constraints in prompts ("do NOT report code smells") are as important as positive instructions — they prevent agents from flooding output with low-value findings.
90. Acceptance criteria with checkboxes give agents a concrete definition of done — no ambiguity about whether the work is complete.
91. File-per-work-item (not a single backlog file) lets agents read only what's relevant and prevents merge conflicts when multiple agents file items simultaneously.
92. The review → consensus → work items → execution → update pipeline is a repeatable, scalable process for agent-driven engineering.
93. Storing prompts that produce good results (`plans/prompts/`) creates institutional knowledge — the consensus review prompt is reusable across projects.
94. Separating "investigation" checkboxes from "implementation" checkboxes in work items lets agents validate assumptions before coding.
95. Work items that specify sub-agent splits ("one agent on transport, one on callsites, merge after contract settled") encode parallelization strategy in the task spec.
96. The `sync_project_board.py` bridge between markdown work items and GitHub project board means agents work in markdown while humans get a visual board.

**Emergent Patterns**

97. Agents improve tooling for future agents — adding CONFIGURE_DEPENDS, creating replay fixtures, building FakeRenderer — each investment compounds.
98. The test suite (80+ files) is almost entirely agent-authored, yet catches real bugs because the work item system specifies exact test vectors and validation commands.
99. Agent-generated documentation stays current because agents are instructed to update it as part of completing work — not as a separate documentation pass.
100. The entire project demonstrates that complex, multi-platform, GPU-accelerated software can be built without a single line of human-written code when the orchestration, guardrails, and feedback loops are right.
