#pragma once

namespace draxul::nanovg_mtl_test
{

enum class FailurePoint
{
    None,
    InitialContextAllocation,
    RendererInitialization,
    AtlasAllocation,
};

struct Lifecycle
{
    bool device_available = false;
    bool context_created = false;
    int backend_allocations = 0;
    int backend_deletions = 0;
    int render_create_calls = 0;
    int atlas_create_calls = 0;
    int render_delete_calls = 0;
};

// Runs one complete create/delete attempt with a thread-local failure seam.
// The seam is disabled before this function returns and has no effect on
// normal application creation.
Lifecycle exercise_creation(FailurePoint failure);

} // namespace draxul::nanovg_mtl_test
