# 113 — Stacked Struct Plates

## Summary

Compress space by stacking same-footprint struct buildings vertically in groups of up to N (configurable, default 10). Each struct becomes a thin "plate" with visible gaps between them, creating a distinct silhouette from class buildings (solid towers) and function bundles (triangles). Each plate retains its sign label and remains independently clickable with full dependency routing.

## Motivation

Struct-heavy codebases produce many small buildings of similar size that spread across the module, consuming layout area without adding much visual information. Stacking them compresses the footprint while preserving identity — you can still see, click, and trace dependencies for each individual struct.

## Visual Design

- Structs grouped by matching `base_size` within a module, up to N per stack
- Each plate has a visible vertical gap above it (configurable `struct_stack_gap`)
- The gap between plates makes them visually distinct from solid class buildings
- Sign bands on each plate reinforce the "stacked rings" look
- Three distinct building silhouettes: solid towers (classes), triangular bundles (functions), stacked plates (structs)

## Configuration

- `struct_stack_max` — max structs per stack (default 10)
- `struct_stack_gap` — vertical gap between plates (default ~0.1 or tuned to look right)
- `enable_struct_stacking` — toggle on/off (default true)

## Implementation Notes

### Layout (semantic_city_layout.cpp)

- After building placement, group structs with `is_struct == true` and same `base_size` within each module
- Create a synthetic "stack" entry that occupies a single lot — similar to function bundling
- Each struct in the stack becomes a layer at a computed elevation offset
- The stack's total lot footprint is just one building's footprint (that's the whole point)

### Geometry (city_builder.cpp)

- Emit each plate as a separate building entity at its stacked elevation
- Add the gap between plates
- Each plate gets its own sign/label
- Cap/roof only on the topmost plate

### Picking (city_picking.cpp / megacity_host.cpp)

- Pick ray must resolve which plate was hit — the vertical gaps make this easier than function layers since plates are spatially separated
- Reuse the `hit_y` → layer index resolution pattern from function bundles

### Dependency Routing

- Each plate is still an independent entity for `connected_building_identities` and `collect_route_pairs`
- Route ports exit at the plate's elevation — reuse the `function_layer_elev` machinery from function bundles
- Vertical drop segments already work for routes connecting to plates at different heights
- No database changes needed — this is purely a presentation-layer grouping

### Key Constraint

- The stack is a visual compression only — no synthetic entities in the model
- Each struct retains its own `qualified_name`, `source_file_path`, etc.
- Clicking a plate selects that individual struct, not the stack
- Dependencies connect to the specific struct plate, not the stack as a whole

## Tasks

- [x] Add config fields: `struct_stack_max`, `struct_stack_gap`, `enable_struct_stacking`
- [x] Group same-footprint structs in `build_semantic_city_model` into stack entries
- [x] Emit stacked plate geometry with gaps in `city_builder.cpp`
- [x] Sign labels on each plate
- [x] Pick resolution for stacked plates
- [x] Route port elevation for stacked plate entities
- [x] ImGui toggle and sliders for stack settings
- [ ] Tests for stacking layout and picking
