Review the repository through the supplied `repomix-output.xml` in one coordinating review run. It contains source paths, line numbers, directory structure, documentation, tests, tooling, and initialized product submodules. Every reviewer and subagent must use this same packed file as the sole source input. Read and search within it using bounded tool output; continue after truncation. Do not create runner batches, segment receipts, or coverage JSON.

## Review procedure and completion criteria

1. **Map the system first.** Read the embedded `CLAUDE.md`, `docs/module-map.md`, `docs/features.md`, relevant CMake targets, and applicable root/product agent guidance. Map the frame loop, input-to-display path, client/server messaging, terminal and Neovim updates, background work, GPU submission, plugin rendering, startup, and data loading before selecting review areas.
2. **Delegate a bounded review team.** Explicitly use up to four read-only subagents for coherent, non-overlapping subsystem investigations. Balance the scope across workers; do not assign arbitrary character ranges. Give each worker this performance focus, the architecture context, its assigned areas and boundary checks, and the same packed input. Use the same model and high reasoning effort for Codex/Claude; do not substitute a cheaper model. Prohibit nested delegation. Workers return evidence and inspected/unreviewed areas to the coordinator. If delegation is unavailable, perform the same investigations sequentially and disclose that limitation.
3. **Cover every major area.** The coordinator and workers together must examine application/frame orchestration and configuration; client/server/control and Session lifecycle; terminal/Neovim/PTY/input behavior; rendering, fonts and platform implementations; built-in modules; each initialized product plugin; and relevant SDK, build, tests, scripts and documentation. Inspect root and product Kanban lanes to exclude already tracked work. For each area, trace representative hot paths through callers, data sizes, frequency, existing caching/coalescing, platform branches, and tests. A directory listing or keyword match alone is not substantive review.
4. **Verify and connect the results.** Wait for every worker. The coordinator must re-read cited evidence and surrounding control/data flow before accepting a finding, check existing bounds and counterexamples, reconcile overlap, and perform a final cross-component pass over input-to-frame latency, unnecessary work while idle, lock contention, allocation/copy pressure, I/O, GPU/CPU synchronization, background jobs, and Windows/macOS parity. Follow the evidence rules below; do not turn this into a correctness or general refactor review.
5. **Finish on scope, not finding count.** Do not stop because several good findings were found, and do not impose a target number. Finish when every major area has been substantively examined and the verification/boundary pass is complete, or when a real time/context/tool limit prevents it. If limited, return useful verified results and explicitly mark the review partial. Never describe unverified worker claims as confirmed findings.

Report every distinct, substantiated finding without an arbitrary count or length cap. End with a short coverage table: area, representative paths or workloads examined, whether the coordinator or a worker inspected it, and remaining gaps. Identify delegation failures and partially reviewed areas. Keep unresolved performance leads separate from accepted findings, with the measurement or source evidence still needed; do not recommend Kanban cards for those leads. Coverage is an honest account of investigation, not proof that all bottlenecks were found.

Your sole focus is **runtime performance that matters to a real user or a bounded background workload**: responsiveness, frame pacing, throughput, startup time, memory/VRAM use, power/idle work, and scalability with realistic data size. Build/test speed may be reported only when a specific repeated developer workflow has a demonstrable cost. Do not report general code hygiene, feature ideas, or correctness bugs unless the issue is itself a performance failure; route those to their own reviews.

Specifically examine:

1. **Frame and input latency:** work repeated every frame or keypress, redundant relayout/rasterization/upload, event-to-render delays, unnecessary full redraws, blocking on the UI thread, and idle wakeups.
2. **CPU and memory:** avoidable allocations/copies, repeated scans or parsing, poor complexity at realistic board/session/project sizes, unbounded caches or queues, and unnecessary retained state.
3. **Concurrency and I/O:** lock contention, over-serialization, chatty client/server exchanges, repeated filesystem/network reads, expensive polling, and background tasks that compete with interactive work.
4. **GPU and platform paths:** excessive uploads, synchronization stalls, render-pass churn, resource recreation, and Vulkan/Metal differences that waste work; verify which backend actually owns the operation.
5. **Products and startup:** expensive plugin frame preparation, simulation or analysis work, loading/discovery costs, and poor behavior when data grows or a pane is hidden or idle.

For each candidate, establish a **reachable workload** and the exact repeated or scaling work. Prefer existing timing, counters, benchmarks or render traces where the packed repository contains them. A static finding is acceptable when the code shows a concrete frequency, input scale and cost mechanism; label it as static evidence. Never invent timings, speedups, memory savings or benchmark results. If the workload, cost, or existing mitigation cannot be established, leave it as an unresolved lead. Consider total cost, not just a local allocation: an optimization that adds invalidation complexity, memory use, latency spikes or cross-platform risk may be a net loss.

For each accepted finding, report:

- **Location:** concrete files, symbols and line numbers.
- **Priority:** P0 (severe interactive stall or resource blow-up), P1 (material common-path cost), or P2 (credible bounded-workload improvement), with a brief user-impact rationale.
- **Workload and impact:** what the user or background worker is doing, how often it happens, and the data size at which it matters.
- **Evidence and confidence:** measured evidence if present, otherwise a clearly identified static cost model; existing protections and why they do not remove the cost.
- **Bottleneck:** the repeated work, complexity, allocation, I/O, lock or GPU synchronization responsible.
- **Smallest safe change:** the targeted optimization and its cache/invalidation, memory and correctness tradeoffs.
- **Validation:** a reproducible baseline and after-change benchmark or counter, a regression threshold appropriate to that workload, functional tests, and relevant Windows/macOS or Vulkan/Metal checks.
- **Dependencies:** any prerequisite that must land first or area that shares the same bottleneck.

Do NOT report:

- “This might be faster” suggestions without a reachable workload and supported cost mechanism.
- Micro-optimizations likely hidden by I/O, GPU work or compiler optimization, unless evidence shows they matter.
- Caches without a clear invalidation rule and memory bound, or parallelism without an ownership/synchronization plan.
- Correctness bugs, style changes, speculative rewrites, or refactors whose only benefit is cleaner structure.
- Items already tracked in any root or product `kanban/pending/`, `kanban/ice-box/`, or `kanban/done/` lane.

Rank findings by expected user benefit, confidence and implementation risk, not source size or an invented percentage. End with a concise workload/benchmark plan for the highest-value findings and name any strong existing performance safeguards worth preserving. Return the entire report as Markdown so the calling script receives the full review.
