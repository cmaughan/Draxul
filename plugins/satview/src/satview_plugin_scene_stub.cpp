#include <draxul/satview/satview_scene_pass.h>

// The dynamic SatView module prepares the real runtime's scene state, but its
// v1 plugin renderer composites through the public borrowed-command-buffer ABI.
// These module-local definitions deliberately keep the legacy Draxul/VMA scene
// backend out of the DLL. Slice 5 replaces this stub with the full plugin-native
// scene adapter; the static parity host continues using the existing backend.
namespace draxul::satview
{

struct SatViewScenePass::State
{
};

SatViewScenePass::SatViewScenePass()
    : state_(std::make_unique<State>())
{
}

SatViewScenePass::~SatViewScenePass() = default;

void SatViewScenePass::record_prepass(IRenderContext&) {}
void SatViewScenePass::record(IRenderContext&) {}
void SatViewScenePass::render_hdr_debug_ui() {}

} // namespace draxul::satview
