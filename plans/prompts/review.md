Explore the repository source files directly — use Glob and Read (or equivalent file-reading tools) to scan all source files under `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, and `plans/`. Read the actual files as they exist on disk; do not rely on any pre-generated combined file.

Look at the separation of modules and the general layout. Look for bad code smells, or things that will make it harder for multiple agents to work on the codebase. Do a thorough review. Look for testing holes, and for code that is not clean or easy to maintain. Look for opportunities to separate concerns and make things modular. Be sure to check the latest code — it changes fast. docs/features.md has a list of implemented features, and work-items-complete/work-items-icebox should not be duplicated in your review - they are completed or on ice for a reason.

At the end of the report give the top 10 good, top 10 bad things about the application.

When that is done, find the best 10 features that could be added to improve quality of life.

Find the best 10 tests that could be added to improve stability.
Find the worst 10 features.

Finally, the entire output will be returned as markdown output so that a script calling this gets the full report.
