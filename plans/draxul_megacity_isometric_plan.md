# Draxul Megacity Isometric Scene Plan

## Purpose

This document reframes the isometric game-engine prototype so it fits **Draxul's existing Megacity architecture** instead of creating a second engine beside it.

The goal is to evolve the current Megacity spinning-cube proof of concept into a tiny backend-independent isometric scene prototype that:

- lives inside `libs/draxul-megacity`
- uses Draxul's existing `I3DHost` + `IRenderPass` architecture
- works with both Vulkan and Metal through the current renderer boundary
- starts extremely small: a 5x5 grid and one cube on one tile
- keeps rendering simple: one forward pass, one directional light, per-vertex lighting
- defers ECS complexity until after the first scene is working cleanly

---

## Executive Summary

### What already exists in Draxul

Draxul already provides:

- a renderer hierarchy: `IBaseRenderer -> I3DRenderer -> IGridRenderer`
- a clean custom render hook through `IRenderPass::record(IRenderContext&)`
- a Megacity feature module under `libs/draxul-megacity`
- a `MegaCityHost` that owns a `CubeRenderPass`
- backend-specific Megacity rendering in:
  - `src/megacity_render_vk.cpp`
  - `src/megacity_render.mm`

That means the right move is **not** to build a standalone game engine abstraction.

The right move is to evolve the current Megacity block into a tiny scene runtime that submits renderer-friendly scene data into a Megacity render pass.

### What should change

Instead of:

- `MegaCityHost` updating a spinning angle
- `CubeRenderPass` drawing one hardcoded cube

we want:

- `MegaCityHost` owning a tiny world model and camera
- a scene snapshot being prepared on the CPU side
- an `IsometricScenePass` consuming that snapshot
- backend-specific GPU work remaining where it already belongs

### What should not change

Do **not**:

- create a second renderer abstraction
- push world/game logic into `draxul-renderer`
- introduce physics, asset loading, scripting, or animation
- build a general render graph
- add ECS before the first 5x5 board is already rendering

---

## Architectural Background

## Draxul boundaries to respect

### 1. `draxul-renderer` owns backend mechanics

`draxul-renderer` should continue to own:

- swapchain/device lifecycle
- depth attachment lifecycle for the shared frame
- begin/end frame
- backend-specific resource creation
- backend-specific shader/pipeline state
- pass invocation
- frame capture behavior

It should **not** become home to:

- grid-world logic
- scene layout rules
- tile placement logic
- camera policy for Megacity
- ECS/domain objects

### 2. `draxul-megacity` owns Megacity domain logic

`draxul-megacity` should own:

- world model
- camera model
- scene item generation
- Megacity-specific UI panels
- render pass public interface
- optional future ECS
- Megacity-specific backend helper code where it is only used by Megacity

### 3. `IRenderPass` is the correct insertion point

The existing pass model is already the right architecture for this feature.

Megacity should continue to render through a registered `IRenderPass`, rather than bypassing the renderer or creating direct backend coupling from the host.

### 4. Backend independence should remain pragmatic

Backend independence here means:

- the host and scene code are shared
- the render pass interface is shared
- the CPU-side scene snapshot is shared
- Metal and Vulkan each keep their own low-level implementation details

It does **not** mean inventing a giant fake graphics API over both backends.

That would be how one turns a cube into a management consultancy.

### 5. Depth is required for the scene path

The current cube demo gets away without meaningful depth because it renders a
single object into a color-only path.

The isometric scene plan should explicitly require:

- a real depth attachment in the shared renderer frame
- depth state enabled for the Megacity scene pass
- Metal and Vulkan parity for board-plus-cube visibility

Do not treat depth as a later polish item. It is part of the first real scene
rendering path.

---

## Target Architecture

## High-level shape

```text
MegaCityHost
  -> owns IsometricWorld
  -> owns IsometricCamera
  -> builds SceneSnapshot each frame
  -> pushes SceneSnapshot into IsometricScenePass
  -> requests a frame

IsometricScenePass : IRenderPass
  -> consumes SceneSnapshot
  -> owns backend-specific State
  -> records draw commands through IRenderContext

Backend-specific render code
  -> Metal pipeline/state in megacity_render.mm
  -> Vulkan pipeline/state in megacity_render_vk.cpp
```

## Core idea

Split responsibilities into three layers:

### Layer 1: world/domain layer
Pure Megacity logic:

- grid dimensions
- tile size
- object placement
- camera target
- scene object list

### Layer 2: render-prep layer
Converts world state into renderer-friendly data:

- final transforms
- light direction
- camera matrices
- mesh identifiers
- colors/material constants

### Layer 3: backend recording layer
Converts scene snapshot into GPU commands:

- bind pipeline
- upload uniforms
- bind buffers
- issue draw calls

---

## Recommended Initial Design

## Step 1: replace the cube-only mental model

The current spinning-cube demo is useful as a proof that the 3D hook works, but it is too specific.

Replace this mindset:

- `CubeRenderPass` draws a cube from an angle

with this mindset:

- `IsometricScenePass` draws a tiny scene from a scene snapshot

The pass can still begin by rendering only one cube, but the API and data flow should stop pretending the cube is the whole universe.

---

## Proposed Types

## New shared scene types

Create a small header for CPU-side shared scene data.

These types should be shared between the Megacity host and Megacity render-pass
implementation, but they do **not** need to become part of the wider Draxul
public API unless another module genuinely needs them.

### Preferred first location: `src/isometric_scene_types.h`

If later reuse justifies promotion, they can move to
`include/draxul/isometric_scene_types.h`.

Suggested contents:

```cpp
#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

namespace draxul
{

enum class MeshId : uint32_t
{
    Grid,
    Cube,
};

struct SceneObject
{
    MeshId mesh = MeshId::Cube;
    glm::mat4 world{1.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f};
};

struct SceneCameraData
{
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::vec3 light_dir{-0.5f, -1.0f, -0.3f};
    float _pad0 = 0.0f;
};

struct SceneSnapshot
{
    SceneCameraData camera;
    std::vector<SceneObject> objects;
};

} // namespace draxul
```

Notes:

- Keep this dumb and POD-like.
- Do not put backend types here.
- Do not put ECS types here.
- Do not put asset-loading semantics here.

This is the handoff format between Megacity logic and the render pass.

---

## World model

### Preferred first location: `src/isometric_world.h`

Start simple:

```cpp
#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace draxul
{

struct GridTile
{
    bool occupied = false;
};

struct GridObject
{
    int x = 0;
    int y = 0;
    int elevation = 0;
    glm::vec3 color{1.0f, 1.0f, 1.0f};
};

class IsometricWorld
{
public:
    static constexpr int kWidth = 5;
    static constexpr int kHeight = 5;

    IsometricWorld();

    bool is_valid(int x, int y) const;
    glm::vec3 grid_to_world(int x, int y, float elevation = 0.0f) const;

    int width() const { return kWidth; }
    int height() const { return kHeight; }
    float tile_size() const { return tile_size_; }

    const std::vector<GridObject>& objects() const { return objects_; }
    std::vector<GridObject>& objects() { return objects_; }

private:
    float tile_size_ = 1.0f;
    GridTile tiles_[kWidth][kHeight]{};
    std::vector<GridObject> objects_;
};

} // namespace draxul
```

### Behavior

- Keep the grid fixed at 5x5 initially.
- Place one `GridObject` into the world at startup.
- `grid_to_world()` should return 3D world coordinates on the XZ plane.
- Y is elevation.

A sensible first mapping:

```cpp
world_x = x * tile_size
world_z = y * tile_size
world_y = elevation
```

No need for “isometric math” here. The camera creates the isometric look.

---

## Camera model

### Preferred first location: `src/isometric_camera.h`

Suggested class:

```cpp
#pragma once

#include <glm/glm.hpp>

namespace draxul
{

class IsometricCamera
{
public:
    void set_viewport(int pixel_w, int pixel_h);
    void look_at_world_center(float world_w, float world_h);

    glm::mat4 view_matrix() const;
    glm::mat4 proj_matrix() const;

private:
    glm::vec3 position_{-6.0f, 7.0f, -6.0f};
    glm::vec3 target_{2.0f, 0.0f, 2.0f};
    float ortho_half_height_ = 4.0f;
    float aspect_ = 1.0f;
};

} // namespace draxul
```

### Camera policy

For the first version:

- fixed camera
- orthographic projection
- target centered on the 5x5 world
- no orbiting
- no mouse look

This gives the cleanest board-game style image.

### Why orthographic first

Orthographic avoids perspective exaggeration and makes grid alignment visually clearer.

That is ideal for:

- tile placement
- debugging transforms
- validating scene layout
- future UI overlays and picking

---

## Scene pass

### `src/isometric_scene_pass.h`

```cpp
#pragma once

#include <draxul/base_renderer.h>
#include <draxul/isometric_scene_types.h>
#include <memory>

namespace draxul
{

class IsometricScenePass : public IRenderPass
{
public:
    IsometricScenePass();
    ~IsometricScenePass() override;

    void set_scene(SceneSnapshot snapshot);
    void record(IRenderContext& ctx) override;

    struct State;

private:
    SceneSnapshot scene_;
    std::unique_ptr<State> state_;
};

} // namespace draxul
```

### Why this is the right shape

This preserves the existing Draxul approach:

- shared pass type in common code
- backend-specific `State`
- low-level resource ownership hidden in the implementation file

The pass should not query the world directly. It should consume already-prepared render data.

That keeps the pass small and testable.

---

## Mesh handling

## Do not start with model loading

Add a tiny internal mesh library.

### `src/mesh_library.h/.cpp`

Suggested scope:

- `BuildUnitCubeMesh()`
- `BuildGridMesh(int width, int height, float tile_size)`

Suggested vertex format:

```cpp
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 color;
};
```

The grid can be implemented either as:

### Option A: line mesh
Pros:
- simple to reason about
- visually clean for debugging

Cons:
- line raster rules vary slightly by backend/platform

### Option B: thin quad strips / plane tiles
Pros:
- more robust across APIs
- easier to light consistently

Cons:
- slightly more geometry

Recommendation:

- if the existing cube pass already has a triangle pipeline, prefer **tile quads or a plane mesh** over line rendering
- avoid introducing a second pipeline just to draw lines in milestone 1

---

## Lighting model

Keep it primitive and useful.

### First lighting model

- one directional light
- Lambert diffuse only
- per-vertex lighting
- small ambient term
- no shadows
- no specular
- no textures

Suggested formula:

```cpp
float ndotl = max(dot(normal_ws, -light_dir), 0.0f);
float lighting = 0.2f + 0.8f * ndotl;
final_rgb = base_color * lighting;
```

This is more than enough to make the cube readable.

It also keeps the shader path extremely stable while you validate transforms and backend parity.

---

## Megacity host changes

## Current responsibility

`MegaCityHost` currently:

- manages viewport
- manages frame timing
- owns `CubeRenderPass`
- updates a rotation angle in `pump()`
- requests frames
- renders ImGui UI
- renders the existing Tree-sitter inspection panel

## New responsibility

Evolve it into:

```text
MegaCityHost
  owns IsometricWorld
  owns IsometricCamera
  owns IsometricScenePass
  builds SceneSnapshot
  updates pass scene each frame
  optionally handles simple input
```

### Header changes

Update `megacity_host.h`:

- replace `CubeRenderPass` member with `IsometricScenePass`
- add `IsometricWorld world_`
- add `IsometricCamera camera_`
- remove `rotation_angle_` if no longer needed
- optionally keep a test animation state if useful

### Source changes

In `initialize()`:

- initialize world with one object on one tile
- configure camera centered over the world
- store viewport info

In `set_viewport()`:

- update camera aspect/projection inputs
- forward viewport to renderer as before

In `pump()`:

- update any world animation or simple demo logic
- build a new `SceneSnapshot`
- call `scene_pass_->set_scene(snapshot)`
- request a frame

The existing Tree-sitter ImGui panel can remain in place during this refactor.
It is orthogonal to the scene work and should not be coupled to scene state
unless there is a deliberate follow-up plan to make it Megacity-specific UI.

### Suggested helper

Add a private method:

```cpp
SceneSnapshot MegaCityHost::build_scene_snapshot() const;
```

This keeps `pump()` readable and makes the scene preparation path explicit.

---

## Suggested `MegaCityHost::build_scene_snapshot()` behavior

The first version should be extremely explicit.

Pseudo-flow:

1. Create empty `SceneSnapshot`
2. Fill camera matrices and light direction
3. Add a grid object
4. Add one cube object from world object list
5. Return snapshot

Pseudo-code:

```cpp
SceneSnapshot MegaCityHost::build_scene_snapshot() const
{
    SceneSnapshot scene;
    scene.camera.view = camera_.view_matrix();
    scene.camera.proj = camera_.proj_matrix();
    scene.camera.light_dir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));

    SceneObject grid;
    grid.mesh = MeshId::Grid;
    grid.world = glm::mat4(1.0f);
    grid.color = glm::vec3(0.35f, 0.35f, 0.40f);
    scene.objects.push_back(grid);

    for (const auto& obj : world_.objects())
    {
        SceneObject cube;
        cube.mesh = MeshId::Cube;
        glm::vec3 pos = world_.grid_to_world(obj.x, obj.y, static_cast<float>(obj.elevation));
        cube.world = glm::translate(glm::mat4(1.0f), pos + glm::vec3(0.0f, 0.5f, 0.0f));
        cube.color = obj.color;
        scene.objects.push_back(cube);
    }

    return scene;
}
```

The `+0.5f` Y offset assumes a unit cube centered at the origin so it sits on the floor plane.

---

## File-by-file breakdown

## Files to modify

### `libs/draxul-megacity/include/draxul/megacity_host.h`

Change:

- replace cube-specific member naming
- add world/camera/scene members
- add `build_scene_snapshot()` declaration

Suggested member set:

```cpp
std::shared_ptr<IsometricScenePass> scene_pass_;
IsometricWorld world_;
IsometricCamera camera_;
```

### `libs/draxul-megacity/src/megacity_host.cpp`

Change:

- initialize world and camera
- attach new scene pass instead of cube pass
- update `pump()` to build/pass scene data
- keep ImGui support intact

### `libs/draxul-megacity/src/megacity_render_vk.cpp`

Change:

- update pass state to accept multiple objects
- add grid mesh GPU buffers
- support per-object world transform + color
- keep viewport/scissor behavior compatible with the existing renderer context
- enable depth-tested drawing against the renderer-owned depth attachment

### `libs/draxul-megacity/src/megacity_render.mm`

Same changes as Vulkan version, but for Metal.

### `libs/draxul-megacity/CMakeLists.txt`

Add new source files:

- `src/isometric_world.cpp`
- `src/isometric_camera.cpp`
- `src/isometric_scene_pass.cpp`
- `src/mesh_library.cpp`

Potentially rename references if replacing the cube pass.

---

## Files to add

### Private-first headers

Under `libs/draxul-megacity/src/` first:

- `isometric_scene_types.h`
- `isometric_world.h`
- `isometric_camera.h`
- `isometric_scene_pass.h`
- `mesh_library.h`

### Promote to public headers only if needed

Under `libs/draxul-megacity/include/draxul/`, only if another module needs them:

- `isometric_world.h`
- `isometric_camera.h`
- `isometric_scene_types.h`

### Implementations

Under `libs/draxul-megacity/src/`:

- `isometric_world.cpp`
- `isometric_camera.cpp`
- `isometric_scene_pass.cpp`
- `mesh_library.cpp`

---

## Detailed implementation guidance for the render pass

## Pass API

The pass should receive the full scene snapshot by value or moved value.

Recommended:

```cpp
void IsometricScenePass::set_scene(SceneSnapshot snapshot)
{
    scene_ = std::move(snapshot);
}
```

That is simple and safe.

No need for incremental object diffing yet.
No need for multi-threaded scene submission yet.
No need for lock-free heroics yet.

---

## GPU state strategy

## Shared principle

Each backend-specific implementation should lazily initialize its resources on first use or on render-pass change.

State should include:

- pipeline/shader program
- depth-stencil state
- vertex/index buffers for cube
- vertex/index buffers for grid
- uniform buffers / small constant upload path

### Vulkan notes

Likely shape of backend `State`:

- pipeline layout
- graphics pipeline
- depth-enabled pipeline state
- descriptor set layout / descriptor set if needed
- uniform buffer(s)
- mesh vertex/index buffers
- cached framebuffer/render pass dependent objects

### Metal notes

Likely shape of backend `State`:

- `MTLRenderPipelineState`
- `MTLDepthStencilState`
- `MTLBuffer`s for meshes and uniforms
- metallib shader function handles if needed

## Keep pass behavior very narrow

Within `record()`:

1. ensure backend state exists
2. set viewport/scissor from `IRenderContext`
3. bind pipeline and depth state
4. upload camera data
5. loop scene objects
6. bind mesh
7. upload object transform/color
8. draw

No visibility system yet.
No instancing yet.
No material system yet.

---

## Uniform structure suggestions

Use simple CPU-to-GPU structs.

### Camera/frame data

```cpp
struct FrameUniforms
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 light_dir;
};
```

### Object data

```cpp
struct ObjectUniforms
{
    glm::mat4 world;
    glm::vec4 color;
};
```

Keep alignment in mind for each backend.

Recommendation:

- make all vectors `vec4` in GPU-facing structs
- avoid clever packing until later

---

## Shader suggestions

## Vertex shader responsibilities

- read vertex position, normal, color
- transform position by world/view/proj
- transform normal into world space
- compute diffuse lighting from directional light
- output lit color

## Fragment shader responsibilities

- output interpolated color

This keeps the entire first milestone shader path tiny and stable.

---

## Grid rendering recommendations

The grid needs to read clearly in the first version.

### Recommended first visual

Use a flat board or tiled plane beneath the cube.

For example:

- each tile is a quad
- slightly alternating colors, or
- uniform tile color with darker border geometry if easy

A pure line grid is okay for debugging, but a plane-based grid is usually more robust when already using a triangle pipeline.

### Best first compromise

- draw a tiled plane with per-tile color variation
- optionally add thin border lines later

This will make the scene legible immediately, especially under orthographic isometric view.

---

## Input and world motion

The user wants the world to move beneath a fixed camera.

### First version

Do not implement camera controls.

Implement one of these:

#### Option A: move the cube on the grid

- arrow keys change cube tile coordinates
- camera stays fixed

This is the simplest useful behavior.

#### Option B: pan the world origin

- arrow keys add an offset to the whole world transform
- camera stays fixed

This is also valid, but less directly useful than moving the cube.

### Recommendation

Start with **cube movement on the grid** and make that the explicit milestone-4
scope.

That proves:

- world state updates correctly
- scene rebuild works correctly
- transform logic is sound
- camera remains fixed

Later, add world panning if needed.

---

## ECS recommendation

## Do not start with ECS

For this repo and this milestone, the best move is:

- first build a small explicit world model
- then render it cleanly
- then consider ECS

### Why delay ECS

Because the current Megacity feature is still small and focused.

Introducing ECS too early creates risk in three places:

- more concepts for the agent to juggle
- more data ownership questions
- more temptation to leak ECS types across boundaries

### When to add ECS

Add ECS only after:

- grid renders cleanly
- cube placement works
- camera works
- both backends use the same CPU-side scene path

### If/when ECS is added

Use it **only inside `draxul-megacity`**.

Renderer input should still be plain scene snapshots.

Suggested future components:

- `TransformComponent`
- `GridPositionComponent`
- `RenderableComponent`
- `NameComponent`

Do not let `entt::registry` leak into host/render interfaces.

That way lies architectural swamp.

---

## Suggested milestone sequence

## Milestone 1 — scene-pass refactor

### Goal
Refactor the current cube-specific Megacity path into a generic scene-pass path.

### Tasks

- add `SceneSnapshot` types
- add `IsometricScenePass`
- wire Megacity host to use scene pass
- keep one cube rendering

### Acceptance criteria

- Megacity still renders
- one cube still appears
- render pass consumes scene data rather than a single angle

---

## Milestone 2 — add world and camera

### Goal
Introduce `IsometricWorld` and `IsometricCamera`.

### Tasks

- add world model
- add fixed orthographic camera
- center camera on the world
- build scene snapshot from world state

### Acceptance criteria

- cube renders in fixed isometric view
- world-space placement is correct
- viewport resize still works

---

## Milestone 3 — add 5x5 grid

### Goal
Render the board itself, not just the cube.

### Tasks

- add grid mesh generation
- add grid render item to scene snapshot
- validate cube alignment to tiles

### Acceptance criteria

- 5x5 board is visible
- cube sits on one tile cleanly
- no backend-specific logic leaks into host/world code

---

## Milestone 4 — add simple interaction

### Goal
Let the scene move while the camera stays fixed.

### Tasks

- arrow keys move the cube, or equivalent simple action
- update world object coordinates
- rebuild scene each frame as needed

### Acceptance criteria

- cube can move tile-to-tile
- camera remains fixed
- transforms remain stable on both backends

---

## Milestone 5 — harden and clean up

### Goal
Make the feature pleasant for future agent work.

### Tasks

- improve naming
- add comments at architecture boundaries
- document data flow
- add simple tests for grid-to-world and camera behavior
- optionally expose a small debug panel in ImGui

### Acceptance criteria

- code is easy to extend
- world/render boundaries are obvious
- no needless abstraction debt has been added

---

## Testing and validation suggestions

Draxul already has useful validation habits. Follow them.

## Unit tests worth adding

### 1. `grid_to_world()` tests

Check:

- `(0,0)`
- `(4,4)`
- a center tile
- elevation handling

### 2. camera centering tests

Check:

- target after centering on 5x5 world
- projection aspect update on resize

### 3. scene snapshot tests

Check:

- object count
- first object is grid
- cube transform translation matches grid coordinates

## Visual validation

Once the render path is stable, consider a dedicated Megacity render scenario similar in spirit to existing render tests.

Not required for milestone 1, but a good later hardening step.

---

## Code style and structure guidance for the agent

## General rules

- Keep new code explicit and small.
- Prefer straightforward data flow over flexible abstractions.
- Avoid templates unless they clearly simplify the code.
- Keep Megacity types local to `draxul-megacity` unless there is a strong reason to publish them.
- Preserve backend-independent CPU-side structures.
- Preserve backend-specific GPU state isolation.

## Naming rules

Prefer names like:

- `IsometricWorld`
- `IsometricCamera`
- `IsometricScenePass`
- `SceneSnapshot`
- `SceneObject`

Avoid names like:

- `IRenderableSceneGraphFactoryManager`
- `BackendAbstractGeometrySystem`

No one deserves that.

---

## Specific guidance for Vulkan and Metal implementations

## Vulkan

The Vulkan side likely already contains pass-specific logic for the cube.

Refactor it rather than replacing everything.

### Suggested approach

- keep existing pipeline setup patterns where possible
- extend mesh handling to support grid + cube
- upload per-frame camera uniforms once
- upload per-object world/color uniforms inside the loop or through a small ring buffer path
- wire the Megacity scene pass into a renderer frame that has a depth attachment

### Avoid

- creating a second Vulkan rendering subsystem
- broad renderer changes outside what the new pass genuinely needs

## Metal

Mirror the Vulkan structure conceptually.

### Suggested approach

- keep pass-local state in `State`
- create a pipeline for the isometric scene
- keep shader library loading in the same style as current Megacity rendering
- record one simple draw sequence per object
- use a depth-stencil state and a renderer frame that includes depth

### Avoid

- drifting into a completely different scene model on Metal

The CPU-side scene preparation must remain shared.

---

## Concrete implementation checklist

## CMake / build

- [ ] add new Megacity source files to `libs/draxul-megacity/CMakeLists.txt`
- [ ] ensure new headers are included via existing public include path
- [ ] keep platform split source selection intact
- [ ] add any renderer-side depth-buffer wiring needed by both Metal and Vulkan

## Host layer

- [ ] replace `CubeRenderPass` usage with `IsometricScenePass`
- [ ] add world/camera members
- [ ] initialize a default world with one cube
- [ ] update `pump()` to build and submit a scene snapshot

## Shared scene code

- [ ] add scene snapshot types
- [ ] add camera math
- [ ] add world/grid math
- [ ] add mesh generation helpers

## Backend rendering

- [ ] support grid mesh buffers
- [ ] support cube mesh buffers
- [ ] support frame uniforms
- [ ] support object uniforms
- [ ] enable depth attachment/state for the Megacity scene path
- [ ] render all scene objects from snapshot

## Validation

- [ ] build on Windows/Vulkan
- [ ] build on macOS/Metal
- [ ] visually confirm board + cube layout
- [ ] visually confirm correct depth ordering for board + cube
- [ ] verify resize still works

---

## Example coding-agent brief

You can hand this directly to a coding agent.

---

# Coding Agent Brief

Extend Draxul's existing `draxul-megacity` feature so the current spinning-cube proof of concept becomes a tiny backend-independent isometric scene prototype.

## Constraints

- Keep all world and scene logic inside `libs/draxul-megacity`
- Use the existing `I3DHost` + `IRenderPass` architecture
- Do not add a new renderer abstraction
- Preserve Metal/Vulkan backend-specific rendering in the existing platform files
- Add renderer-owned depth support for the Megacity scene path on both backends
- Start without ECS
- Use a fixed orthographic isometric camera
- Render a 5x5 grid and one cube on a tile
- Use one forward pass only
- Use one directional light with simple per-vertex diffuse lighting
- Keep code explicit and easy to follow

## Required deliverables

1. Replace cube-only render pass flow with a generic `SceneSnapshot`-driven Megacity scene pass
2. Add `IsometricWorld`, `IsometricCamera`, and minimal mesh generation helpers
3. Update `MegaCityHost` so it builds a scene snapshot each frame and passes it to the render pass
4. Update Metal and Vulkan Megacity rendering code to draw the grid and cube from the shared scene snapshot
5. Add depth-buffer support needed for correct board/cube rendering on both backends
6. Preserve viewport behavior and existing host/render boundaries

## Acceptance criteria

- Megacity mode renders a visible 5x5 board
- One cube appears on one tile
- Camera is fixed in isometric orthographic view
- Cube movement works while camera stays fixed
- The CPU-side scene path is shared between Metal and Vulkan
- Depth ordering is correct for the board and cube
- No new renderer abstraction or general engine framework has been introduced

## Non-goals

- no asset loading
- no physics
- no animation system
- no scripting
- no materials/textures
- no shadows
- no ECS in the first pass
- no editor features

---

## Optional follow-up after first success

Once the first version works well, the next sensible upgrades are:

1. add ECS inside `draxul-megacity`
2. add simple tile selection/highlight
3. add more than one cube/object type
4. add a dedicated Megacity render regression scenario
5. add simple board/pawn input interactions

That is the right order.

Not “design a general simulation framework for future city metaphysics.”

---

## Final recommendation

Treat this project as:

> evolving Draxul's Megacity cube demo into a tiny scene runtime

not as:

> building a second standalone engine inside the repo

That framing is the cleanest fit for the codebase, the renderer boundaries, and future agent contributions.
