Read exactly the optimization-focused review reports listed in `INPUT_REVIEWS/index.md` and return one complete consensus as your final Markdown response. The trusted runner publishes the consensus and any enabled Kanban cards; do not write files or substitute mutable latest-file globs.

Map the relevant architecture and compare the reports' actual workload coverage before reconciling findings. Use up to four read-only subagents for independent verification of coherent groups of findings when useful, with no nested delegation; use the same model and high effort for Codex/Claude. Wait for all workers, re-read their evidence before accepting it, and check cross-component implications. If delegation is unavailable, verify sequentially. Reconcile against current source, documentation, existing benchmarks and metrics, and root/product tracker lanes. Distinguish confirmed opportunities, rejected claims, already-tracked work, and unresolved leads. State verification gaps and do not turn unresolved leads into tasks. Report every distinct accepted finding without a numeric quota or arbitrary output cap.

## Plain-English summaries

Start every accepted finding or recommendation with `**Summary:**` and one short sentence explaining what will change and why it is needed, for someone who has not read the code. Use everyday language; avoid class names, file paths, acronyms, and unexplained technical terms. Describe the current problem and intended benefit without claiming unsupported results or measured speedups. Put technical evidence and implementation details below the summary.

Every proposed, created, or revised Kanban card, including cards merged into existing work and cards in product submodules, must put this summary immediately below its title heading and before priority, source, or other metadata. Preserve existing evidence and checklist progress when revising a card.

This is **runtime optimization planning**, not a general refactor or bug triage. Build/test speed belongs here only when a repeated developer workflow has a demonstrated cost.

1. **Deduplicate by bottleneck and workload.** Agents may describe the same extra work in different files. Merge those into one opportunity and credit each agent who found it.
2. **Verify the cost.** Trace the cited path through callers, frequency, data size, existing cache/coalescing and platform branches. Prefer recorded timings or counters; accept a static cost model only when workload and repeated work are concrete. Never invent speedups or treat a profile hypothesis as measured fact.
3. **Check total impact.** Ask whether the proposed change reduces end-to-end latency, throughput cost, memory/VRAM, startup time or idle work. Account for added allocation, cache invalidation, synchronization, power and correctness risk. Reject micro-optimizations with no credible user or bounded-workload benefit.
4. **Resolve disagreement.** Compare claims about frequency, likely cost, priority and platform behavior. State why the consensus accepts, narrows, defers or rejects each disputed proposal.
5. **Prioritize honestly.** Use P0 only for a substantiated severe stall or resource blow-up, P1 for a material common-path cost, and P2 for a credible bounded-workload improvement. Order accepted work by user benefit, confidence and implementation risk.

For each accepted optimization, include:

- Concrete files, symbols and current-source line numbers.
- The representative workload and user-visible or background impact.
- The bottleneck mechanism and existing protections that were checked.
- Evidence level: measured, instrumented in existing tests, or static cost model; include actual numbers only when present in the source reports or repository evidence.
- The smallest safe implementation, including memory, invalidation, threading and platform tradeoffs.
- A reproducible baseline and after-change measurement plan, functional regression checks, and Windows/macOS or Vulkan/Metal coverage where applicable.
- Priority, confidence, dependencies and which agent or agents reported it.

Include a separate **Existing task refinements** section for substantiated measurement plans or implementation constraints that belong in an existing root/product card; identify the exact card path and do not create a duplicate. Keep **Unresolved leads** separate and state the evidence needed before they can become work.

Return one complete implementation-ready Markdown work item for the runner to create for each new accepted opportunity. Use root `kanban/pending/` for core/shared work and `plugins/<product>/kanban/pending/` for work wholly owned by an initialized product submodule. Use an unused two-digit number in the owning lane and end optimization implementation cards with `-perf.md`; use `-test.md` only for a standalone measurement/testing improvement. Do not overwrite or duplicate an existing card. Each card must have a title heading, a `**Source:**` line with a backtick-quoted owning source path, a standalone `**Priority:** Pn` line, an evidence/confidence note, and scoped checkboxes for baseline measurement, implementation, functional safety, before/after performance comparison, cross-platform checks, and acceptance criteria. If the source evidence supports only an investigation, leave it in Unresolved leads rather than creating a card.

End with a short ranked measurement plan and the performance safeguards that should be preserved. Append your `<model>` identifier to the consensus file and clearly state sequencing and interdependencies. Return the full consensus, including complete proposed cards under exact `### kanban/pending/<filename>.md` or `### plugins/<product>/kanban/pending/<filename>.md` headings when task creation is enabled.
