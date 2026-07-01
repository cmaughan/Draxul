# Semantic Code Visualization Separation Plan

> Refinement note, July 2026: the intended direction is now to keep one shared
> backend-neutral scene/render path and split only the city and biology
> metaphor builders. See
> `plans/design/shared-code-visualization-renderer-module-split-plan.md` for
> the current module-split plan.

## Goal

Keep Draxul's existing Tree-sitter analysis, but make its output independent of any visual metaphor.

The same immutable semantic code snapshot should be consumable by:

- the existing city visualization;
- a new biological visualization using cells, organelles, nerves, vessels, and signals;
- future visualizations without changing the parser or losing semantic detail.

The city must become one presentation of the code model, not part of the code model itself.

## Current State

The current pipeline is:

```text
Tree-sitter CodebaseSnapshot
  -> CodeSemanticSnapshot
  -> City projection (Megacity-local CityClassRecord / CityDependencyRecord)
  -> SemanticMegacityModel
  -> SemanticMegacityLayout + CityGrid + routes
  -> SceneWorld
  -> SceneSnapshot
  -> Vulkan or Metal MegaCity renderer
```

The parser boundary is now separate from the shared semantic model, while city presentation records are local to `draxul-megacity`:

- `CodeSemanticSnapshot` preserves repository, module, file, type, function, method, field, include, inheritance, include, and type-reference facts without city/building vocabulary.
- `city_builder.cpp` assigns roles such as `building`, `tower`, `block`, and `tree`.
- Megacity-local `CityClassRecord` stores derived city properties such as `road_size` and aggregated building functions.
- methods remain first-class semantic nodes, then the city projection folds them into a type's building layers;
- `SemanticMegacityModel` combines semantic facts with building metrics and city filtering;
- `MegaCityHost` owns scanning, semantic snapshot publication, presentation selection, city layout, routes, selection, UI, scene creation, and rendering;
- `SceneWorld` and `SceneSnapshot` are specialized for buildings, roads, signs, and city materials.

BioView now consumes `CodeSemanticSnapshot` directly instead of reinterpreting city records. The remaining split is to rename legacy city record/layout type names when that churn is worthwhile.

## Design Principle

Use four explicit layers:

```text
1. Parse               2. Understand              3. Present                 4. Render

Tree-sitter       ->   CodeSemanticSnapshot  ->   CityPresentation      ->  CitySceneSnapshot
                                                or BiologyPresentation  ->  BiologySceneSnapshot
```

Only layer 2 is shared semantic truth. Layers 3 and 4 belong to a metaphor.

Do not call layer 2 a "view" in code. Use `CodeSemanticSnapshot` or `SemanticCodeModel`; reserve "view" and "presentation" for visual interpretations.

## Target Dependency Shape

Keep the work inside the optional module initially, so `DRAXUL_ENABLE_MEGACITY=OFF` continues to remove all code-visualization dependencies.

```text
draxul-treesitter
        |
        v
draxul-code-semantics
        |
        +----------------------+----------------------+
        |                      |                      |
        v                      v                      v
draxul-codeviz-common   draxul-city-view       draxul-biology-view
  scan/session            city projection         biology projection
  filters/selection       city scene builder       biology scene builder
  metrics bridge          city render pass         biology render pass
        |                      |                      |
        +----------------------+----------------------+
                               |
                               v
                    host provider composition
```

Recommended initial library changes:

| Current | Target responsibility |
|---|---|
| `draxul-treesitter` | Parsing only: files, source spans, raw symbols, fields, inheritance, and parse errors |
| `draxul-code-semantics` | Build the canonical semantic snapshot and indexes |
| `draxul-megacity` | Split into common code-visualization orchestration and city-specific presentation/rendering |
| new `draxul-biology-view` | Biological projection, layout, scene building, picking, UI, and backend rendering |

Do not rename `modules/megacity/` or the `DRAXUL_ENABLE_MEGACITY` option during the first extraction. That is high-churn packaging work and does not prove the boundary. Once both views work, a separate cleanup can rename the umbrella to `modules/codeviz/` and introduce `DRAXUL_ENABLE_CODEVIZ`, retaining the old option as a compatibility alias for one release.

## Canonical Semantic Snapshot

Replace the query-shaped `ICitySemanticSource` API with one immutable snapshot. Tree-sitter is currently the only source, so a concrete pure builder is preferable to a source interface that implies unsupported implementations.

Suggested shape:

```cpp
using SemanticNodeId = uint64_t;
using SemanticEdgeId = uint64_t;

enum class SemanticNodeKind
{
    Repository,
    Module,
    Directory,
    File,
    Type,
    Function,
    Method,
    Field,
};

enum class SemanticEdgeKind
{
    Contains,
    Inherits,
    ReferencesType,
    Includes,
    Calls,
};

struct SourceSpan
{
    std::string file_path;
    uint32_t start_line = 0;
    uint32_t end_line = 0;
};

struct SemanticNode
{
    SemanticNodeId id = 0;
    SemanticNodeId parent_id = 0;
    SemanticNodeKind kind = SemanticNodeKind::File;
    std::string name;
    std::string qualified_name;
    SourceSpan source;
    SemanticMetrics metrics;
    SemanticFlags flags;
};

struct SemanticEdge
{
    SemanticEdgeId id = 0;
    SemanticNodeId source_id = 0;
    SemanticNodeId target_id = 0;
    SemanticEdgeKind kind = SemanticEdgeKind::ReferencesType;
    std::string label;
};

struct CodeSemanticSnapshot
{
    uint64_t generation = 0;
    std::vector<SemanticNode> nodes;
    std::vector<SemanticEdge> edges;
    SemanticIndexes indexes;
    CodebaseHealthMetrics health;
};
```

The exact storage can remain data-oriented, but it must preserve these properties:

- stable identity independent of the active visualization;
- explicit repository/module/file/symbol hierarchy;
- methods and fields as first-class records, not building attributes;
- typed relationships rather than a city-specific dependency route;
- source spans sufficient for navigation and tooltips;
- deterministic ordering and deterministic IDs for identical input;
- immutable publication through `shared_ptr<const CodeSemanticSnapshot>`;
- indexes built once for lookup by ID, qualified name, file, module, and parent.

Stable IDs should be derived from normalized source path, semantic kind, qualified name, and source location. Views may create their own presentation IDs, but every selectable presentation object must retain its originating `SemanticNodeId` or `SemanticEdgeId`.

### Remove From The Semantic Layer

These concepts belong to city presentation and must not survive the extraction into `draxul-code-semantics`:

- building, tower, block, and tree roles;
- footprint, height, road width, road size, parks, signs, and routes;
- struct stacking and function bundling;
- city visibility filters such as hiding structs or free-function entities;
- city colors, materials, camera settings, and mesh choices.

Generic facts such as line count, field count, method count, abstractness, test-source classification, complexity, coupling, and coverage are valid semantic metrics. A view decides how those facts affect shape, size, color, activity, or visibility.

## Shared Session And State

Extract the scanner lifecycle from `MegaCityHost` into a small `CodeAnalysisSession` owned by the common code-visualization host layer.

It should own:

- scan root and `CodebaseScanner` lifecycle;
- progress and error reporting;
- conversion from `CodebaseSnapshot` to `CodeSemanticSnapshot`;
- atomic publication of the latest immutable snapshot;
- semantic snapshot generation numbers and cancellation;
- common filters that operate on semantic facts;
- live performance and coverage data keyed by `SemanticNodeId`;
- current hover and selection as semantic IDs.

It should not own layout, camera, scene entities, materials, picking geometry, or metaphor-specific UI.

Use one common selection record:

```cpp
struct SemanticSelection
{
    std::optional<SemanticNodeId> node;
    std::optional<SemanticEdgeId> edge;
};
```

This makes selection portable across visualizations. Selecting a class in the city can select the corresponding nucleus or organelle in biology without matching strings.

## Metaphor Boundary

Each metaphor gets two pure transformations and one rendering component:

```text
CodeSemanticSnapshot
  -> metaphor projection
  -> metaphor presentation/layout
  -> scene builder
  -> backend-neutral scene snapshot
  -> metaphor render pass
```

### City View

Move these responsibilities behind a city-specific boundary:

- `SemanticMegacityModel` and all building-layer derivation;
- `SemanticMegacityLayout`;
- `CityGrid`, roads, sidewalks, parks, signs, and dependency routing;
- city scene ECS/components and scene snapshot building;
- city picking and building tooltips;
- city configuration and city renderer UI;
- Vulkan and Metal city render-pass implementations.

The pure entry point should become conceptually:

```cpp
CityPresentation build_city_presentation(
    const CodeSemanticSnapshot& semantics,
    const CityViewConfig& config);
```

`CityPresentation` may retain city-specific building models, layouts, and routes. The key constraint is that it cannot reach back into Tree-sitter records or scanner state.

### Biological View

Start with a deterministic, readable mapping:

| Semantic concept | Initial biological representation |
|---|---|
| repository | organism or specimen |
| module | organ or tissue region |
| source file | cell |
| type/class/struct | nucleus or large organelle inside its file cell |
| method/function | smaller organelle or active process body |
| field/data member | vesicle or storage body |
| containment | physical nesting |
| reference/call relationship | nerve or signal fibre |
| data/type dependency | vessel or cytoskeleton connection |
| coverage/performance | pulse, flow, brightness, or metabolic activity |

Treat this table as a starting mapping, not semantic truth. Keep it in the biology projector/configuration so it can evolve without parser changes.

The current Tree-sitter snapshot does not contain a complete function call graph. It has functions and methods, fields, referenced types, inheritance, and include symbols. The first biological view must therefore label fibres according to the relationships actually available, such as type references and inheritance. Showing nerves as call flow requires a later analyzer extension that emits real `Calls` edges into the same semantic snapshot; it does not require a biology-renderer redesign.

The first biological presentation should favor legibility over simulation:

- modules form separated tissue regions;
- files become cells with stable positions;
- symbols are nested inside their owning file;
- relationships become curved fibres/tubes;
- selection and filtering work before animation is added;
- performance/coverage may modulate emissive pulses only after static layout is stable.

Do not reuse `SceneWorld` merely because it already exists. Its API is explicitly buildings, roads, signs, and XZ ground placement. Biology will need spheres or membranes, tubes/curves, translucency, nesting, and possibly animation. Reuse lower-level geometry, camera helpers, render-target infrastructure, and material conventions where they fit; give biology its own scene model.

## Visualizer Contract

Avoid creating a universal scene graph. The city and biology renderers have different primitive and update requirements.

Use a narrow orchestration interface for built-in metaphors:

```cpp
class ICodeVisualization
{
public:
    virtual ~ICodeVisualization() = default;
    virtual std::string_view id() const = 0;
    virtual void set_semantic_snapshot(
        std::shared_ptr<const CodeSemanticSnapshot> snapshot) = 0;
    virtual void set_selection(const SemanticSelection& selection) = 0;
    virtual SemanticSelection selection() const = 0;
    virtual void pump(float dt) = 0;
    virtual void draw(IFrameContext& frame) = 0;
    virtual void draw_ui() = 0;
    virtual bool handle_input(const CodeVisualizationInput& input) = 0;
};
```

This interface coordinates lifecycle and shared identity only. It must not expose buildings, cells, meshes, or a lowest-common-denominator `SceneObject`.

For two built-in visualizations, use an enum and factory rather than a plugin registry. Introduce dynamic registration only if an independently built third-party visualization becomes a real requirement.

## Host And CLI Strategy

Preserve existing behavior while the split is introduced:

- `--host megacity` creates the common analysis host with the city visualization selected;
- add `--host bioview` when the biological MVP is usable;
- both hosts use the same analysis/session code and semantic snapshot contract;
- internally, both can be thin factories around a shared `CodeVisualizationHost` plus a visualization kind;
- runtime switching can be added later because stable semantic IDs make it cheap, but it is not required for the initial extraction.

This retains existing scripts and configuration while making the biological view a first-class product rather than a mode hidden inside city code.

Split configuration into:

- `[code_analysis]`: source root, semantic filters, parser-independent analysis options;
- `[code_visualization]`: common selection and optional shared interaction preferences;
- `[mega_city_code]`: existing city layout, camera, materials, routes, and lighting;
- `[bio_view]`: biology mapping, layout, camera, materials, fibres, and animation.

Keep reading existing `[mega_city_code]` values so current users do not lose settings.

## Implementation Phases

### Phase 0: Characterize Existing Behavior

Before moving types:

1. Add focused fixtures for the current Tree-sitter projection: modules, files, classes, structs, methods, fields, inheritance, and dependencies.
2. Add deterministic city presentation tests that capture building counts, module assignments, layout bounds, and route endpoints for a small fixture.
3. Record a render smoke reference for the existing city.

Exit criterion: refactoring failures can be distinguished from deliberate semantic improvements.

### Phase 1: Introduce The Neutral Semantic Snapshot

1. Add `draxul-code-semantics` and the neutral node/edge/snapshot types.
2. Implement `build_code_semantic_snapshot(const CodebaseSnapshot&)` as a pure transformation.
3. Preserve methods, fields, containment, inheritance, and type references as first-class data.
4. Add stable IDs and lookup indexes.
5. Done: the city now builds from `CodeSemanticSnapshot` directly; `ICitySemanticSource` and `TreeSitterSemanticSource` were removed rather than kept as an adapter.

Exit criterion: the existing city renders through the compatibility adapter, and semantic tests contain no city vocabulary.

### Phase 2: Make City Projection Explicit

1. Done: city role assignment moved from `TreeSitterSemanticSource` into the city builder's semantic projection.
2. Make city filtering, function bundling, struct stacking, and metric-to-building conversion part of `CityViewConfig` and the city projector.
3. Change city dependencies to consume semantic edge IDs.
4. Replace string-based `SourceSymbol` identity with semantic IDs plus display metadata.
5. Done: `ICitySemanticSource`, `TreeSitterSemanticSource`, and `CityModuleRecord` are deleted. `CityClassRecord` and `CityDependencyRecord` are local city-projection records inside `draxul-megacity`.

Exit criterion: `draxul-code-semantics` has no include or symbol containing `City`, `Building`, `Road`, or `MegaCity`.

### Phase 3: Extract Common Host Orchestration

1. Move scanner lifecycle and immutable snapshot publication out of `MegaCityHost` into `CodeAnalysisSession`.
2. Move common filters, progress UI, coverage/performance lookup, and semantic selection into `draxul-codeviz-common`.
3. Make `MegaCityHost` a thin composition of common session plus city visualization.
4. Keep city route workers and city grid workers inside the city visualization.
5. Add generation/cancellation checks so stale presentation builds cannot replace a newer semantic snapshot.

Exit criterion: common code can run a scan and report/select semantic nodes without constructing city layout or city renderer state.

### Phase 4: Separate City Scene And Renderer

1. Move city scene components, snapshot types, materials, picking, and render passes into `draxul-city-view`.
2. Rename generic-looking city types such as `SceneWorld` and `SceneSnapshot` to `CitySceneWorld` and `CitySceneSnapshot`.
3. Keep backend-neutral CPU scene data shared between the city Vulkan and Metal implementations.
4. Extract only genuinely neutral helpers; do not force biology through city material or primitive enums.

Exit criterion: the city view depends on code semantics, while code semantics and common orchestration do not depend on city scene types.

### Phase 5: Build The Biological MVP

1. Add a pure `build_biology_presentation` with deterministic tissue, cell, organelle, and fibre placement.
2. Add `BiologySceneSnapshot` and matching Vulkan/Metal render passes.
3. Implement camera, picking, semantic selection, filtering, and tooltips.
4. Register `--host bioview`.
5. Reuse coverage/performance snapshots by semantic ID.

Exit criterion: the same source tree can be opened in city and biological hosts, and selecting the same semantic ID identifies the same source symbol in both.

### Phase 6: Add Biological Motion Carefully

After the static view is readable and stable:

1. animate signal pulses along call/reference fibres;
2. use coverage or performance data to drive activity rather than arbitrary motion;
3. keep topology and geometry rebuilds separate from per-frame animation data;
4. publish animation state without rebuilding the full semantic or presentation snapshot.

Exit criterion: animation does not alter semantic identity, cause layout jitter, or trigger full geometry uploads each frame.

### Phase 7: Packaging Cleanup

Only after both views are established:

1. decide whether `modules/megacity/` should become `modules/codeviz/`;
2. introduce `DRAXUL_ENABLE_CODEVIZ` and preserve `DRAXUL_ENABLE_MEGACITY` as a compatibility alias;
3. update `docs/features.md`, build presets, CI, and nested `AGENTS.md` guidance;
4. remove transitional target aliases and adapters.

## Testing And Validation

### Semantic tests

- deterministic IDs and ordering;
- module/file/symbol containment;
- first-class methods and fields;
- inheritance and typed reference edges;
- duplicate names in different files or modules;
- unresolved and ambiguous references;
- test-source classification and aggregate health metrics;
- immutable snapshot publication and generation handling.

### City regression tests

- existing fixture produces equivalent buildings, layout bounds, and routes;
- city filters still affect presentation, not semantic snapshot contents;
- city selection resolves to a semantic ID and source span;
- Vulkan and Metal render smoke references remain valid or are deliberately re-blessed.

### Biology tests

- deterministic layout for identical input;
- every visible object maps to a semantic node or edge;
- containment keeps organelles inside their owning cells;
- fibres connect the intended endpoints;
- filter and selection parity with city view;
- bounded scene build time and GPU upload size for large repositories;
- Vulkan and Metal render smoke coverage.

### Build boundary tests

- `DRAXUL_ENABLE_MEGACITY=OFF` still removes the entire optional feature;
- the terminal application has no source-level dependency on code-visualization headers;
- semantic tests run without creating a window or GPU device;
- each metaphor's presentation tests run without a renderer.

## Risks And Controls

| Risk | Control |
|---|---|
| A generic model becomes a dump of every parser detail | Define the semantic schema around identity, hierarchy, relationships, source spans, and metrics; retain raw parser data only when a consumer needs it |
| The extraction changes the existing city | Keep a temporary adapter and characterization fixtures until city projection is fully migrated |
| A universal renderer weakens both metaphors | Share lifecycle and identity, not scene primitives; keep separate city and biology scene snapshots/render passes |
| String matching continues to leak across layers | Require semantic IDs on every selectable presentation object and relationship |
| Runtime view switching complicates the first delivery | Ship separate host aliases first; add switching after both visualizers have stable lifecycle behavior |
| Biological animation causes constant rebuilds | Separate semantic, presentation, geometry, and per-frame animation generations |
| The optional module is renamed too early | Defer outer directory and build-option cleanup until both views prove the new ownership model |

## Recommended First Work Item

Implement only Phases 0 and 1 first:

1. add characterization fixtures;
2. introduce `CodeSemanticSnapshot` with stable node and edge IDs;
3. build it directly from `CodebaseSnapshot`;
4. adapt it back to the existing city records;
5. leave all city rendering and host behavior unchanged.

This is the smallest change that validates the central architecture. Once the existing city consumes the compatibility adapter, the biological view can be designed against real neutral data while the city extraction continues independently.

## Completion Criteria

The separation is complete when:

- Tree-sitter analysis produces one metaphor-neutral immutable semantic snapshot;
- the snapshot contains no city or biology vocabulary;
- city and biology each perform their own explicit projection and layout;
- both presentations use stable semantic IDs for source navigation, selection, filtering, coverage, and performance;
- neither visualization depends on the other's scene types, configuration, or renderer;
- parsing and semantic-model tests run without a host, window, or GPU;
- city behavior remains available through `--host megacity` on Windows/Vulkan and macOS/Metal;
- biology is available through its own host on both supported renderers.
