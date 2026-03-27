# City DB Design

## Purpose

The MegaCity host keeps a local SQLite database as an intermediate semantic/city cache between Tree-sitter parsing and later spatial/layout work.

Current goals:

- persist a useful city-oriented snapshot across sessions
- keep the storage local, inspectable, and cheap to rebuild
- expose enough semantic structure for later district/layout generation
- support runtime queries by module

The database is a derived cache, not a source of truth. Schema migrations are therefore allowed to be destructive when needed.

## Current Runtime Paths

The path selection lives in `libs/draxul-megacity/src/megacity_host.cpp`.

- development runs from inside this repo write to `<repo>/db/megacity.sqlite3`
- macOS app/bundle runs outside the repo write to `~/Library/Application Support/draxul/megacity.sqlite3`

Generated local data directories are intentionally git-ignored:

- `.draxul/`
- `db/`

## Ownership And Flow

Current flow:

1. `draxul-treesitter` scans the repo into `CodebaseSnapshot`
2. `MegaCityHost` waits until the snapshot is complete
3. `draxul-citydb` reconciles the snapshot into SQLite

The reconciliation currently happens once per completed scan in one transaction. It does not yet do per-file incremental updates.

## Tree-sitter Inputs

`SymbolRecord` now carries more than display metadata:

- `kind`
- `name`
- `parent`
- `is_abstract`
- `line`
- `end_line`
- `field_count`
- `referenced_types`
- `fields`

Notes:

- inline class methods are now associated with their enclosing class
- out-of-class method definitions still use the qualified-name split
- `field_count` is the count of direct data-member declarations in the class/struct body
- `referenced_types` is a raw set of type-like names seen inside the class/struct declaration body
- `fields` is a best-effort list of direct member fields with field name, display type, and raw referenced type names for that declaration

These values let the DB compute building metrics without needing to keep AST nodes alive.

## Schema

Schema version is managed with `PRAGMA user_version`.

Current schema version: `5`

### `files`

- `path` `TEXT PRIMARY KEY`
- `module_path` `TEXT NOT NULL`
- `symbol_count` `INTEGER NOT NULL`
- `parse_error_count` `INTEGER NOT NULL`
- `last_scanned_at_unix` `INTEGER NOT NULL`

### `symbols`

- `symbol_id` `TEXT PRIMARY KEY`
- `file_path` `TEXT NOT NULL`
- `module_path` `TEXT NOT NULL`
- `kind` `TEXT NOT NULL`
- `name` `TEXT NOT NULL`
- `qualified_name` `TEXT NOT NULL`
- `parent_name` `TEXT NOT NULL`
- `line_start` `INTEGER NOT NULL`
- `line_end` `INTEGER NOT NULL`
- `is_abstract` `INTEGER NOT NULL`
- `city_role` `TEXT NOT NULL`
- `field_count` `INTEGER NOT NULL DEFAULT 0`

Indexes:

- `idx_symbols_file_path`
- `idx_symbols_city_role`
- `idx_symbols_module_path`

### `city_entities`

- `entity_id` `TEXT PRIMARY KEY`
- `symbol_id` `TEXT NOT NULL UNIQUE`
- `entity_kind` `TEXT NOT NULL`
- `district` `TEXT NOT NULL`
- `display_name` `TEXT NOT NULL`
- `module_path` `TEXT NOT NULL`
- `source_file_path` `TEXT NOT NULL`
- `source_line` `INTEGER NOT NULL`
- `base_size` `INTEGER NOT NULL`
- `building_functions` `INTEGER NOT NULL`
- `building_function_sizes_json` `TEXT NOT NULL`
- `road_size` `INTEGER NOT NULL`
- `height` `REAL NOT NULL`
- `footprint_x` `REAL NOT NULL`
- `footprint_y` `REAL NOT NULL`

Indexes:

- `idx_city_entities_district`
- `idx_city_entities_module_path`

### `symbol_fields`

- `field_id` `TEXT PRIMARY KEY`
- `symbol_id` `TEXT NOT NULL`
- `field_name` `TEXT NOT NULL`
- `field_type_name` `TEXT NOT NULL`

Indexes:

- `idx_symbol_fields_symbol_id`

### `city_entity_dependencies`

- `source_entity_id` `TEXT NOT NULL`
- `target_entity_id` `TEXT NOT NULL`
- `field_name` `TEXT NOT NULL`
- `field_type_name` `TEXT NOT NULL`

Primary key:

- `(source_entity_id, target_entity_id, field_name, field_type_name)`

Indexes:

- `idx_city_entity_dependencies_source`
- `idx_city_entity_dependencies_target`

## Semantic Classification

`city_role` is derived during reconciliation:

- `concrete_class`
- `abstract_class`
- `data_struct`
- `free_function`
- `method`
- `include`

`entity_kind` is currently:

- classes -> `building`
- abstract classes -> `tower`
- data structs -> `block`
- free functions -> `tree`

Methods and includes are stored as symbols but not emitted as city entities.

## Derived Building Metrics

### `base_size`

For class/struct entities:

- `base_size = field_count`

This is intended as the semantic footprint seed before a real layout pass exists.

### `building_functions`

For class/struct entities:

- number of defined methods associated with that type name

Method sizes are derived from line span:

- `function_size = end_line - line + 1`

### `building_function_sizes_json`

Stored as a JSON text array of integers, for example:

```json
[7, 14, 3]
```

This is intentionally denormalized for now because it is easy to inspect and sufficient for early layout heuristics.

If later analytics need per-method joins, add a normalized child table rather than replacing this field immediately.

### `road_size`

For class/struct entities:

- count distinct resolved field dependencies to other known class/struct entities
- self-references are excluded
- ambiguous simple-name matches are ignored

The DB now keeps the explicit field and dependency rows as well as the collapsed `road_size`.

## Normalized Render Metrics

The DB keeps raw semantic metrics. The current UI preview derives city-friendly render metrics from those raw values rather than storing the compressed values back into SQLite.

This keeps:

- raw data stable and inspectable
- normalization tunable without schema churn
- future layout code free to recompute visual metrics differently

### Current Preview Formulas

The current normalization logic is shown in the MegaCity Tree-sitter ImGui panel under `City Preview`.

#### Buildings / towers / blocks

Inputs:

- `base_size`
- `building_functions`
- `function_mass = sum(building_function_sizes_json)`
- `road_size`

Derived preview values:

- `footprint = 1 + sqrt(base_size)`, clamped to `[1.0, 9.0]`
- `height = 2 + 1.35 * log1p(function_mass) + 0.45 * sqrt(building_functions)`, clamped to `[2.0, 12.0]`
- abstract classes currently get a small additional height bonus
- `road_width = 0.6 + 0.85 * log1p(road_size)`, clamped to `[0.6, 3.0]`

This compresses outliers while preserving ordering.

#### Free functions / trees

Free functions are currently shown as trees rather than buildings.

Inputs:

- method/function line span

Derived preview values:

- fixed footprint seed of `1.0`
- `tree_height = 1.4 + 0.9 * log1p(function_size)`, clamped to `[1.4, 4.5]`

## UI Integration

The existing Tree-sitter ImGui panel now has three main sections:

- `Files`
- `Objects`
- `City Preview`

`City Preview` is module-grouped and shows, for each entity:

- raw semantic values
- compressed preview render values

The preview is computed directly from the live `CodebaseSnapshot`, so it is available immediately while experimenting with formulas and does not depend on querying the SQLite cache first.

## Module Model

`module_path` is derived from the source file parent path:

- `src/foo/bar.cpp` -> module `src/foo`
- root-level files -> empty module path

This is path/module grouping, not C++ namespace/module semantics.

That is intentional for now because it matches how the codebase is physically organized and is easy to query at runtime.

## Public API

The public surface currently lives in `libs/draxul-citydb/include/draxul/citydb.h`.

### `CityDatabase`

Core lifecycle:

- `open(path)`
- `close()`
- `is_open()`
- `path()`
- `last_error()`
- `stats()`
- `reconcile_snapshot(snapshot)`

Runtime query helpers:

- `list_modules()`
- `list_classes_in_module(module_path)`

### `CityDbStats`

Current aggregate state:

- opened DB path
- file count
- symbol count
- city entity count
- last reconcile time
- whether a snapshot has been reconciled

### `CityClassRecord`

Returned by `list_classes_in_module()`:

- `name`
- `qualified_name`
- `module_path`
- `source_file_path`
- `entity_kind`
- `base_size`
- `building_functions`
- `function_sizes`
- `road_size`
- `is_abstract`

Despite the name, the current query includes class-like entities, including structs.

## Current Limitations

- type identity is still based on simple names, not namespaces or fully qualified type graphs
- `road_size` is heuristic and depends on Tree-sitter type-like name capture
- `building_functions` groups by type name only, so duplicate type names in different scopes could collide
- `base_size` only counts direct data members, not nested records or semantic weight
- `building_function_sizes_json` is text, not a normalized relational child table
- reconciliation is full-snapshot, not per-file incremental

## Likely Next Steps

- add namespace-aware type identity
- split semantic data from explicit city layout data
- add a normalized table for per-method records if needed
- add per-file incremental reconciliation
- compute district/layout placement from module graph + `road_size`
- add renderer-facing queries for neighborhoods, roads, and parcel geometry
