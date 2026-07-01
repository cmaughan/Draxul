# Shared Code Visualization Renderer Module Split Plan

## Goal

Keep one shared backend-neutral scene and renderer for code visualizations, while making the visual metaphors independent.

The desired shape is:

```text
Tree-sitter scan
  -> neutral semantic code snapshot
  -> city metaphor builder OR biology metaphor builder
  -> shared scene snapshot
  -> shared Vulkan/Metal renderer
```

The renderer should not know whether an object came from a building, cell, fibre, organelle, road, or sign. Those are metaphor concerns. The renderer should know meshes, materials, transforms, labels, picking IDs, transparency, shadows, and debug views.

## Current Progress

- Phase 2 has started in place inside `draxul-megacity`: scene snapshot/object/material/camera/source identity types now use `CodeViz*` names.
- The shared render pass and backend files are now named `CodeVizScenePass`, `codeviz_scene_pass.h`, `codeviz_scene_types.h`, `codeviz_render_vk.cpp`, and `codeviz_render.mm`.
- The shared input state file is now `codeviz_input_state.*`; the class was already `CodeVizInputState`.
- Custom mesh transform modes now describe generic block and label scaling rather than building and sign scaling.
- CMake target extraction has not started yet; the next high-value step is still separating codeviz scene/renderer sources into dedicated static libraries.

## Non-Goals

- Do not rewrite the Vulkan or Metal renderer just to prove the boundary.
- Do not rename `modules/megacity/` or `DRAXUL_ENABLE_MEGACITY` in this work.
- Do not move parsing or semantic facts into either metaphor module.
- Do not force geometry generation into the renderer.
- Do not remove the existing city behavior while extracting it.

## Current Problem

`draxul-megacity` currently contains several concerns that should be separate:

- host orchestration and Tree-sitter lifecycle;
- neutral semantic snapshot consumption;
- city projection, layout, routes, signs, tooltips, and mesh cache;
- early biology projection and layout;
- shared ECS scene state and snapshot building;
- shared Vulkan/Metal render backends;
- ImGui panels for generic analysis, city controls, biology controls, and render debugging.

The render infrastructure itself is the right shared foundation, but its inputs still use city-shaped names:

- `BuildingMetrics`, `RoadMetrics`, `RouteSegmentMetrics`, `SignMetrics`;
- `MeshId::RoadSurface`, `MeshId::RoofSign`, `MeshId::WallSign`;
- `MaterialId::AsphaltRoad`, `MaterialId::WoodBuilding`;
- `SceneObject::Role::ModulePark`, `ModuleLabel`, `ModuleOutline`;
- `MegaCityCodeConfig` mixes renderer, camera, city layout, signs, parks, performance overlay, and biology controls.

## Target Libraries

Keep the optional module isolated, but split its internal libraries like this:

```text
modules/megacity/
  draxul-treesitter
    Raw parser snapshots.

  draxul-code-semantics
    CodeSemanticSnapshot, stable semantic IDs, indexes, module ownership.

  draxul-geometry
    Renderer-independent mesh data and procedural mesh generation.
    Flexible enough for city, biology, and future metaphors.

  draxul-codeviz-scene
    Shared neutral scene records consumed by the renderer.
    No city or biology vocabulary.

  draxul-codeviz-renderer
    Shared CodeVizScenePass plus Vulkan/Metal backends.
    Owns GPU buffers, shadows, AO, post pass, material upload, mesh upload.

  draxul-codeviz-host
    Shared code-analysis host/session, camera/input glue, selection, scan progress.
    Composes a selected metaphor builder with the shared renderer.

  draxul-megacity-metaphor
    City metaphor: code city projection, layout, routes, signs, city UI, tooltips.

  draxul-biology-metaphor
    Biology metaphor: tissue/cell/organelle/fibre projection, layout, biology UI, tooltips.

  draxul-megacity
    Thin compatibility/factory target while existing host names settle.
    Registers --host megacity and --host bioview.
```

Initial extraction can use slightly shorter target names if preferred, but the ownership boundaries above should be kept.

## Target Dependency Direction

```text
draxul-treesitter
        |
        v
draxul-code-semantics
        |
        v
draxul-codeviz-host --------------------+
        |                               |
        v                               |
  metaphor interface                    |
        |                               |
        +--------------+----------------+
                       |
        +--------------+--------------+
        |                             |
        v                             v
draxul-megacity-metaphor      draxul-biology-metaphor
        |                             |
        +--------------+--------------+
                       |
                       v
              draxul-codeviz-scene
                       |
                       v
             draxul-codeviz-renderer
                       ^
                       |
               draxul-geometry
```

`draxul-geometry` is intentionally shared, but it must stay renderer-independent and metaphor-light.

Forbidden dependencies:

- `draxul-code-semantics` must not include city, biology, scene, renderer, host, GPU, or ImGui headers.
- `draxul-codeviz-renderer` must not include city or biology metaphor headers.
- `draxul-codeviz-scene` must not expose `Building`, `Road`, `Cell`, `Organelle`, or similar metaphor names.
- `draxul-megacity-metaphor` and `draxul-biology-metaphor` must not depend on each other.
- Core Draxul libraries must not depend on any of these optional-module targets.

## Shared Scene Contract

Replace city-shaped scene components with neutral renderable records.

Proposed core types:

```cpp
using CodeVizSceneObjectId = uint64_t;
using CodeVizMeshHandle = uint32_t;
using CodeVizMaterialHandle = uint32_t;

struct CodeVizSemanticRef
{
    CodeSemanticNodeId node_id = 0;
    CodeSemanticEdgeId edge_id = 0;
    std::string source_file_path;
    std::string display_name;
};

struct CodeVizRenderable
{
    CodeVizSceneObjectId object_id = 0;
    CodeVizMeshHandle mesh = 0;
    CodeVizMaterialHandle material = 0;
    glm::mat4 world{1.0f};
    glm::vec4 color{1.0f};
    glm::vec4 uv_rect{0.0f, 0.0f, 1.0f, 1.0f};
    bool double_sided = false;
    bool casts_shadow = true;
    bool selectable = true;
    CodeVizSemanticRef semantic_ref;
};

struct CodeVizSceneSnapshot
{
    CodeVizCameraData camera;
    CodeVizEnvironment environment;
    std::vector<CodeVizMesh> meshes;
    std::vector<CodeVizMaterial> materials;
    std::vector<CodeVizRenderable> objects;
    std::vector<CodeVizLabel> labels;
    std::vector<CodeVizOverlay> overlays;
};
```

The important part is not these exact names; it is the ownership:

- metaphor builders create scene objects;
- the shared renderer draws scene objects;
- source navigation uses semantic IDs, not city strings;
- tooltip and selection payloads stay outside renderer-specific GPU state.

## Shared Renderer Ownership

Move these pieces toward `draxul-codeviz-renderer`:

- `codeviz_scene_pass.h`;
- `codeviz_render_vk.cpp`;
- `codeviz_render.mm`;
- `shadow_cascade.*`;
- low-level frame resources, attachments, transient buffers, mesh upload, texture upload;
- AO, shadow, post, debug-view, and present passes;
- backend-neutral material uniform packing.

Keep the renderer API narrow:

```cpp
class CodeVizScenePass : public IRenderPass
{
public:
    void set_scene(CodeVizSceneSnapshot snapshot);
    void record_prepass(IRenderContext& ctx) override;
    void record(IRenderContext& ctx) override;
    void render_debug_ui();
};
```

The renderer may keep optimized static GPU buffers and material caches. It should not know whether a mesh is a city tower, cell membrane, route fibre, or sign board.

## Geometry Library Direction

Keep `draxul-geometry` separate and make it more flexible, not more city-specific.

Target concerns:

- `GeometryMesh`, vertex/index data, bounds, normals, tangents, UVs;
- deterministic mesh builders;
- generic primitives: box, plane, cylinder, prism, ellipsoid, tube, ribbon, ring, billboard/card;
- generic composition helpers: merge meshes, transform vertices, generate LOD-friendly variants;
- procedural surface helpers that can support both city and biology shapes;
- no Vulkan, Metal, ImGui, host, config-document, or semantic-model dependencies.

City-specific wrappers should move out of geometry:

- building rings belong in the city metaphor unless implemented as generic extruded prism/ring helpers;
- roof signs belong in the city metaphor unless implemented as generic label-board meshes;
- tree generation can remain in geometry only if treated as generic procedural branching/foliage, not "central park" logic.

Biology should add geometry by extending generic primitives first:

- ellipsoid/spheroid;
- tube/path mesh for fibres and vessels;
- membrane shell;
- translucent billboard/card clusters if needed.

## Metaphor Interface

Use a small interface that lets the shared host compose a metaphor with the shared renderer.

```cpp
class ICodeVizMetaphor
{
public:
    virtual ~ICodeVizMetaphor() = default;
    virtual std::string_view id() const = 0;
    virtual std::string_view display_name() const = 0;

    virtual void set_semantics(std::shared_ptr<const CodeSemanticSnapshot> semantics) = 0;
    virtual void set_selection(CodeVizSelection selection) = 0;
    virtual CodeVizSelection selection() const = 0;

    virtual void pump(float dt) = 0;
    virtual CodeVizSceneSnapshot build_scene(const CodeVizFrameInputs& inputs) = 0;
    virtual void draw_ui(CodeVizUiContext& ui) = 0;
    virtual std::optional<CodeVizSelection> pick(const CodeVizPickRequest& request) const = 0;
};
```

This interface should be about lifecycle, semantic identity, scene output, and UI. It should not mention buildings, roads, tissues, cells, or backend GPU objects.

## City Metaphor Ownership

Move these into `draxul-megacity-metaphor`:

- `city_builder.*`;
- `semantic_city_layout.*`;
- city projection records such as `CityClassRecord` and `CityDependencyRecord`;
- `CityGrid`, city routes, roads, sidewalks, parks, module signs;
- `static_mesh_family_cache.*`;
- city-specific mesh specs and cache keys;
- `city_picking.*`;
- `building_tooltip.*`;
- city-specific parts of `live_city_metrics.*`;
- city sections of `ui_treesitter_panel.*`;
- `ui_city_map_panel.*`.

The city metaphor should output neutral `CodeVizSceneSnapshot` objects. A city building is a semantic/presentation concept before the snapshot; in the snapshot it is just a mesh handle, material handle, transform, color, labels, and semantic ref.

## Biology Metaphor Ownership

Move these into `draxul-biology-metaphor`:

- `biology_builder.*`;
- biology layout records: tissue, cell, organelle, fibre;
- biology-specific colors/material choices;
- biology-specific filters and controls;
- biology picking and tooltip data;
- later animation state for pulses/flows.

Biology should not depend on city layout, `SemanticMegacityModel`, `CityGrid`, city route generation, signs, or building metrics.

The initial biology output can be simple:

- module -> tissue region;
- file -> cell;
- type/class/struct -> nucleus or large organelle;
- function/method -> smaller organelle/process body;
- field -> vesicle/storage body;
- semantic reference/inheritance/include edge -> fibre/tube.

## Shared Host Ownership

Move host-level shared behavior into `draxul-codeviz-host`:

- scan root and `CodebaseScanner` lifecycle;
- `CodeSemanticSnapshot` publication;
- semantic generation tracking;
- common camera/input state;
- common selection by semantic node/edge ID;
- common scan/progress UI;
- config document loading/saving dispatch;
- host factory glue for `--host megacity` and `--host bioview`.

Keep metaphor-specific workers in the metaphor:

- city grid/path workers stay city-owned;
- biology layout/animation workers stay biology-owned;
- the host only swaps immutable scene snapshots into the shared render pass.

## Config Split

Break `MegaCityCodeConfig` into:

- `CodeVizRenderConfig`: projection mode, camera state, lighting, AO, debug views, wireframe, tone mapping, label fade, continuous refresh.
- `CodeVizAnalysisConfig`: source root, selected module/common semantic filters if they are truly shared.
- `MegaCityMetaphorConfig`: building dimensions, road widths, parks, signs, city routes, struct stacking, function bundling.
- `BiologyMetaphorConfig`: tissue spacing, cell sizing, organelle mapping, fibre thickness, translucency, animation settings.

Keep backward compatibility by reading the existing `[mega_city_code]` keys during the transition. New writes can either keep the old table until the split is complete or write both old and new tables for one migration window.

## Migration Phases

### Phase 1: Freeze Behavior With Tests

Add or confirm tests for:

- `CodeSemanticSnapshot` module/file/type/function/field edges;
- existing city building counts and layout bounds for a small fixture;
- city route endpoints for a small dependency fixture;
- biology object counts for a small fixture;
- shared scene snapshot object counts and semantic refs.

Exit criterion: extraction failures are distinguishable from intended behavior changes.

### Phase 2: Introduce Neutral Scene Names In Place

Inside current `draxul-megacity`, rename and reshape without moving targets first:

- `SceneSnapshot` -> `CodeVizSceneSnapshot`;
- `SceneObject` -> `CodeVizRenderable`;
- `SceneWorld` -> `CodeVizSceneWorld` or replace with a simpler snapshot builder API;
- `SourceSymbol` -> `CodeVizSemanticRef`;
- `CityInputState` -> `CodeVizInputState`;
- city-shaped render roles become generic tags or metadata.

Keep compatibility aliases briefly if needed to reduce churn.

Exit criterion: shared scene/render files no longer expose city or biology names in public structs.

### Phase 3: Split Config And UI Panels

Extract config sections and UI panels while behavior remains unchanged:

- common render controls;
- common analysis tree/progress panel;
- city controls;
- biology controls.

Exit criterion: Biology mode no longer shows city/building/road/sign sliders.

### Phase 4: Extract `draxul-codeviz-scene`

Move neutral scene records and snapshot building helpers into a new static library.

Likely files after cleanup:

- `codeviz_scene_types.h`;
- `codeviz_scene_world.h/.cpp` if an ECS remains useful;
- `codeviz_scene_snapshot_builder.h/.cpp`;
- `codeviz_camera.h/.cpp`;
- `codeviz_input_state.h/.cpp`.

Exit criterion: this library builds without city, biology, ImGui, Vulkan, or Metal headers.

### Phase 5: Extract `draxul-codeviz-renderer`

Move shared render implementation behind the neutral scene API:

- rename render files from `megacity_render_*` to `codeviz_render_*`;
- renderer consumes neutral mesh/material handles;
- renderer material slots come from snapshot material definitions rather than city enum values;
- both Vulkan and Metal paths remain behaviorally aligned.

Exit criterion: `draxul-codeviz-renderer` does not include city or biology metaphor headers.

### Phase 6: Extract City Metaphor

Move city projection/build/layout/UI into `draxul-megacity-metaphor`.

Replace direct city scene component creation with neutral scene emission:

```text
CodeSemanticSnapshot
  -> CityPresentation
  -> CodeVizSceneSnapshot
```

Exit criterion: city behavior is still available through `--host megacity`, and all city-specific names live in the city metaphor target.

### Phase 7: Extract Biology Metaphor

Move biology projection/build/layout/UI into `draxul-biology-metaphor`.

Replace any city scene reuse with neutral scene emission:

```text
CodeSemanticSnapshot
  -> BiologyPresentation
  -> CodeVizSceneSnapshot
```

Exit criterion: biology behavior is available through `--host bioview`, with no dependency on city metaphor headers.

### Phase 8: Clean Geometry Boundaries

Refactor `draxul-geometry` so it exposes generic procedural building blocks:

- generic prism/ring/box helpers;
- generic label-board or card helpers;
- generic tube/path helpers;
- generic ellipsoid/shell helpers.

Move metaphor-specific parameter naming and cache keys into the metaphor targets.

Exit criterion: `draxul-geometry` remains a reusable procedural geometry library, not a city mesh library.

### Phase 9: Final Dependency Audit

Run targeted include and link checks:

```powershell
rg -n "Building|Road|City|Megacity|Biology|Cell|Organelle" modules/megacity/draxul-codeviz-scene modules/megacity/draxul-codeviz-renderer
rg -n "Vulkan|Metal|ImGui|IHost" modules/megacity/draxul-code-semantics modules/megacity/draxul-geometry
```

Configure and build both:

```powershell
cmake --build build --config Release --target draxul draxul-tests
py do.py smoke
ctest --test-dir build --build-config Release --output-on-failure

cmake -S . -B build-no-megacity -G "Visual Studio 17 2022" -A x64 -DDRAXUL_ENABLE_MEGACITY=OFF
cmake --build build-no-megacity --config Release --target draxul
```

Exit criterion: optional-module isolation still works, both hosts run, and renderer files are shared by both metaphors.

## Suggested First Work Item

Start with Phase 2 and Phase 3, not CMake target extraction.

Reason: renaming the scene contract and splitting config/UI inside the current target proves the real boundary with less build churn. Once the headers stop leaking city vocabulary, moving files into clean libraries becomes mostly mechanical.

First work item scope:

1. introduce `CodeVizSceneSnapshot`, `CodeVizRenderable`, and `CodeVizSemanticRef`;
2. adapt city and biology builders to populate those neutral records;
3. split common render config from city metaphor config;
4. hide city controls in Biology mode through separate panel functions;
5. keep the Vulkan/Metal renderer behavior unchanged.

## Completion Criteria

The cleanup is complete when:

- parser and semantic targets contain no metaphor vocabulary;
- city and biology are separate libraries and do not include each other;
- shared scene/render libraries contain no city or biology vocabulary;
- the shared renderer draws both metaphors from the same neutral snapshot type;
- `draxul-geometry` is reusable procedural geometry with no renderer or host dependency;
- `--host megacity` and `--host bioview` both work;
- `DRAXUL_ENABLE_MEGACITY=OFF` still removes all optional code-visualization dependencies;
- Windows/Vulkan and macOS/Metal render paths remain behaviorally aligned.
