# Megacity Static Mesh Family Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce megacity mesh upload size by reusing one uploaded mesh per normalized building/sign/sidewalk family and scaling it per object.

**Architecture:** Keep the existing `SceneWorld -> SceneSnapshot -> renderer` flow, but stop baking footprint, total height, and module color into every generated mesh. Add a static mesh family cache that emits unit-scale meshes keyed by shape/ring pattern, and extend `CustomMeshRef` with a transform mode so the snapshot builder scales reusable meshes from `BuildingMetrics` or `SignMetrics`.

**Tech Stack:** C++20, GLM, EnTT, Catch2, existing Vulkan/Metal custom mesh rendering path.

---

## Current Problem

`city_builder.cpp` currently generates a fresh `GeometryMesh` for each building, cap, roof sign, and sidewalk ring. Those meshes bake in dimensions and colors, so `scene_snapshot_builder.cpp` can only dedupe by pointer identity when the same pointer is reused. On large corpora this produces tens of thousands of custom meshes and a very large vertex upload.

The first implementation slice will still use `SceneSnapshot::custom_meshes`, but the contents become static mesh families:

- building rings are generated at footprint `1.0` and total height `1.0`;
- roof sign rings are generated at outer radius `0.5` and height `1.0`;
- sidewalk rings are generated at outer radius `0.5` and height `1.0`;
- object transforms scale those unit meshes to the actual building/sign dimensions;
- object color carries module/sign color where possible.

This gives the renderer one uploaded mesh for each reusable family in the current scene without adding a new shader or instance-buffer path.

## File Changes

- Add `modules/megacity/draxul-megacity/src/static_mesh_family_cache.h`
- Add `modules/megacity/draxul-megacity/src/static_mesh_family_cache.cpp`
- Add `tests/megacity_static_mesh_family_tests.cpp`
- Modify `modules/megacity/draxul-megacity/CMakeLists.txt`
- Modify `modules/megacity/draxul-megacity/src/scene_components.h`
- Modify `modules/megacity/draxul-megacity/src/scene_world.h`
- Modify `modules/megacity/draxul-megacity/src/scene_world.cpp`
- Modify `modules/megacity/draxul-megacity/src/scene_snapshot_builder.cpp`
- Modify `modules/megacity/draxul-megacity/src/city_builder.cpp`

## Task 1: Static Mesh Family Cache

**Files:**

- Create: `modules/megacity/draxul-megacity/src/static_mesh_family_cache.h`
- Create: `modules/megacity/draxul-megacity/src/static_mesh_family_cache.cpp`
- Test: `tests/megacity_static_mesh_family_tests.cpp`

- [x] **Step 1: Write failing cache tests**

Tests cover pointer reuse, distinct keys, and unit-scale bounds for generated meshes.

- [x] **Step 2: Implement cache API**

Create:

```cpp
namespace draxul
{

struct StaticBuildingLayerSpec
{
    float height_fraction = 1.0f;
    float color_multiplier = 1.0f;
    uint32_t layer_id = 0;
};

struct StaticBuildingRingSpec
{
    int sides = 4;
    float middle_strip_scale = 1.0f;
    float level_gap_fraction = 0.0f;
    std::vector<StaticBuildingLayerSpec> layers;
};

struct StaticBrickStackSpec
{
    int grid_size = 2;
    float brick_gap_fraction = 0.0f;
    float floor_gap_fraction = 0.0f;
    std::vector<StaticBuildingLayerSpec> bricks;
};

struct StaticRoofSignRingSpec
{
    int sides = 4;
    float inner_radius_fraction = 0.45f;
};

struct StaticSidewalkRingSpec
{
    int sides = 4;
    float inner_radius_fraction = 0.45f;
    glm::vec3 color{ 1.0f };
};

class StaticMeshFamilyCache
{
public:
    [[nodiscard]] std::shared_ptr<const GeometryMesh> building_ring(const StaticBuildingRingSpec& spec);
    [[nodiscard]] std::shared_ptr<const GeometryMesh> brick_stack(const StaticBrickStackSpec& spec);
    [[nodiscard]] std::shared_ptr<const GeometryMesh> building_cap(int sides, float middle_strip_scale, uint32_t layer_id);
    [[nodiscard]] std::shared_ptr<const GeometryMesh> roof_sign_ring(const StaticRoofSignRingSpec& spec);
    [[nodiscard]] std::shared_ptr<const GeometryMesh> sidewalk_ring(const StaticSidewalkRingSpec& spec);
    [[nodiscard]] size_t size() const;
    void clear();
};

} // namespace draxul
```

Implementation details:

- Use separate `std::unordered_map` containers per family.
- Quantize float fields to integer keys with millimetre precision.
- Generate meshes only on cache miss.
- Generate building meshes with `DraxulBuildingParams::footprint = 1.0f`.
- Generate roof sign meshes with outer radius `0.5f` and height `1.0f`.
- Generate sidewalk ring meshes with outer radius `0.5f`, `y = 0.0f`, and height `1.0f`.
- Use white or grayscale vertex colors for building families so `SceneObject::color` can tint by module/sign color.

- [x] **Step 3: Wire CMake**

Add `src/static_mesh_family_cache.cpp` to both Apple and non-Apple `MEGACITY_SOURCES` lists in `modules/megacity/draxul-megacity/CMakeLists.txt`.

## Task 2: Scaled Custom Mesh References

**Files:**

- Modify: `modules/megacity/draxul-megacity/src/scene_components.h`
- Modify: `modules/megacity/draxul-megacity/src/scene_world.h`
- Modify: `modules/megacity/draxul-megacity/src/scene_world.cpp`
- Modify: `modules/megacity/draxul-megacity/src/scene_snapshot_builder.cpp`
- Test: `tests/megacity_static_mesh_family_tests.cpp`

- [x] **Step 1: Add transform mode**

Add:

```cpp
enum class CustomMeshTransformMode : uint8_t
{
    Baked,
    ScaleByBuildingMetrics,
    ScaleBySignMetrics,
};

struct CustomMeshRef
{
    std::shared_ptr<const GeometryMesh> mesh;
    CustomMeshTransformMode transform_mode = CustomMeshTransformMode::Baked;
};
```

- [x] **Step 2: Pass transform mode through SceneWorld**

Extend `SceneWorld::create_building()` and `SceneWorld::create_sign()` with a final defaulted parameter:

```cpp
CustomMeshTransformMode custom_mesh_transform_mode = CustomMeshTransformMode::Baked
```

When a custom mesh is supplied, emplace `CustomMeshRef` with both the mesh and transform mode.

- [x] **Step 3: Scale reusable meshes in snapshots**

In `scene_snapshot_builder.cpp`, for `ScaleByBuildingMetrics`, apply:

```cpp
transform = glm::scale(transform, glm::vec3(bm->footprint, bm->height, bm->footprint));
```

For `ScaleBySignMetrics`, apply:

```cpp
transform = glm::rotate(transform, sm->yaw_radians, glm::vec3(0.0f, 1.0f, 0.0f));
transform = glm::scale(transform, glm::vec3(sm->width, sm->height, sm->width));
```

The `Baked` mode keeps the old behavior.

## Task 3: Use Static Families In City Builder

**Files:**

- Modify: `modules/megacity/draxul-megacity/src/city_builder.cpp`

- [x] **Step 1: Add one cache per city build**

Create `StaticMeshFamilyCache static_meshes;` before the module/building loops.

- [x] **Step 2: Normalize building ring specs**

For regular buildings:

- compute total height as positive layer heights plus gaps;
- store layer heights as fractions of total height;
- store level gap as a fraction of total height;
- use `color_multiplier = 1.0f` for normal layers;
- use `color_multiplier = 1.0f - config.building_alternate_darkening` for dark alternating layers;
- pass `module_color` as the `SceneWorld::create_building()` color;
- pass `CustomMeshTransformMode::ScaleByBuildingMetrics`.

- [x] **Step 3: Normalize struct stack specs**

For brick stacks:

- compute total stack height from floor maximums and floor gaps;
- store brick heights as fractions of total stack height;
- store brick gap as `config.struct_brick_gap / building.metrics.footprint`;
- store floor gap as a fraction of total stack height;
- pass `module_color` as object color;
- pass `CustomMeshTransformMode::ScaleByBuildingMetrics`.

- [x] **Step 4: Normalize cap/sign/sidewalk meshes**

- caps use `static_meshes.building_cap(building_side_count, middle_strip_scale, top_layer_id)`, object color `cap_color`, and `ScaleByBuildingMetrics`;
- roof signs use `static_meshes.roof_sign_ring(StaticRoofSignRingSpec{ sides, inner_radius_fraction })`, object color `sign_board`, and `ScaleBySignMetrics`;
- sidewalks use `static_meshes.sidewalk_ring(StaticSidewalkRingSpec{ sides, inner_radius_fraction, color })`, existing sidewalk color/material, and `ScaleByBuildingMetrics`.

- [x] **Step 5: Log retained static families**

Add:

```cpp
DRAXUL_LOG_INFO(LogCategory::App,
    "CityBuilder: static mesh family cache retained %zu reusable meshes",
    static_meshes.size());
```

## Task 4: Verification

- [x] Build tests:

```powershell
cmake --build build --config Release --target draxul-tests
```

- [x] Run focused tests:

```powershell
D:\dev\Draxul\build\tests\Release\draxul-tests.exe "[megacity][static-mesh-family]"
```

- [x] Run megacity tests:

```powershell
D:\dev\Draxul\build\tests\Release\draxul-tests.exe "[megacity]"
```

- [x] Run CTest:

```powershell
ctest --test-dir build --build-config Release -R draxul-tests --output-on-failure
```

- [x] Run diff check:

```powershell
git diff --check
```

## Follow-Up

This implementation still uses `SceneSnapshot::custom_meshes` as the transport to Vulkan/Metal. The geometry inside it is static-family geometry instead of per-building baked geometry. A later renderer-specific step can move these families into a persistent renderer-owned mesh library and draw them with instance buffers, which would avoid rebuilding `custom_meshes` even when snapshots are recreated.
