# Level authoring stage 1: PROPS LAYERS (design)

> 2026-11-09. Branch `tools/levelgen-props`. Status: design, not started.
>
> **The request (game designer, 2026-11-09).** Maps are big and hand placement takes too long. He wants to author props iteratively:
> - generate one layer at a time: barrels (some explosive), crates, turrets, mines, loot;
> - re-roll a layer until it looks right;
> - lock the layers and objects he likes;
> - never lose his hand edits made in the editor.
>
> **Scope.** This doc covers the data format, the placement rules, the runtime spawner, the authoring workflow, the task split and the tests. Level geometry (`codex.levelgen_layout/1`, LEVEL_GENERATOR.md) is not changed.
>
> **Assumption markers.** **[A]** marks an assumption about existing code. Each one must be checked before the task that depends on it starts.

## 0. Decisions in one screen

| Topic | Decision |
|---|---|
| Storage | One file per map: `Content/Data/LevelGen/props/<map>.props.json`, schema `codex.levelgen_props/1`. The level JSON references it. It is not stored in the layout or in the .umap. |
| Spawn | Spawned at level load by `UCodexLevelPropsSubsystem` (a `UWorldSubsystem`). The .umap holds no props. |
| What the file holds | Per layer: the rules, the seed, the lock flag and the resolved `objects` (the last roll). Separately: the hand `overrides`. The game spawns `objects` + `overrides` as stored, so what the preview shows is what spawns. |
| Placement code | The C++ `PropsPlacementRules` is the reference. The TS port gives the instant re-roll in the Training Ground (TG). Both run the same golden fixtures. This is the same pattern as the combat sim. |
| Determinism | Integer cm, a 50 cm candidate lattice, a 32-bit RNG (`mulberry32`), ties sorted by id. No floats in any decision. |
| Who writes what | The TG writes `layers[]`. The editor writes `overrides[]`. Each writer re-reads the file and replaces only its own keys. `revision` is bumped on every write. |
| Gate | Fast gate in the TG: nav-grid completability with props as obstacles, run on every re-roll. Real gate: `LevelGenRouteSmoke` + `LevelGenLaddersSmoke` with props loaded. |
| Balance | All numbers (damage, radii, HP, loot tables, budgets, default rules) live in `Content/Data/Balance/props_balance.json`. The C++ hardcodes nothing. |

## 1. Data format

### 1.1 Files

- **`Content/Data/LevelGen/props/<map>.props.json`.** One per built map, e.g. `L_Gen_Depot_1.props.json`.
  - It is a separate file because the layout is the accepted generator output; re-accepting a layout must not wipe props.
  - The level JSON is rewritten by the builder and edited by the combat-lab page. Three writers on one file is too many.
- **The level JSON** gets a new block: `"props": { "file": "L_Gen_Depot_1.props.json", "enabled": true }`.
  - The builder template writes it.
  - A rebuild keeps it, as it already keeps `pressure` / `cheats`.
  - Console override: `Codex.Props.Enabled 0|1`.
- **`Content/Data/Balance/props_balance.json`** (`codex.props_balance/1`). Holds:
  - per-kind stats;
  - rule presets;
  - loot tables;
  - budgets;
  - the class map `kind/variant -> soft class path`, so no class is hardcoded.

### 1.2 Schema `codex.levelgen_props/1`

**Top level:**

| Field | Meaning |
|---|---|
| `schema` | Always `codex.levelgen_props/1`. |
| `map`, `layout` | The built map and the accepted layout name. |
| `layout_hash` | SHA-1 of the canonical geometry JSON: `walls`, `buildings`, `cut`, `ladders`, `covers`, `routes`, `spawn`, `end`. Detects a changed layout (§2.6). |
| `revision` | Integer, bumped on every write. Used for the merge check (§4.3). |
| `frozen` | When `true`, the TG and the editor refuse writes. Set for shipped campaign maps. |

**Per layer** (`layers[]`):

| Field | Meaning |
|---|---|
| `id`, `kind` | `kind` is one of `barrels`, `crates`, `turrets`, `mines`, `loot`. |
| `enabled` | Layer on or off. |
| `seed` | uint32. |
| `locked` | When `true`, a re-roll of this layer is refused. |
| `preset` | Optional. Defaults come from the balance file; `rules` overrides them. |
| `rules` | The placement rules (§1.3 and the example). |
| `rules_hash` | SHA-1 of the merged rules at roll time. Detects JSON edited by hand. |
| `objects[]` | The resolved roll. |
| `report` | The last placement report (§2.5). |

**Per object** (`objects[]`):

| Field | Meaning |
|---|---|
| `id` | `<layer>_<seed hex8>_<n>`. Unique across re-rolls. |
| `variant` | E.g. `barrel` / `barrel_explosive`, `crate_s` / `crate_l`. |
| `pos` | `[x, y]` in integer cm. |
| `walk_layer` | The layout layer name: `ground`, `roof:B_x`, `floor:B_x:n`, `cut`. |
| `yaw` | Integer degrees. |
| `locked` | Bool. |
| Kind fields | Optional: `attach` (mined crate / loot in crate), `door` (doorway mine), `trigger`, `loot_table`, `arc`. |

**Overrides** (`overrides[]`), written only by the editor (or a later TG drag tool):

| `op` | Fields | Meaning |
|---|---|---|
| `move` | `id`, `pos`, `walk_layer`, `z_offset`, `yaw` | Moving an object also pins it: a moved object is treated as locked. |
| `delete` | `id`, `pos` | The object is hidden. After a re-roll its `pos` becomes a keep-out disk (§2.2). |
| `add` | A full object with `id: "hand_<layer>_<n>"`, `origin: "hand"` | Never re-rolled. Counts toward the layer's target. |
| `set` | `id`, `fields: {…}` | Changes fields, e.g. explosive on/off or the loot table. Also pins the object. |

### 1.3 Example (Depot, all five kinds; trimmed to 1-2 objects per layer)

```json
{
  "schema": "codex.levelgen_props/1", "map": "L_Gen_Depot_1", "layout": "gen_ref_01_02_depot_1",
  "layout_hash": "3f1c…", "revision": 14, "frozen": false,
  "layers": [
    { "id": "crates", "kind": "crates", "enabled": true, "seed": 7, "locked": false, "preset": "crates_default",
      "rules": {
        "count": { "mode": "density", "per_100m2": 0.25, "max": 80 },
        "where": { "zones": ["Z_compound", "Z_junkyard", "Z_tanks", "Z_ruin"], "walk_layers": ["ground", "floor:*"],
                   "near": [{ "what": "wall", "max_cm": 150, "weight": 3 }, { "what": "cover", "max_cm": 200, "weight": 2 }] },
        "variants": { "crate_s": 3, "crate_l": 1 },
        "min_spacing_cm": 250, "min_gap_any_cm": 120,
        "exclude": { "spawn_cm": 1500, "end_cm": 500, "ladder_cm": 300, "stair_cm": 250, "door_cm": 250,
                     "route_cm": { "mandatory": 260, "variants": 200 }, "crossing": true, "hole_rim_cm": 200 } },
      "rules_hash": "a91e…",
      "objects": [ { "id": "crates_00000007_0", "variant": "crate_s", "pos": [688, -5300], "walk_layer": "ground", "yaw": 90, "locked": true } ],
      "report": { "requested": 41, "placed": 41, "gate": "pass" } },

    { "id": "barrels", "kind": "barrels", "enabled": true, "seed": 3, "locked": false,
      "rules": { "count": { "mode": "count", "value": 30 },
        "cluster": { "min": 1, "max": 4, "ring_cm": [70, 130] },
        "explosive": { "share": 0.3, "min_gap_cm": 400, "never_within_cm_of": { "spawn": 2500 } },
        "where": { "zones": ["*"], "walk_layers": ["ground"], "near": [{ "what": "wall", "max_cm": 120, "weight": 2 }] },
        "min_spacing_cm": 600, "min_gap_any_cm": 100, "exclude": { "$preset": "standard" } },
      "objects": [ { "id": "barrels_00000003_0", "variant": "barrel_explosive", "pos": [-2450, -2100], "walk_layer": "ground", "yaw": 0, "locked": false } ] },

    { "id": "turrets", "kind": "turrets", "enabled": true, "seed": 1, "locked": false,
      "rules": { "count": { "mode": "count", "value": 2 },
        "where": { "walk_layers": ["roof:*", "ground"], "anchors": ["ambush_points", "roof_edges", "behind_tall_cover"] },
        "cover_route": { "route": "R_main", "range_cm": [1000, 4000], "arc_deg": 90, "min_covered_m": 25 },
        "exclude": { "spawn_cm": 4000, "ladder_cm": 300, "stair_cm": 300 }, "min_spacing_cm": 3000 },
      "objects": [ { "id": "turrets_00000001_0", "variant": "turret_mg", "pos": [3400, 2600], "walk_layer": "roof:B_warehouse",
                     "yaw": 180, "arc": 90, "locked": false } ] },

    { "id": "mines", "kind": "mines", "enabled": true, "seed": 11, "locked": false,
      "rules": {
        "doorways": { "mode": "percent", "value": 25, "kinds": ["building_door", "gate"], "mandatory_route": "forbid",
                      "inside_cm": 80, "trigger": "tripwire" },
        "crates":   { "source_layer": "crates", "percent": 10, "trigger": "crate_open" },
        "field":    { "count": { "mode": "count", "value": 4 }, "where": { "zones": ["Z_cut"], "walk_layers": ["cut"] },
                      "route_cm": { "mandatory": 260, "variants": 0 }, "trigger": "proximity", "min_spacing_cm": 500 } },
      "objects": [
        { "id": "mines_0000000b_0", "variant": "mine_tripwire", "door": "B_pump#0", "pos": [-4339, -1820], "walk_layer": "ground", "yaw": 90, "trigger": "tripwire", "locked": false },
        { "id": "mines_0000000b_1", "variant": "mine_crate", "attach": "crates_00000007_0", "pos": [688, -5300], "walk_layer": "ground", "trigger": "crate_open", "locked": false },
        { "id": "mines_0000000b_2", "variant": "mine_proximity", "pos": [-17, 4100], "walk_layer": "cut", "trigger": "proximity", "locked": false } ] },

    { "id": "loot", "kind": "loot", "enabled": true, "seed": 5, "locked": false,
      "rules": { "budget_value": 600, "tables": ["ammo_common", "med_common", "rare_roof"],
        "containers": { "crates_share": 0.3, "source_layer": "crates" },
        "loose": { "where": { "anchors": ["dead_ends", "roof_centres", "building_interiors", "ambush_points"] },
                   "prefer_far_from_route_cm": [1400, 3000], "roof_table": "rare_roof" },
        "min_spacing_cm": 800 },
      "objects": [ { "id": "loot_00000005_0", "variant": "loot_pile", "pos": [3400, 1500], "walk_layer": "roof:B_warehouse", "loot_table": "rare_roof", "locked": false } ] }
  ],
  "overrides": [
    { "op": "move",   "id": "barrels_00000003_0", "pos": [-2480, -2140], "walk_layer": "ground", "yaw": 30 },
    { "op": "delete", "id": "crates_00000007_5",  "pos": [1032, -5351] },
    { "op": "add",    "object": { "id": "hand_barrels_0", "origin": "hand", "variant": "barrel_explosive", "pos": [693, 2000], "walk_layer": "ground", "yaw": 0 } }
  ]
}
```

### 1.4 Versioning

- Readers (C++ and TS) accept `/1` and refuse an unknown major version with a clear log line. They never crash.
- New optional fields are allowed in `/1`. A breaking change means `/2` plus a TS migration in `src/levelgen/props/migrate.ts`.
- `layout_hash` mismatch means the props are **stale** (§2.6).
- `rules_hash` mismatch means `rules` were edited by hand. The game re-runs C++ placement for that layer at load and logs it. The TG shows "rules changed, re-roll to store".

## 2. Placement

### 2.1 Inputs read from the layout (`PropsLayoutView`, pure, built once per layout)

| Input | Source |
|---|---|
| Walk layers + Z | `layersOf` / `levelZ`: ground, `cut` (-300), floors, walkable roofs. Roof area = footprint minus 50 cm inset minus `roof_holes`. |
| Blocked area | `walls[]` (widened by thickness/2 + the object's half size + `min_gap_any_cm`), building footprints (ground layer only), `covers[]` footprints, chimneys, silos. |
| Doorways | `buildings[].doors[]` (id `<building>#<i>`, side read from the footprint). **Gates** = gaps of 80-400 cm between collinear wall segments of one wall/fence. Example: `F_compound_N_0` ends at y -6234, `F_compound_N_1` starts at -5934, so the gate is at (1400, -6084), 300 cm wide. Gaps in `CUTWALL_*` are crossings, not doors. |
| Routes | `routes[]` polylines per layer. `mandatory` and variants are kept apart. A door on the mandatory route = the route polyline passes within 150 cm of it (e.g. the compound gate above, the shed door (797, -7250)). |
| Fixed points | `spawn.pos`, `end`, ladders (`pos`, `foot`, `top`), stairs (the `pos` -> `top` corridor, 2 x half-width), roof/floor holes (rim = half side + 0.8 m), crossings polygons. |
| Anchors | Zones (`poly`, `kind`, `set_piece`, `intent`), `ambush_points`, `spawn_points`, `patrol_routes`. Dead ends = `Z_*` with `kind: wall_end`. Roof edges = roof perimeter inset 100 cm. |

### 2.2 Common algorithm (every kind)

1. **Target count.** Use `count` (`count`, `density` per 100 m² of eligible area, or `per_zone`). Subtract the kept objects: locked objects + overrides `move` / `set` / `add`. Clamp to the balance budget.
2. **Candidates.**
   - A 50 cm lattice inside the allowed `where` (zones ∩ walk layers), plus anchor candidates (wall faces, door offsets, ambush points).
   - All candidates are integer cm.
3. **Hard filters.**
   - Inside bounds and on the walk layer.
   - Not blocked.
   - Outside every `exclude` radius or corridor.
   - Outside the `delete` keep-out disks (radius = the layer's `min_spacing_cm`).
   - At least `min_gap_any_cm` from **every** current object of **all other** layers (their stored state).
   - At least `min_spacing_cm` from the kept objects of this layer.
4. **Weight.** Base 1, multiplied by each matched `near` clause's weight.
5. **Pick.**
   - Weighted draw without replacement with `rng = mulberry32(seed ^ fnv1a(layer.id))`.
   - After each pick, drop candidates within `min_spacing_cm`.
   - Stop at the target or when no candidates remain.
   - Candidates are pre-sorted by (y, x) so both languages draw from the same list.
6. **Ids.** `<layer>_<seed hex8>_<n>` in pick order. Kept objects keep their ids.

**Order and determinism.**
- "Generate all" runs in the fixed order crates -> barrels -> turrets -> mines -> loot.
- A single re-roll treats the other layers' stored objects as obstacles.
- So the result is a pure function of (layout, rules, seed, other layers' objects, overrides). That is the unit-tested contract.

### 2.3 Per kind

- **Crates.**
  - Common algorithm with near-wall / near-cover weights.
  - `floor:*` allowed (enterable buildings).
  - Yaw = the nearest wall's direction snapped to 90°, else `rng % 4 * 90`.
- **Barrels.**
  - Pick cluster anchors with the common algorithm.
  - Each anchor gets `min..max` members on ring lattice offsets (`ring_cm`). Every member passes the hard filters.
  - Explosive: `share` of the clusters, chosen by rng. Explosives keep `explosive.min_gap_cm` from each other across clusters (this limits chain reactions) and are never inside `never_within_cm_of`.
- **Turrets.**
  - Candidates = anchors only (ambush points, roof edges, the far side of covers >= 140 cm).
  - Score = metres of `cover_route` visible inside `range_cm` and `arc_deg`. Visibility uses 2D segment tests against walls + tall covers, the same blockers as the evaluator's sightlines; on roofs the elevated rule applies (only obstacles that reach eye height block).
  - Drop candidates scoring below `min_covered_m`.
  - Weighted pick by score. Yaw = the centre of the covered arc snapped to 15°.
- **Mines.** Three sub-rules, all in the one `mines` layer:
  - **Doorways.**
    - Enumerate the doorways (§2.1), filtered by `kinds`.
    - `mandatory_route: forbid` removes the mandatory doors.
    - Sort by id, seeded Fisher-Yates.
    - Take `value` (count) or `ceil(value% x eligible)` (percent).
    - Position = the door centre moved `inside_cm` into the building (gates: the centre). Yaw = across the opening.
  - **Crates.**
    - Eligible = the `source_layer` objects (generated + hand, not deleted).
    - Seeded pick of `percent`.
    - Each mine is stored with `attach` = the crate id. A locked mine keeps its crate even if the crate layer is re-rolled, but only if that crate is locked too; otherwise the mine is dropped and reported.
  - **Field.** Common algorithm with its own `route_cm`. A mine never blocks navigation (no nav modifier), but `exclude` keeps it off ladders, stairs and the mandatory route corridor.
- **Loot.**
  - `containers.crates_share` of the crates get `attach` + a `loot_table`.
  - Loose piles use anchors (dead ends, roof centres, building interiors, far ambush points) weighted by `prefer_far_from_route_cm`.
  - Draw until `budget_value` (table values from the balance file) is spent.
  - Roof piles use `roof_table` (a reward for climbing).

### 2.4 Validation (after every roll; C++ and TS run the same checks)

**Static checks:**
- Every object is on its walk layer.
- Not blocked; spacing and exclusions hold.
- `attach` targets exist.
- Doorways keep a free 90 cm corridor 150 cm deep on both sides, except the doorway's own mine.
- Ladder `foot` / `top` and stair ends keep a free 100 cm disk.

**Nav gate (TS fast gate):**
- Build the lab nav grid with nav-affecting props as obstacles (crates, barrels, turrets; not mines, not loot) widened by the 35 cm agent radius.
- Re-run `evaluate.ts` completability for **every** route.
- Result `gate: pass | fail`, with the first failing leg (same messages as §4.1 of LEVEL_GENERATOR.md).

### 2.5 Failure handling (never throw)

**Report** per layer:
`{ requested, placed, kept, short_by, rejected: { blocked, spacing, route, spawn, ladder, door, keepout, other_layer }, gate, gate_leg?, warnings[] }`.

- Placing fewer than requested is a **report**, status `short`, shown in the TG; not an error.
- A gate failure:
  - the TG offers "re-roll" / "drop offending objects" (the objects whose removal makes the leg pass);
  - "Save" still works, but the file keeps `gate: fail`, and at load the game logs `Props WARN gate fail <layer>`.
- At load, the C++ validator **drops** (does not spawn) any object that fails a static check and logs it with its id. Locked and hand objects are logged as `LOCKED/HAND INVALID` so the designer sees them.

### 2.6 Stale layout

When `layout_hash` differs:
- the game still spawns, with per-object validation;
- the TG shows a "stale" badge and "Re-validate" (lists the invalid objects; never auto-deletes locked or hand objects).

## 3. Runtime (C++)

### 3.1 Subsystem

`UCodexLevelPropsSubsystem : UWorldSubsystem`, game module, no tick.

- **When it runs:** `OnWorldBeginPlay`. It reads the level JSON `props` block through the existing level-JSON loader **[A: a level-JSON reader/subsystem exists that `drop_links` / `pressure` already use - verify the name]**.
- **Pipeline:**
  1. Parse the file (`FLevelPropsFile`, pure struct).
  2. Apply the overrides (pure `ApplyOverrides`).
  3. Re-place any layer whose `rules_hash` is stale (§1.4).
  4. Validate (§2.4 static).
  5. Skip the ids recorded in the save (§3.4).
  6. Spawn.
- **Order:**
  - Spawn before `UDropLinkSubsystem` builds its links **[A: it also runs at begin play; add an explicit dependency via `Collection.InitializeDependency` or a ready delegate]**.
  - Spawn before the pressure subsystem's first event at 30 s, so no ordering issue there.
- **Z:**
  - `levelZ(level of walk_layer)` + `z_offset`.
  - One downward trace per prop at spawn, ±30 cm, to snap to the slab. A miss logs and uses the computed Z.
  - Nothing runs per tick.
- **Nav:**
  - Nav-affecting props carry a `UNavModifierComponent` (area from the balance file, default Null) sized to the footprint.
  - This needs runtime nav generation = `DynamicModifiersOnly` on generated maps **[A: currently Static after `bake_navmesh.ps1` - the builder must set it; one task]**.
- **Console:** `Codex.Props.Enabled`, `Codex.Props.MinesInert` (spawn but never fire; used by smokes), `Codex.Props.Dump` (counts per layer, dropped ids).

### 3.2 Actor classes (class per `variant` comes from the balance class map; none hardcoded)

| Kind | Class | Notes |
|---|---|---|
| Crate | `ACodexPropCrate` **[A: no destructible crate exists; greybox covers are static cubes]** | HP; optional `UCodexPropTrapComponent`; optional loot. |
| Barrel / explosive barrel | `ACodexPropBarrel` **[A: an explosive-barrel class may exist from combat work - search `Explosive`, `Barrel`; reuse if it has data-driven damage]** | Static, no physics simulation in stage 1. Explosion through the existing damage/explosion path **[A]**. |
| Turret | Existing enemy/turret pawn if present, else `ACodexPropTurret` **[A: verify whether the enemy roster has a stationary gunner]** | The AI is its own; the subsystem only spawns it with yaw + arc. |
| Mine | `ACodexPropMine` (new) | Trigger shapes: sphere (proximity), box across the door (tripwire). Overlap events only, no tick. |
| Loot pile | Existing pickup actor **[A: verify `Pickup` / inventory item classes]** | `loot_table` resolved through the balance file. |
| Trap component | `UCodexPropTrapComponent` (new) | On a crate: fires on `crate_open` (interaction) or destruction. |

**Tagging.** Actor tags `CodexProp`, `prop_layer=<layer id>` and `prop_id=<object id>`. This matches the `ladder=<id>` convention.

**Representations:**
- A **mined crate** = the crate actor + an armed trap component. The mine object exists only in data (its id is used for save/telemetry); nothing is spawned for it.
- A **mined doorway** = an `ACodexPropMine` with a tripwire box spanning the door width, placed `inside_cm` past the threshold.

### 3.3 Performance budget (caps in the balance file; the smoke measures)

| Budget | Value |
|---|---|
| Props per map | <= 400 total (crates 120, barrels 80, turrets 12, mines 40, loot 80). |
| Spawn time | <= 60 ms total on Depot (measured in the smoke; WARN above). |
| Tick | Zero ticking props, except the turret AI's own. |
| Mines | Overlap events only. |

- No pooling for props (one-shot spawn).
- Explosion FX/decals use the existing pooled path **[A]**.
- Static mesh components share meshes. ISM is not needed at this count; revisit above 1000.

### 3.4 Save / load

- The save stores, per map: `props_revision`, `destroyed[]`, `looted[]`, `triggered[]` (prop ids) **[A: a per-level save record exists - verify; else add `FCodexLevelPropsSave`]**.
- On load, those ids are skipped.
- Revision differs from the save (dev only): spawn all and log. Campaign maps are set to `frozen: true` before release, so ids stay stable.

## 4. Workflow end to end

### 4.1 TG page (Gemini, TS)

A new **«Пропсы»** tab in `LevelGenDepartment.tsx` with `PropsPanel.tsx`:

- **Left:** layer list.
  - Per layer: toggle, lock, seed, «Перебросить» (seed+1 or random), rules form from the schema, report line.
  - Global: «Сгенерировать всё», «Сохранить».
- **Middle:** the `LayoutView` canvas plus a props overlay.
  - Icons per kind; explosive in red; a lock badge; overrides tinted (moved = blue, added = green, deleted = grey X).
  - Layer toggles; an "exclusions" toggle draws the spawn/ladder/door/route keep-outs.
  - Click an object: lock/unlock, details.
- **Right:** per-layer report, gate status with the failing leg, stale badge.
- **Buttons:**
  - «Открыть в Unreal»: launches the editor on the map. No rebuild is needed; props are data.
  - «Прогнать маршрут с пропсами»: `levelgen_route_smoke.ps1 -Map …` with `job: props_smoke` status (§8.3 pattern).

**Server:** `server/levelgenApi.ts`
- `GET /api/levelgen/props/:map`.
- `PUT /api/levelgen/props/:map` with `{ base_revision, layers }`. The server re-reads the file and refuses on revision mismatch (409; the page reloads and shows the diff). It writes only `layers`, bumps `revision`, writes atomically (temp + rename) and refuses when `frozen`.

**Flow:**
1. Re-roll in the browser (TS port).
2. The fast gate runs.
3. Save.
4. The next level load in the game or editor picks it up. No build step.

### 4.2 Editor hand edits (Claude, C++, editor module **[A: `CodexTacticsEditor` module exists - else create it]**)

Simplest robust way: **spawn transient, diff on save.**

1. `UCodexPropsEditorSubsystem` (`UEditorSubsystem`). On map open, if the level JSON has `props`, it spawns all props as **transient** actors (`RF_Transient`; never saved into the .umap) in the outliner folder `Props/<layer>`, with the same tags. Toolbar/console `Codex.Props.Edit`.
2. The designer moves, deletes, duplicates (Alt-drag) or drags a prop class from the palette. A new actor without `prop_id` = an add. The palette lists classes from the balance class map; the layer is picked from a dropdown, default = the class's kind.
3. On `FEditorDelegates::PreSaveWorld`, or the button «Save props», the subsystem **diffs** world vs file:
   - an id is missing -> `delete`;
   - position moved > 1 cm or yaw changed > 1° -> `move`;
   - a tagged actor of a prop class without an id -> `add` with a new `hand_` id.
4. It writes **only** `overrides` (same revision check and atomic write). No per-transaction tracking, so undo/redo needs no special handling.
5. A changed props file on disk (the TG saved) triggers a respawn of the transient actors **only if** the editor holds no unsaved prop edits; otherwise it shows a notification "props changed on disk - save or discard your edits".

**What a re-roll does with hand edits:**
- It never touches overrides.
- A `move` / `set` pins the object: it is kept and counted, with the edit applied.
- A `delete` of a re-rolled object turns into a keep-out.
- An `add` is kept and counted.
- In the TG, the designer can clear an override per object («Сбросить правку»), but never in bulk without a confirm.

## 5. Tasks (one commit each)

The shared fixtures are written first by Claude: `Content/Data/LevelGen/props/fixtures/` holds a small layout (doors, a gate, a ladder, a route, two zones) plus 6 expected outputs.

### C++ (Claude). Tests in `CodexTacticsDev` **[A: the automation test module used by the smokes]**

| # | Task | Acceptance |
|---|---|---|
| C1 | `codex.levelgen_props/1` + `props_balance.json` + fixtures + `FLevelPropsFile` parse/write + `ApplyOverrides` (pure) | Round-trip equal; unknown major refused with a log; overrides: move/delete/add/set applied; a deleted id becomes a keep-out after a re-roll. |
| C2 | `PropsLayoutView` (pure): walk layers, blocked test, doorways incl. gates, exclusion shapes from the layout | On Depot: the gate (1400, -6084) is found and is on the mandatory route; `B_pump#0` is not on it; the cut gaps are not doors; `B_hall_n` has 2 doors. |
| C3 | `PropsRng` (mulberry32, fnv1a) + common algorithm + crates + barrels | Same seed -> byte-identical objects; a different seed differs; locked objects are kept and counted; spacing / `min_gap_any` / every exclusion holds on 50 seeds; explosive gap holds; `short` report when the area is too small (no crash). |
| C4 | Turrets + mines (doorway N and %, crate %, field) + loot | Doorway % = ceil(p x eligible); `forbid` never picks a mandatory door; a crate mine is dropped (reported) when its unlocked crate is re-rolled; turrets score > `min_covered_m`; loot stays within budget. Golden fixtures match. |
| C5 | Static validator + nav-free checks (door corridor, ladder disks) | Bad fixture objects are dropped with id logs; locked/hand ones are flagged `LOCKED/HAND INVALID`. |
| C6 | `UCodexLevelPropsSubsystem` + actor classes / class map + tags + nav modifiers + the builder sets `DynamicModifiersOnly` | `LevelPropsSpawnSmoke` on Depot: count per layer = file; tags present; 0 tickers; spawn <= 60 ms; `Codex.Props.Enabled 0` spawns nothing. |
| C7 | Trap component (mined crate), doorway mine, `MinesInert` + save/load of destroyed/looted/triggered | Smoke: opening a mined crate fires (inert: logs only); a door tripwire fires on overlap; save -> load skips destroyed ids. |
| C8 | Editor subsystem: transient spawn + diff-on-save + overrides write + revision check | Editor test: move/delete/add -> 3 correct override records; nothing saved into the .umap (the map's actor count is unchanged); a revision conflict is refused. |
| C9 | **Combat gate** smoke | `LevelGenRouteSmoke` with props on and `MinesInert 1`: PASS 25 / 25; `LevelGenLaddersSmoke` 13 / 13; reports mines stepped on (info). Registered in `Scripts/test_map.json`. |

### TS (Gemini, Antigravity). Tests in `npm test`

| # | Task | Acceptance |
|---|---|---|
| T1 | `src/levelgen/props/schema.ts` types + read/write + `migrate.ts` stub | Fixture round-trip; the validator rejects a bad `kind` / missing seed. |
| T2 | Port of RNG + placement (§2) | **Parity:** every golden fixture output equals the C++ output byte for byte (same JSON after canonical sort). |
| T3 | Fast gate (nav grid with props + completability + door/ladder checks) | A fixture with a crate in a gate fails with the leg named; Depot with the default layers passes. |
| T4 | Server endpoints + revision merge + atomic write + `frozen` | PUT with a stale `base_revision` -> 409; `overrides` are never changed by a TG PUT. |
| T5 | `PropsPanel.tsx` + canvas overlay (toggles, lock, re-roll, exclusions, reports, stale badge) | Re-roll changes only the chosen layer; locked objects stay put; a Depot re-roll takes < 300 ms; screenshot check of the overlay. |
| T6 | «Открыть в Unreal» / «Прогнать маршрут с пропсами» jobs | Job status `props_smoke` shown; a failure shows the smoke's failing leg. |

**Order:** C1 -> C2 -> C3 -> T1/T2 (parity) -> C4 -> T3 -> C5/C6 -> T4/T5 -> C7 -> C8 -> C9/T6.

## 6. Open questions for the designer (with the recommended default)

1. **Barrels/crates already in the layout's `covers` (greybox cubes in the .umap).** Should they stay, or move into props layers?
   *Default:* keep them as cover geometry. Props are added on top and avoid them.
2. **A deleted generated object after a re-roll.** Should its spot stay blocked?
   *Default:* yes, a keep-out disk of the layer's spacing. «Сбросить правку» clears it.
3. **Mines in doorways on the mandatory route.**
   *Default:* forbidden. Only variant and side doors; opt-in per layer (`mandatory_route: allow`).
4. **Mine visibility in stage 1.**
   *Default:* a visible marker within 3 m; shooting a mine detonates it; no disarm action yet.
5. **Explosive chains (barrel sets off barrel).**
   *Default:* allowed, limited by `explosive.min_gap_cm` 400 between explosives, so chains happen only inside a cluster.
6. **Freezing.**
   *Default:* `L_Gen_Depot_1` (campaign 0.1) gets `frozen: true` once you sign off its props. Further re-rolls on it need an explicit unfreeze in the TG.