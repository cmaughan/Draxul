1. **CRITICAL — Scene edits can terminate the application**  
   `plugins/rezonality/src/live_project.cpp:413`  
   Saving a camera with `field_of_view: -` matches the numeric regex but makes `std::stof` throw. The exception escapes `build()` and the worker thread (`:1237`, `:1019`), invoking `std::terminate` instead of preserving the last valid scene.  
   **Fix:** Validate numeric conversions and catch worker exceptions, publishing a failed `BuildResult`.

2. **CRITICAL — Resizing during Vulkan capture can read beyond the capture buffer**  
   `libs/draxul-renderer/src/vulkan/vk_renderer.cpp:1360`  
   A resize causing presentation to return out-of-date recreates the swapchain before capture readback. `finish_capture_readback()` uses the replacement extent (`:409`), although the buffer contains the previous extent. Growing beyond the allocated capture size causes out-of-bounds CPU reads; shrinking produces incorrectly shaped output.  
   **Fix:** Complete readback before swapchain recreation, using dimensions saved when recording the capture.

3. **HIGH — Concurrent SatView refreshes can delete the downloaded cache**  
   `plugins/satview/src/services/satview_catalog_service.cpp:105`  
   Separate SatView panes share a cache directory but write through the same `.tmp` filename. If both writers open it before one publishes it, the second writer’s rename fails because the temporary pathname has disappeared. Its fallback (`:124`) then deletes the successfully published cache, leaving subsequent offline launches without it.  
   **Fix:** Use unique temporary files and an atomic replacement operation that preserves the existing cache on failure.

Static review used only `repomix-output.xml`, including related tests and tracker entries. Coverage was not exhaustive; no files were changed and no builds or tests were run.
