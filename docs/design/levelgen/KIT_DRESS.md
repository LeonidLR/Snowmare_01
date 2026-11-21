# Kit dressing: generated buildings in the Soviet building kit

Generalises the one-building pilot (`KIT_PILOT_2026-11-20.md`, `Scripts/Editor/kit_pilot_building.py`) to every generated building that has
a facade **series**. Visuals only: the generator's grey boxes stay as hidden proxies, so gameplay (cover, sight, mantle edges, door states,
navmesh, the walking ramps) is the generator's, bit for bit. The pack (`Content/SovietHouses`, FinFunGames) is untracked and never
committed; so are the maps that use it (`/Game/Maps/KitPilot/*`).

| File | What |
| --- | --- |
| `Scripts/Editor/kit_dress_lib.py` | the pure part: series lookup, fitting rules, the per-building `Dresser` (no `unreal`) |
| `Scripts/Editor/kit_dress.py` | the editor part: builder as a library, the map, instancing, level JSON, rendered shots; `plan` / `export` under system python |
| `Content/Data/LevelGen/kit_series.json` | the series table (series -> modules per element; archetype defaults) |
| `Scripts/kit_dress.ps1` | runner: export (optional) -> build -> bake navmesh -> `LevelGenBuildingsSmoke` -> shots |
| `Scripts/Editor/tests/test_kit_dress.py` | unit tests (`python -m unittest Scripts/Editor/tests/test_kit_dress.py`) |

```
powershell -ExecutionPolicy Bypass -File Scripts\kit_dress.ps1 -Ref claude/building-generator      # the kit block plans of that branch
powershell -ExecutionPolicy Bypass -File Scripts\kit_dress.ps1 -Layout Content\Data\LevelGen\accepted\<layout>.json
powershell -ExecutionPolicy Bypass -File Scripts\kit_dress.ps1 -Ref claude/building-generator -Grey -Steps build,bake,smoke   # baseline
python Scripts/Editor/kit_dress.py plan [layout.json] [builder root]                                   # counts, no editor
```

Story showcase views (2026-11-21 night, `STORY_SHOWCASE_2026-11-22.md`): `KIT_DRESS_SHOT_SET=story` (kit_dress.py `story_cameras`: overview,
every building's front 3/4, each story element close up - locked / sealed door, rubble flight + roof-off, fire escape, roof ladder, broken
windows, snow ramp), `KIT_DRESS_SHOT_PREFIX=showcase_`, `KIT_DRESS_SHOTS_DIR=Saved\StoryShowcase\shots`, with
`kit_dress.ps1 -Layout Content\Data\LevelGen\accepted\gen_story_showcase.json -Target /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit`.

`-Ref` runs `kit_dress.py export <ref> gen_bgen_plans_check`: `git show` of that branch's builder (`build_levelgen_map.py`, its two helper
modules, `building_coord_fields.json`, `levelgen_v2.json`) and accepted layout into `Saved/KitBlocks/builder/<ref>/` (read only; no checkout).
The dresser then uses THAT builder, so a layout with fields this checkout's builder does not know yet (the switchback cores) is built the
way its own branch builds it. The level JSON's `levelgen_layout` points at the layout actually used (a path under the project).

## How a building is read (why new plans need no change)

Nothing depends on a plan kind, building id or room layout. Per building with a series (record `series` if the table has it, else
`archetype_default[archetype]`; enterable buildings only):

| Element | Read from |
| --- | --- |
| Facade sides | the footprint rectangle (a rotated building is dressed in its own frame - the builder's `_building_local` - and turned back) |
| Windows / entrances / balcony doors | the layout's `openings` (+ the builder's synthesised balcony doors) within 20 cm of a side: window -> window bay by width; door on storey 0 -> entrance bay; door above -> balcony-door bay; loophole -> left grey |
| Gaps nobody explains | the builder's wall pieces of that side and storey: an uncovered stretch >= 20 cm that is no opening (a ladder exit) stays open |
| Wall thickness | the builder's full-height wall pieces of that side |
| Stair glass column | every `stairwell` room touching a side; aligned on a ground entrance in that span; dropped if a layout window falls into it |
| Interior walls, lintels, sills | every builder piece tagged `interior` (partitions, a switchback core's lane walls, legacy `walls[]`); stair paint when it lies in a stairwell room |
| Doors | `partitions[].openings` (role `unit_entry` / `room` / `enfilade` / any; `portal` = no leaf); open / closed = the builder's `effective_opening_state`; barricaded / sealed keep the builder's blocking pieces visible |
| Stairs | the builder's ramps of each `stairwell` stair (labels `<bid>_<stair id>` and `_b`): two ramps (+ the `_midlanding` slab) = **switchback** -> one kit switchback set per storey; one ramp = **single flight** -> stacked kit step blocks |
| Parapets, firing-gap sills | every builder piece tagged `parapet` |
| Floors / ceilings / roof | `rooms` x the builder's `floor_slab` / `roof_slab` pieces (planes on the slab, ceilings under it, bitumen on the roof) |

Tags such as `structure` / `material` / `vaultable` are carried by the builder pieces untouched (the proxies keep every tag). Checked on
both plan generations: the checkout's L_Gen_BgenPlans layout (single-flight cores) and the kit block plans of `claude/building-generator`
(court-side switchback cores, 3 flats per landing) - the same code, unit-tested on both (`DressLayoutTest`).

## The series table (`kit_series.json`, schema `codex.kit_series/1`)

Top level: `kit_root`, `materials_root`, `plane_mesh`, `stretch_max` (0.10), `archetype_default` {archetype: series}, `series` {name: {...}}.
Mesh paths are relative to `kit_root`; module frames as dumped in `Saved/KitDump/kit_buildings.json` (facade modules x 0..300 along the
wall, y -30..0 with the outer face at y = 0, z 0..storey). Per series:

| Key | Content |
| --- | --- |
| `storey_cm`, `module_cm`, `depth_cm` | the kit storey (heights are stretched to our 300 cm), module width, facade depth |
| `solid` | the plain wall module (fillers, jambs) |
| `windows` | list `{id, wall, glass, hole [x0, x1], hole_z [z0, z1]}` - the window bay is chosen by opening width |
| `balcony_door` | `{wall, glass, door_hole, window_hole, balcony, balcony_x, balcony_depth_cm}` |
| `entrance` | `{wall, height_cm, door, hole, porch, porch_z_cm, canopy, canopy_z_cm}` |
| `stair_column` | `{wall, glass, height_cm, strip, strip_height_cm}` (the stairwell's facade column, topped by a strip) |
| `plinth`, `parapet`, `cornice`, `drain`, `radiator` | single meshes with their heights / anchors; `cornice` / `drain` may be null |
| `interior_walls` | list `{mesh, thickness_cm, height_cm}` - the nearest thickness wins |
| `stair_wall` | the stairwell paint wall |
| `doors` | `{<role>: [candidates {mesh, width_cm, height_cm, local, framed}], single_max_cm}`; `room` is the fallback role |
| `stairs.switchback` | the kit switchback set and its frame: `lane_x`, `floor_edge_y`, `turn_y`, `landing_end_y`, `floor_landing_y`, `rise_cm` (vertex probe 2026-11-21: floor landing y -590..-356 at z 0, flight 1 x 10..140 rising +y to the half landing y -160..-30 at z 140, flight 2 x 160..290 back) |
| `stairs.flights` | step blocks for single flights `{mesh, rise_cm, run_cm, width_cm, centre_x, foot_y, foot_z}` (rising along -y) |
| `floors` {room kind: material}, `ceiling`, `roof` {material, props [{mesh, at [u, t]}]} | finishes |
| `slot_balconies` | how many `slot: balcony` windows get a visual (unreachable) balcony |

Series now: **`panel_1-464`** (the kit's Facade series = its 5-storey block L_SH_Building_20; default for `block`) and **`brick_1-447`**
(BrickR modules, wooden windows / entrance; default for `office` until offices get their own).

### Adding a series (example: Stalin brick / plaster)

1. Pick the kit building that shows it (`Saved/KitDump/kit_plans.md`: 05 and 18 are Stalin plaster, 3.2 m storey) and look at its plan
   images (`Saved/KitDump/plans/Building_05_*.png`) for the modules it uses.
2. Copy the `brick_1-447` entry to `stalin_plaster` and swap the meshes: `Wall_Plaster_solid_32`, windows `Wall_Plaster_win_32` +
   `Win_Wood_win_32` and `Wall_Plaster_tall_32` + `Win_Wood_tall_32` (check their holes in `kit_buildings.json` / a vertex probe -
   the 28-series values do not carry over), `StairEnt_Plaster_26`, `StairWall_Plaster_32_w` + `StairWall_Plaster_16_x`,
   `Plinth_Plaster_13`, `Parapet_Plaster_9`, cornice `Cornice_Plaster_stalin`, balcony `Balc_Stalin`, `storey_cm` 320 (heights x 0.9375).
3. Map it: `archetype_default` for a new archetype, or the generator writes `"series": "stalin_plaster"` on the building record.
4. `python -m unittest Scripts/Editor/tests/test_kit_dress.py` (the table test checks the keys), `kit_dress.py plan`, then the map.

## Stretch rules

- Heights: every module of a `storey_cm` series x 300 / storey_cm (28-series 1.0714).
- Window: the smallest hole that reaches the opening at +10 %; stretched within +-10 %; a kit hole still wider than the opening gets
  wall jamb strips inside it (the visual never shows more opening than the gameplay one); an opening wider than the widest kit hole at
  +10 % is reported (`narrower`).
- Entrance: the door hole stretched to the door within +-10 % (130 -> 143 cm in our 150 cm doors).
- Doors: one leaf up to 130 cm, two above; the first candidate within +-10 % of a leaf, else the nearest narrower one with wall jambs;
  height within +10 %, the rest filled with a wall strip under our lintel.
- Fillers between bays: exact tiling when a width within +-10 % fits; otherwise full modules sliding up to a neighbour's jamb (overlap
  mode, alternately recessed 0.3 / 0.15 cm) and only a gap too small for that is squished (`KIT_DRESS_FILLER=squish`: contiguous, any width).
  Plinth, parapet, cornice and interior walls tile their run exactly (plain paint / brick courses, no openings).
- Stairs: switchback lanes x (lane spacing / 150), flights x 0.9..1.1 and shifted along the flight to the least gap between the kit steps
  and our ramp, with the kit floor landing kept inside the outer wall; our mid landing stays visible where the kit's half landing does not
  reach. Single flights: the step block and count with the least stretch (old 300 / 429 cm flights: 2 x Steps_10, z 1.154).
- Every stretch is in `Saved/KitBlocks/kit_dress_report_<map>.json` (`stretch`: per mesh min / max x and z, count beyond 10 %).

## Instancing

One actor per building (`StaticMeshActor` `Kit_<id>`, tags `KitDress`, `kit_building=`, `kit_series=`) with one
`InstancedStaticMeshComponent` per mesh / material / mirror / group, added through `SubobjectDataSubsystem` (they are saved with the map;
the build reads the count back). Group `roof` (parapets, cornice, roof planes, roof props) carries the component tag `kit_roof` (the shots
hide it for the roof-off views). NoCollision, CanEverAffectNavigation false. The ISM path needs the kit materials' **Used with Instanced
Static Meshes** flag (without it the game draws the default checker material): the build sets it on the base materials it instances and
saves them - a local change of the untracked pack, once per checkout.

## Result 2026-11-21 (`/Game/Maps/KitPilot/L_KitBlocks`)

Layout: `gen_bgen_plans_check.json` of `claude/building-generator` e9920330 (the L_Gen_BgenPlans layout regenerated with the kit block plans:
court-side switchback cores), built with that branch's builder (`kit_dress.ps1 -Ref claude/building-generator`). 4 blocks (`panel_1-464`)
and 5 offices (`brick_1-447`) dressed.

| Building | Archetype | Series | ISM components | Instances | Hidden proxies | Visible grey pieces |
| --- | --- | --- | --- | --- | --- | --- |
| B_x (12 x 36 m, 2 storeys) | block | panel_1-464 | 24 | 1638 | 488 | 14 (slabs, 4 mid-landing strips) |
| B_y (12 x 37 m, 3 storeys) | block | panel_1-464 | 24 | 2399 | 724 | 21 |
| B_b18 | block | panel_1-464 | 24 | 660 | 187 | 10 |
| B_b20 | block | panel_1-464 | 24 | 1400 | 406 | 15 |
| B_o10 / o11 / o12 / o12h / o12w | office | brick_1-447 | 24 each | 329 / 362 / 364 / 419 / 415 | 83 / 83 / 83 / 95 / 95 | 8-10 (slabs) |

Total: **9 kit actors (one per building), 216 ISM components, 7 986 instances**; the pilot's approach would have been ~8 000 actors (it
spawned 779 for B_x alone). The level has 2 368 actors, 2 354 of them the builder's (2 244 hidden proxies) - the builder itself does not
instance its grey boxes (S2_ROOMS_PLAN risk 2, unchanged here).

- `LevelGenBuildingsSmoke` (`-LevelGenBuildingsSmokeMaxFail=10`): L_KitBlocks and the grey copy L_KitBlocks_Grey (same layout and builder,
  `-Grey`) give **identical reports** (minus the map name): 7 / 9 pass; B_y (82 of 240 paths) and B_b18 (17 of 31) fail on BOTH, so the
  failures are the layout / builder's (or this checkout's older smoke binaries), not the dressing. Navmesh: both 277 Default / 168 Vehicle tiles.
- In-game checks (shots mode): the hidden proxies block sight and the navmesh at a solid wall end, a window stays open for sight - PASS;
  9 kit actors / 216 components / 7 986 instances read back from the running game.
- Pictures `Saved/KitBlocks/shots/kitblocks_*.png`: overview_34, map_top_down, street_facade_34 (B_y), court_entrances_34,
  entrance_closeup, window_closeup, stairwell_interior, stairwell_above_roof_off, flat_interior, top_down_roof_off, office_facade_34.
- The old single-flight layout of this checkout dresses too (`kit_dress.py plan`: 4 blocks + 5 offices, 2 x Steps_10 per flight; unit test).

## Known gaps

1. **Fake openings**: stair glass and balcony-door bays are visual only (the proxy wall behind is solid: they block sight and are not
   reachable); `slot: balcony` balconies have no collision.
2. **Roof cutaway** (in game) does not hide the kit visuals (no collision for its trace); the shots use their own camera and hide the
   `kit_roof` components.
3. **Stairs**: switchback steps within ~13 cm of our ramp (kit flight 196 cm vs our 215 cm; kit floor landing 234 cm deep vs 229 cm to the
   outer wall); single flights are stacked step blocks (risers 19 cm).
4. **Doors** are wider than the kit's (150 vs 80 / 102 / 130 cm): flat doors are pairs of interior leaves, entrances x 1.1.
5. **Window grid**: the generator's 50 cm room grid vs the kit's 300 cm bays -> squished / overlapping fillers at corners (plain paint:
   only the panel joints come closer).
6. **Ceilings / floors** are planes per 200 cm cell (UV 0..1 each); slab edges stay grey box.
7. The court side of the generated maps is in the fixed sun's shadow at 13:00; the shots add exposure on shaded facades.
8. Offices use the brick block series (no office modules in the kit); non-enterable buildings stay grey; hatches / external stairs /
   snow ramps / blocked flights keep their grey pieces.
9. No snow cover in the level JSON (it would land on the interior floors).
10. The **first** build (2026-11-21) also flagged and saved the engine's `/Engine/EngineMaterials/WorldGridMaterial` (the engine Plane's
    default material) - fixed the same day (only materials under `materials_root` are touched now). That engine-install file stays
    modified until UE 5.8 is verified in the Epic launcher (the flag is harmless: it is a special engine material compiled for every usage).
