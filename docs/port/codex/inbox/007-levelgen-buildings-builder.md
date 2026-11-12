# 007 - Game builder & C++ runtime support for combat buildings (draft for Claude to approve)

> Status: Draft specification from Gemini (`027 item 7`), pending Claude review & approval.  
> Source task: `docs/port/gemini/inbox/027-levelgen-combat-buildings.md` (item 7)  
> Schema & TS report: `docs/design/levelgen/BUILDINGS.md`, `docs/port/gemini/outbox/027-levelgen-buildings.report.md` (`45ee8a8c`)  
> Target layouts: `Content/Data/LevelGen/accepted/L_Gen_Train_1.json` .. `L_Gen_Train_3.json` (plus full backward compatibility with legacy `gen_ref_01_02_depot_1.json`)

## 1. Architecture Note: Where "the Builder" Lives Today

Map actor spawning and C++ levelgen consumption are currently split across two places — both need updates:

1. **Map Geometry Spawner (UE Editor Python):** `Scripts/Editor/build_levelgen_map.py` (helpers `Scripts/Editor/levelgen_ladders.py`, `Scripts/Editor/levelgen_bot_route.py`; CLI wrapper `Scripts/levelgen_build_map.ps1`). Today this script spawns all `StaticMeshActor` walls, slabs, stairs, `ALadderActor`s, `ANavLinkProxy` drops, and `TargetPoint`s into `/Game/Maps/Generated/<Map>`.
2. **C++ LevelGen Reader & Runtime Systems (`Source/CodexTactics/`):**
   - `Source/CodexTactics/Private/LevelGen/` (`PropsLayoutView.cpp`, `LevelGenJsonRead.h`, `PropsPlacement*.cpp`) reads `codex.levelgen_layout/1` JSON for props & turret sightlines (`FPropsLayoutView::GetSightBlockers`, `BuildLayers`).
   - `Source/CodexTactics/Private/Core/CodexTacticsGameMode.cpp` (`StartPlay`) converts tagged actors via `VaultRules::IsVaultableLevelObject` -> `VaultNavigation::MakeVaultable`.
   - `Source/CodexTactics/Private/Characters/OperativeCharacterVault.cpp`, `VaultRules.cpp`, `SightRules.cpp`, `LedgeFallComponent.cpp`, `LadderActor.cpp`, `RoofCutawaySubsystem.cpp`.
   - `Source/CodexTacticsDev/Private/Debug/` hosts the headless smokes (`LevelGenRouteSmokeCommand.cpp`, `LevelGenLaddersSmokeCommand.cpp`, `DepotVaultSmokeCommand.cpp`, `DropSoftlockSmokeCommand.cpp`).

**Rule:** Never break legacy `codex.levelgen_layout/1` files that omit `openings`, `partitions`, `floors_detail`, `hatches`, `parapets`, or `mantle_edges`. Treat missing keys as empty arrays / pre-027 defaults.

---

## 2. Implementation Tasks & Exact `file:line` Insertion Points

### 2.1 Wall Openings (`window`, `loophole`) -> Segmented Walls (Aprons & Lintels)

**JSON contract (`BUILDINGS.md`, `L_Gen_Train_1.json:584-630`, `L_Gen_Train_2.json:1991-2034`):**
- `building.openings[]`: `{ id, kind: "door" | "window" | "loophole", pos: [x, y], floor, wall, width, sill_height, top_height, yaw }`.
  - `door`: `sill_height = 0, top_height = 210, width = 120` — ground doors are **already** pre-split out of `layout.walls[]` (`B_house_S_0`, `B_house_S_1`).
  - `window`: `sill_height = 90, top_height = 210, width = 120`. Still present inside `layout.walls[]` as solid spans.
  - `loophole`: `sill_height = 120, top_height = 150, width = 30`. Still present inside `layout.walls[]` as solid spans.

**Insertion 1 — `Scripts/Editor/build_levelgen_map.py:227-236` (+ `Scripts/Editor/levelgen_ladders.py:122-174`):**
- Currently lines `227-236` iterate `layout["walls"]`, cut only `exit_gaps`, and emit one full-height `wall_cube(...)` (`L142-145`) per piece. Because multi-storey exterior perimeter walls have `height = floors * 300` (e.g. `height = 600` in `L_Gen_Train_1.json:49`), projecting floor `0` and floor `1` openings requires slicing by floor first.
- Required change:
  1. For each wall `w` belonging to a building (`w.get("building")`) whose `height > LEVEL_HEIGHT` (`300`, `L73`): slice into per-storey horizontal bands `f = 0 .. int(round(w["height"] / LEVEL_HEIGHT)) - 1`, `band_z0 = gz + f * LEVEL_HEIGHT`, `band_h = LEVEL_HEIGHT`. (Note: for `f >= 1` over ground-floor `door` gaps where `walls[]` was already split at ground level, either keep upper bands continuous across the building edge from `building.footprint` or restore a `300 cm` spandrel/wall over exterior ground doors — **check this**: today `B_house_S_0` / `B_house_S_1` leave an open 6 m vertical slot above ground doors!).
  2. Match openings `op` in `b.get("openings", [])` where `op["kind"] in ("window", "loophole")` and `int(op.get("floor", 0)) == f` onto the collinear perimeter wall segment `(a -> b)` (using `op["wall"]` prefix/ID or point-on-segment projection `t` along `a..b`). Intersect ladder `exit_gaps` first as full-height cutouts; across each non-ladder opening interval `[t - width/2, t + width/2]`:
     - Emit **solid full-height intervals** between openings with `wall_cube(...)` (`height = LEVEL_HEIGHT`).
     - Emit **apron / sill** (`z0 = band_z0`, `height = op["sill_height"]`, e.g. `90` for window, `120` for loophole) with `tags`:
       - Window sill: `["building=" + bid, "floor=%d" % f, "window_sill", "mantle_sill"]` (and `"low_wall"`/`"Vault"` only when ground/floor landing is valid — see §2.6).
       - Loophole sill: `["building=" + bid, "floor=%d" % f, "loophole_sill"]` (not vaultable since `120 > MaxHeight 105`).
     - Emit **lintel / header** (`z0 = band_z0 + op["top_height"]`, `height = LEVEL_HEIGHT - op["top_height"]`, e.g. `90` for window `210..300`, `150` for loophole `150..300`) with `tags = ["building=" + bid, "floor=%d" % f, "opening_lintel", RoofCutaway when f >= 1]`.
  3. **Important constraint (`OperativeCharacterVault.cpp:99`):** downward vault trace `TopProbe` starts at `GroundZ + 200.f`. Window `top_height = 210` clears `GroundZ + 200.f` by `10 cm` — keep lintel bottom strictly `>= GroundZ + 210.f` so `TopProbe` hits the `90 cm` sill, never the header!

**Insertion 2 — `Source/CodexTactics/Private/LevelGen/PropsLayoutView.cpp:409-482, 1420-1489`:**
- Parse optional `buildings[].openings[]` in `ForEachObject(R, TEXT("buildings"), ...)` (`L409-482`).
- In `FPropsLayoutView::GetSightBlockers` (`L1420-1489`), when emitting blockers for `Floor` / `Ground` layers (`EyeHeightCm = 160`, `PropsLayoutView.h:57`), split building walls around `window` intervals on matching `Floor` (`sill_height < EyeHeightCm && top_height > EyeHeightCm`) so turret/prop sightline math (`PropsPlacementC4`) sees out through windows. Keep solid blockers for `loophole` unless designer wants turrets to shoot through slits (see §4 Q1).

**Insertion 3 — `Source/CodexTactics/Public/Combat/AmbushSpotRules.h:47-53` & `CombatPressureSubsystem.cpp:559`:**
- Verify window sills (`height = 90 cm`) carry accurate `Box.HeightCm` (`CombatPressureSubsystem.cpp:559`) and check whether `FreeEnds` / `Doorways` (`AmbushSpotRules.h:49-53`) treat a split `90 cm` sill pier between two windows as a walkable `Doorway` (`80..600 cm`) ambush candidate vs a solid chest-high window apron. Wait: `MinWallLengthCm = 150.f` (`AmbushSpotRules.h:48`) already filters short mullions `< 150 cm`, while piers `>= 150 cm` adjacent to window sills should check sill collision/height rather than assuming an open pass-through if full-height wall behavior is expected.

---

### 2.2 Interior Partitions (`partitions[]` & `walls[]` `kind: "interior"`)

**JSON contract verified in `L_Gen_Train_1.json:130-240, 936-990`:**
- Every partition is **already pre-split around its interior doors** and duplicated into top-level `layout["walls"][]` with `"id": "<B>_part_f<n>_<i>_<seg>"`, `"kind": "interior"`, `"building": "<B>"`, `"height": 300`, `"thickness": 20`, and `"floor": <n>` (omitted when `0`).

**Insertion — `Scripts/Editor/build_levelgen_map.py:227-236` & `Source/CodexTactics/Private/LevelGen/PropsLayoutView.cpp:665-715`:**
- **CRITICAL — Do NOT double-spawn!** Do NOT iterate `b["partitions"]` in `build_levelgen_map.py` to spawn walls; `layout["walls"]` (`L227-236`) ALREADY spawns them at `gz + w.get("floor", 0) * LEVEL_HEIGHT`!
- Required refinements:
  1. Ensure `build_levelgen_map.py` tags `w.get("kind") == "interior"` walls with `["interior", "building=" + w["building"], "floor=%d" % int(w.get("floor", 0))]`.
  2. For partition doors (`b["partitions"][].openings[]`, `kind == "door"`, `width = 120`, `top_height = 210`): optionally spawn a `210..300 cm` lintel cube above each interior door (note: verify Recast NavMesh agent height / step clears `210 cm` cleanly with `SLAB = 20 cm` above `LEVEL_HEIGHT = 300`).
  3. `PropsLayoutView.cpp:665-713` already includes `W.Floor == 0` in `GroundWalls` and `W.Floor == N` in `FloorWalls`, so C4 layer walkability and ground/floor sight blockers already respect interior partitions. Optionally parse `b["partitions"]` / `rooms[]` only if C4 room-level prop quotas are desired later.

---

### 2.3 Multi-Storey Slabs with `floors_detail[].gaps` (Stairwells, Balconies, Catwalks)

**JSON contract verified in `L_Gen_Train_1.json:1073-1178`, `L_Gen_Train_3.json:1420-1447`:**
- `building.floors_detail[]`: `{ floor: number, height_z: number, gaps: [{ id, poly: [[x,y],...], kind: "stairwell" | "balcony" | "atrium" | "catwalk" }], rooms: [...] }`.

**Insertion 1 — `Scripts/Editor/build_levelgen_map.py:151-175, 238-265`:**
- Today `slabs_with_holes(footprint, holes, z, ...)` (`L151-175`) only subtracts square holes from `b["floor_holes"]` and `b["stairs"]` (`STAIRWELL_HOLE = 350`), completely ignoring `floors_detail`.
- Required change:
  1. Extend `slabs_with_holes` (using grid-line sweep over arbitrary X/Y cut coordinates rather than fixed symmetric `(x ± half, y ± half)`) to accept general axis-aligned rectangular bounds `(xmin, ymin, xmax, ymax)` so `floors_detail[].gaps[].poly` can have arbitrary aspect ratios (e.g. `80 x 509` stairwell at `L_Gen_Train_1.json:1079-1099`, catwalks/balconies in `L_Gen_Train_3`).
  2. For each upper floor `n` in `range(1, floors)`: union `floor_holes(n)` + `stairs` stairwell holes + `floors_detail` entry where `detail["floor"] == n` (`gaps`), merging overlapping stairwell cutouts so zero-thickness slivers are skipped (`x1 - x0 >= 5`, `y1 - y0 >= 5`).
  3. Tag floor slabs `n >= 1` with `["floor_slab", "building=" + bid, "floor=%d" % n]` so `URoofCutawaySubsystem` (`RoofCutawaySubsystem.h:99-105`) hides upper floors cleanly above the selected operative's head (`HeadClearanceCm = 40`).

**Insertion 2 — `Source/CodexTactics/Private/LevelGen/PropsLayoutView.cpp:467-482, 691-715`:**
- Parse optional `floors_detail[].gaps[]` alongside `floor_holes` in `PropsLayoutView.cpp` so C4 props placement excludes all `gap` polygons (balconies/catwalks/stairwells) on upper floors.

---

### 2.4 Roof Hatches (`hatches[]`) & Free-Standing Ladder Navigation

**JSON contract verified in `L_Gen_Train_2.json:2037-2051, 2238-2258`:**
- Inside `building`: `hatches: [{ id: "B_tower_roof_hatch", pos: [-1220, -1630], floor: 0, size: [100, 100], ladder_id: "LAD_B_tower_hatch" }]`.
- Top-level `ladders[]`: `LAD_B_tower_hatch` (`from: "ground", to: "roof:B_tower", level_from: 0, level_to: 2, rungs: 16, pos: [-1230, -1630], foot: [-1330, -1630], top: [-1120, -1630], yaw: 180`).

**Insertion 1 — `Scripts/Editor/build_levelgen_map.py:265-289` & `Scripts/Editor/levelgen_ladders.py:111-119, 122-146`:**
- Currently `build_levelgen_map.py:265-275` cuts ONLY `b["roof_holes"]` from the roof slab (`roof_z = gz + floors * LEVEL_HEIGHT`). Consequently `L_Gen_Train_2`'s `B_tower` roof slab is **solid across `LAD_B_tower_hatch`**!
- Required change:
  1. For each `h` in `b.get("hatches", [])` (`pos = [hx, hy]`, `size = [sx, sy]` default `[100, 100]`): cut rectangular hole `(hx - sx/2, hy - sy/2, hx + sx/2, hy + sy/2)` out of the target slab above (all slabs at levels `> h["floor"]` up through `roof`, or specifically `roof` slab when `b["hollow"]` is true, as in `B_tower`).
  2. **Do NOT pass `hatches` into `levelgen_bot_route.drop_links()`** (`Scripts/Editor/levelgen_bot_route.py:160-193`) — hatch vertical traversal is two-way via `ALadderActor`'s `UNavArea_Ladder` smart link, not a one-way `DROP` `ANavLinkProxy`.
  3. Spawn optional visual hatch rim/coaming around the `size` perimeter **only on the 3 non-exit sides** (never blocking `ladder.foot -> ladder.pos -> ladder.top`).
  4. Verify `levelgen_ladders.py:111-119`: interior hatch ladders with no backing wall (`backing == None`, `levelgen_ladders.py:222`) already keep `pos` and orient `yaw` from `foot -> pos`; verify `Placer.place` (`build_levelgen_map.py:489`) spawns `ALadderActor` with `WallBracketCount = -1` (auto-detects free-standing `bHasWall = false`, `LadderActor.h:20-21, 71-73`).

**Insertion 2 — `Source/CodexTactics/Public/Ladders/LadderActor.h:90-95` & `LadderActor.cpp` (CRITICAL GEOMETRY CHECK):**
- Verified mismatch in `L_Gen_Train_2.json`: `B_tower_roof_hatch` spans `X [-1270..-1170]`, `LAD_B_tower_hatch` stands at `pos = -1230` and expects exit at `top = -1120` (`110 cm` behind `pos` on the roof slab). Meanwhile `ALadderActor::TopExitStandoff` defaults to `40.f` (`LadderActor.h:94`)! At `40 cm` behind `pos = -1230` (`X = -1190`), the top exit probe/nav-link point is **still inside the hatch hole (`-1270..-1170`)** rather than on solid roof (`>= -1170`)!
- Fix: Expose `TopExitStandoff` (`ExposeOnSpawn = "true"`) or set it in `build_levelgen_map.py:280-289` from `max(40.0, dist2d(pos, top))` (clamped safely onto solid slab >= `hatch_half + 35 cm` when `ladder.id == hatch.ladder_id`), OR widen/offset the hatch relative to `pos -> top` in `b.hatches` (see §4 Q2).

**Insertion 3 — `Source/CodexTactics/Private/LevelGen/PropsLayoutView.cpp:462-482`:**
- Parse `buildings[].hatches[]` into roof/upper-floor exclusions (`hole_rim_cm`) so props never cover hatch entry/exit slabs.

---

### 2.5 Roof Parapets (`parapets[]`)

**JSON contract verified in `L_Gen_Train_1.json:1180-1215`, `L_Gen_Train_2.json:2052-2085`:**
- `building.parapets[]`: `{ wall_id, start: [x, y], end: [x, y], height_cm: 100, thickness_cm: 20 }`.

**Insertion 1 — `Scripts/Editor/build_levelgen_map.py:76, 265-285` & `Scripts/Editor/levelgen_ladders.py:122-174`:**
- Today `build_levelgen_map.py:76` defines `PARAPET_H = 100.0`, while `L267` explicitly skips spawning (`# no parapet: the roof simply ends`).
- Required change:
  1. Construct wall-like dicts from `b.get("parapets", [])` with `"floor": floors`, `"a": p["start"]`, `"b": p["end"]`, `"thickness": p.get("thickness_cm", 20)`, `"height": p.get("height_cm", PARAPET_H)`.
  2. **MANDATORY:** Run these parapet segments through `levelgen_ladders.exit_gaps` + `split_walls` (`levelgen_ladders.py:122-174`) BEFORE spawning! Otherwise exterior ladders climbing to `roof:<b>` (e.g. `level_to == b["floors"]` on `B_house` or `B_checkpoint`) will be walled off at the roof edge by a `100 cm` solid parapet. Similarly, cut an opening (`STAIR_W` = `150 cm`) where any top-floor stair exits onto a parapet edge.
  3. Spawn surviving parapet pieces at `z0 = gz + floors * LEVEL_HEIGHT` via `wall_cube(...)` with `tags = ["parapet", "building=" + bid, "floor=%d" % floors]` (do **not** auto-tag outer perimeter roof parapets with `Vault` — see §2.6).
  4. `SightRules.cpp:125-127` sets `Crouching EyeHeight = 95 cm` (covered behind `100 cm` parapet) and `Standing EyeHeight = 160 cm` (shoots cleanly over `100 cm` parapet). Reconcile with `FallRules.h:96` `ParapetHeightCm = 80.f` (`LedgeFallComponent.cpp:733`, see §4 Q3).

**Insertion 2 — `Source/CodexTactics/Private/LevelGen/PropsLayoutView.cpp:725`:**
- `BuildLayers()` (`L725`) already generates `AddRect(LayerWalls..., Config.ParapetThicknessCm)` on walkable roofs. When `parapets[]` is present in JSON, prefer explicit `parapets[]` segments (respecting any open sides) with fallback to `AddRect` when absent.

---

### 2.6 Mantle Edge Markers (`mantle_edges[]`) & Vault Integration

**JSON contract verified in `L_Gen_Train_1.json:1367-1430`:**
- `building.mantle_edges[]` and top-level `layout.mantle_edges[]`: `{ id, start: [x,y], end: [x,y], floor, base_z, height_cm, normal: [nx, ny], kind, building?, wall? }`.
- Generated kinds include `window` (`height_cm: 90`), `parapet` (`height_cm: 100`), `crate` (`height_cm: 110`), `fence`, `obstacle` (cover heights up to `150 cm`).

**Insertion — `Source/CodexTactics/Private/Core/CodexTacticsGameMode.cpp:127-160`, `Source/CodexTactics/Private/Characters/VaultRules.cpp:37-53`, `Source/CodexTactics/Private/Characters/OperativeCharacterVault.cpp:72-162`:**
1. **Immediate vault support (existing system, `HeightCm <= VaultRules::MaxHeight = 105 cm`, `MaxLandingStep = 100 cm`):**
   - Ground-floor / upper-floor **window sills** (`90 cm <= 105 cm`, `thickness 30 <= MaxVaultableDepthCm 130 cm`) fit existing `VaultRules::CanVault` (`VaultRules.cpp:3-6`) as long as solid walkable floor/ground lies on both sides!
   - Either add `"window_sill"` (or `"low_wall"` tag in `build_levelgen_map.py`) to `VaultRules::IsVaultableCoverKind` (`VaultRules.cpp:39`) so `ACodexTacticsGameMode::StartPlay()` (`L134-159`) auto-tags spawned window sills with `VaultNavigation::VaultTag` (`"Vault"`) and builds `UNavArea_Vault` modifiers via `VaultNavigation::MakeVaultable`.
   - **Guardrail on outer roof `parapet` (`100 cm`):** Even though `100 <= MaxHeight (105)`, `OperativeCharacterVault.cpp:125` forbids `LandingStep > 100 cm`, so an operative cannot vault off a `300..600 cm` outer roof drop — **do NOT tag outer roof parapets with `"Vault"`**, or NavMesh will emit unusable `UNavArea_Vault` links over lethal drops! Tag parapets `"parapet"` + `"CodexMantleEdge"` (reserve `"Vault"` for `window_sill` and ground/interior low walls/barriers).
   - **Check `VaultRules::NavAreaExtent` (`VaultRules.cpp:23-35`) & `OperativeCharacterVault.cpp:121-123`:** verify `UNavModifierComponent` height on `window_sill` does not overlap the `210..300 cm` lintel and `TryVault` capsule landing check clears the `120 cm` wide window opening (operative radius ~`35 cm` -> `70 cm < 120 cm`).
2. **Metadata for future `50..150 cm` mantling system (`MAX_MANTLE_HEIGHT_CM = 150` vs `VaultRules::MaxHeight = 105`):**
   - Tag every actor spawned for a `mantle_edges` feature (`window_sill`, `parapet`, vaultable/climbable covers) with `CodexMantleEdge` (`build_levelgen_map.py`) AND add lightweight parsing of `layout["mantle_edges"]` in `PropsLayoutView` / future `UMantleSubsystem` so ledges `105..150 cm` (e.g. `110 cm` crates or `120..150 cm` roofs/containers) are indexed with `{ Start, End, Normal, BaseZ, HeightCm, Kind }` without prematurely making `VaultRules` accept `> 105 cm` jumps before animations exist.

---

## 3. Smokes & Automated Checks to Add / Extend

1. **Extend `PropsLayoutViewTest` (`Source/CodexTacticsTests/Private/LevelGen/PropsLayoutViewTest.cpp`):**
   - Load `L_Gen_Train_1.json` .. `L_Gen_Train_3.json` (plus `gen_ref_01_02_depot_1.json` regression).
   - Verify old files without new fields load cleanly; verify `floors_detail[].gaps` and `hatches[]` exclude props; verify `GetSightBlockers` passes LOS through ground/upper-floor `window` openings (`sill 90..top 210` at `EyeHeight 160`).
2. **Extend `LevelGenRouteSmoke` (`Source/CodexTacticsDev/Private/Debug/LevelGenRouteSmokeCommand.cpp:188-355`):**
   - Already resolves `floor:<b>:<n>` and `roof:<b>` heights via `LayerLevel` (`L111-141`).
   - Extend `Step` (`L266-355`) to record stair/hatch/ladder usage checks: when `Leg.LevelFrom != Leg.LevelTo` and `!Leg.Via.StartsWith("hole")`, assert squad/leader actually traverses `floor:<b>:<n>` / `roof:<b>` via stair or `ALadderActor` (`Via.StartsWith("LAD_")`) within `LevelTolCm` rather than silently succeeding only at endpoints, and run against:
     - `/Game/Maps/Generated/L_Gen_Train_1` (2-storey house block, stairs + partitions)
     - `/Game/Maps/Generated/L_Gen_Train_2` (fortified checkpoint + guard tower `LAD_B_tower_hatch` + loopholes)
     - `/Game/Maps/Generated/L_Gen_Train_3` (warehouse yard + multi-storey rooms/catwalks)
3. **Run / extend `LevelGenLaddersSmoke` (`Source/CodexTacticsDev/Private/Debug/LevelGenLaddersSmokeCommand.cpp:1-90`):**
   - Verify `LAD_B_tower_hatch` on `L_Gen_Train_2`: free-standing (`HasWall() == false`), `IsClimbable() == true`, climbs `ground (0) -> roof:B_tower (2)` through the `100x100` hatch cutout and back down, validating `TopExitStandoff` lands on solid roof navmesh.
4. **New `LevelGenOpeningsSmoke` (`Source/CodexTacticsDev/Private/Debug/LevelGenOpeningsSmokeCommand.cpp`):**
   - Uses `FindLayoutFile` (`LevelGenRouteSmokeCommand.cpp:150-186`) to load `buildings[].openings[]` & `parapets[]`.
   - Checks (a) **Window LOS:** standing eye (`Z + 160`) through every `window` reaches `>= 200 cm` past wall along `yaw`, while apron trace (`Z + 45`) and lintel trace (`Z + 250`) hit solid wall; (b) **Loophole slit:** trace at mid-aperture (`Z + 135`) passes clear while (`Z + 60`) and (`Z + 200`) hit wall (plus standing eye test per §4 Q1); (c) **Window vault:** places leader facing a ground-floor `window` in `L_Gen_Train_1` (`DepotVaultSmokeCommand.cpp` style), verifies `VaultNavigation::IsVaultable` and successful `TryVault` across the sill into the adjacent room; (d) **Parapet check:** verifies roof parapets block crouching eye (`Z + 95`) while clearing standing eye (`Z + 160`) and leaving roof ladder exits open.
5. **Regression:** re-run `DepotVaultSmoke`, `LevelGenLaddersSmoke`, and `DropSoftlockSmoke` (`L_Gen_Depot_1`, `L_Gen_Train_1..3` — specifically confirming `sweep` excludes `hatches` from `LevelGenDrop` links).

---

## 4. Open Decisions / Mismatches to Confirm Before Merging

1. **Loophole `120..150 cm` vs `SightRules::EyeHeight(Standing) = 160 cm` (`SightRules.cpp:124-128`):**
   - Standing operative eye (`160 cm`) is `10 cm` *above* a `150 cm` loophole lintel bottom, while Crouching eye (`95 cm`) is `25 cm` *below* the `120 cm` sill! Without an adjustment, neither standing nor crouched LOS traces (`UTacticalSightSubsystem`) will shoot out of a `120..150 cm` bunker slit.
   - *Default recommendation:* either set C++ loophole aperture / firing-port sight offset (`135 cm`) for operatives standing adjacent to a `loophole_sill` wall, OR raise loophole `top_height` from `150` to `175 cm` in builder/schema so `Standing EyeHeight = 160 cm` passes directly through `120..175 cm` while keeping `sill = 120 cm > VaultRules::MaxHeight (105 cm)`.
2. **`ALadderActor::TopExitStandoff` (`40 cm`, `LadderActor.h:94`) vs `[100, 100]` Hatch Opening (`L_Gen_Train_2.json:2039-2050, 2238-2258`):**
   - `B_tower_roof_hatch` spans `50 cm` half-width (`-1270..-1170`), `LAD_B_tower_hatch` sits at `pos = -1230` (`60 cm` from far slab edge `-1170`, `top = -1120` = `110 cm` behind `pos`). A fixed `40 cm` `TopExitStandoff` lands inside the empty hole (`-1190`).
   - *Default recommendation:* make `TopExitStandoff` (and `TopStandoff`) `ExposeOnSpawn = true` or derived per-ladder in `build_levelgen_map.py` when `abs(pos -> top) > 50` (set `TopExitStandoff = dist(pos, top)` = `110 cm` clamped safely on roof slab) so `LAD_B_tower_hatch` exits cleanly onto `roof:B_tower` navmesh.
3. **Parapet Height (`FallRules.h:96` `80 cm` vs schema `100 cm`):**
   - Keep `height_cm = 100` in map builder (`L_Gen_Train_1..3` use `100`), matching crouched cover (`95 cm`) + standing overwatch (`160 cm`); confirm with designer whether `FallRules::ParapetHeightCm` (`80 cm`) stays an independent knockback threshold or aligns to `100 cm`.
4. **Wall Builder Location (Extend Python `build_levelgen_map.py` vs New C++ Editor Builder):**
   - *Default recommendation:* implement geometry spawning in `Scripts/Editor/build_levelgen_map.py` + `levelgen_ladders.py` (where all current `.umap` building, `wall_cube`, `slabs_with_holes`, `ALadderActor`, `ANavLinkProxy`, and `bake_navmesh.ps1` live) AND implement JSON sight/exclusion/tag/smoke support in C++ (`PropsLayoutView.cpp`, `VaultRules.cpp`, `CodexTacticsGameMode.cpp`, `Source/CodexTacticsDev/Private/Debug/`).
