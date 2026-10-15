# Demo 01 «Frozen Lake»: auto layout (2026-10-13)

> **2026-10-15 rev 5h:** additive section `AutoLayout_Demo01_5h` (the A3 corner-peek mines, §9); the additive mode now runs the sections named in `CODEX_LAYOUT_SECTIONS` (default `5h` = only the newest; `5d,5h` = both).

> **2026-10-15: ONE lake map.** The review copy became the working map: `/Game/Maps/Dev/L_Demo01_A_FrozenLake_AutoLayout` was saved over `/Game/Maps/L_Demo01_A_FrozenLake` (editor Save As, user decision) and the copy + its level JSON were removed. Every path below that names the `Dev/...AutoLayout` copy now means `/Game/Maps/L_Demo01_A_FrozenLake`. Backups of both old maps: `Saved/Backups/maps_before_swap_20261015_0917`.


The user asked: "build everything on the level yourself (beacons / volumes / markers / objects); I will adjust positions by
hand afterwards". This is that first pass, made by script on a **copy** of the user's map (committed on `agents/work`, 0ea2635).

**Rev 5d (2026-10-14): the copy is the user's now (he edits it by hand) - only the ADDITIVE mode may run on it (section 8):
the ruin of the A2 barrel lesson (tag `AutoLayout_Demo01_5d`), no pre-existing actor moved. Verify PASS (incl. a real barrel push
into the ruin), screenshots re-shot (incl. `_ruin.png` / `_ruin_angle.png`).**

**Rev 5c (2026-10-13, the designer's review: "looks good for a start"):** the copy was re-laid in place (no fresh copy; the
user's map untouched, SHA256 checked) with the **tactical hut entry** (§2a) and the bridge marker re-id'd `A1_solo` ->
**`A1_bridge`** (the bridge teaches nothing now). 101 actors placed (was 94). Verify **PASS**, screenshots re-shot (§4).

| What | Where |
|---|---|
| Working copy (the only map that was modified) | `/Game/Maps/Dev/L_Demo01_A_FrozenLake_AutoLayout` (`Content/Maps/Dev/L_Demo01_A_FrozenLake_AutoLayout.umap`) |
| User's map (NOT modified, SHA256 checked before / after every run) | `/Game/Maps/L_Demo01_A_FrozenLake` (`3622cd28...5945fa1`) |
| Backup of the user's map taken first | `Saved/Backups/user_wip_20261013_layout_1529/L_Demo01_A_FrozenLake.umap` (same SHA256) |
| Placement script (idempotent) | `Scripts/Editor/build_demo01_lake_layout.py` |
| Dumps (every actor: class, label, transform, bounds, tags, tutorial ids) | `Saved/LayoutDumps/L_Demo01_A_FrozenLake_user_20261013_153428.json` (user's map as found), `..._AutoLayout_before_*.json` / `..._after_*.json`, `demo01_autolayout_table.json` (placement table) |
| Verify result | `Saved/Logs/Demo01LayoutVerify.txt` (`RESULT: PASS`) |
| Screenshots (in-engine top-down captures, labels drawn afterwards) | `Saved/Screenshots/demo01_autolayout_top.png` + close-ups `_crossing.png`, `_approach.png`, `_compound.png` |

Source: `docs/demo/DEMO01_BEAT_SHEET.md` rev 5 §4 (beats) and §10.1 (build list), `Content/Data/Tutorial/demo_01_steps.json`,
HANDOFF §6 «Tutorial» / «How to place a PMC tripwire».

## 1. What the user's map had (anchors)

The map is small and flat (no landscape): two template floor slabs `Floor` (X -22480..-6480) and `Floor2` (X -6363..40459),
both Y +-11250, top at Z = 0, and a **1.2 m gap between them = the user's crevasse** across the whole width, bridged only by the
plank `Floor3` (X -6478..-5985, Y 1804, 1.23 m wide). The user's fishermen's hut (cubes + roof, camera zone `Trigger_BoatsCabin`
Shelter + `Camera_BoatsCabin`, `LootCrateActor`, marker `A1_obj_hut`) stands at (-2180, -7090). `PlayerStart` (-19225, 1289),
`VOL_A0_BEACON1` (Beat A1) + marker `A0_move` at (-17367, 2010). `NavMeshBoundsVolume` X -21657..10343.

Not in the map: `VOL_A1_BEACON2/3/4`, `VOL_A1_HUT`, `VOL_A1_RIDGE(_FAR)`, `VOL_A2_CAMP`, `VOL_A6_FIELD`, a camp, the complex.
So **no re-id was needed** (the script would rename `VOL_A1_BEACON4 -> VOL_A3_WIND`, `VOL_A1_RIDGE(_FAR) -> VOL_A3_RIDGE(_FAR)`,
`VOL_A6_FIELD -> VOL_A3_FIELD` on the user's own volumes if they existed, and keep them in place); every missing id was placed.
User actors kept exactly as they were: all 25 (only the two generated `RecastNavMesh-*` actors are dropped, see §4).

## 2. What was placed (94 actors, tag `AutoLayout_Demo01`, Outliner folder `AutoLayout_Demo01/<section>`)

Coordinates are world cm (centre of a volume, ground point of the others). +X = the direction of travel.

| Actor | Class | Id / settings | Position | Relative to |
|---|---|---|---|---|
| VOL_A1_BEACON2 | TutorialTriggerVolume | Leader, box 5 x 10 m | (-6928, 1804) | 4.5 m before the user's plank (west end) |
| Marker_A1_bridge | TutorialMarkerActor | Step `A1_bridge` (rev 5c; was `A1_solo`), ring 1.2 m | (-6658, 1804) | near end of the bridge |
| BridgeCrossing_A1 | BridgeCrossingVolume | Capacity 1, span 6.9 x 2 x 2.5 m, local X along the span | (-6231, 1804, 124) | user plank `Floor3` + 1 m each end |
| WindZone_A1_FarBank | CameraZoneVolume | Blizzard x2.5, camera switch off, 12 x 12 m | (-5285, 1804) | far side of the bridge |
| VOL_A1_HUT | TutorialTriggerVolume | Any Operative, **Fire Once off**, the hut INTERIOR only (inner wall faces - 40 cm), 9.7 x 6 m (rev 5c) | (-2197, -7113) | user hut walls (§2a) |
| Marker_A1_loot_open | TutorialMarkerActor | Step `A1_loot_open`, ring 0.9 m around the crate | (-1740, -7010) | user's hut crate |
| VOL_A1_BEACON3 | TutorialTriggerVolume | Leader | (2520, -5290) | hut + (47, 18) m |
| VOL_A2_CAMP | TutorialTriggerVolume | **Beat A2**, Leader | (5820, -4690) | hut + (80, 24) m, 22 m before camp hut 1 |
| Camp hut 1 (5 walls + roof, door 2 m west) | StaticMeshActor x6 | 6 x 4 x 2.4 m | (8020, -4490) | hut + (102, 26) m |
| Camp_Hut1_Lee_Shelter | CameraZoneVolume | Shelter x0.5, camera off | (8580, -4490) | east (lee) side of camp hut 1 |
| Camp_Barrel_1 / Camp_Barrel_2_Spare | BarrelActor | unlit | (7020, -5090) / (8620, -3590) | 10 m west of hut 1 in the wind / spare |
| Camp_FishRack_0/1 | StaticMeshActor | low cover 1.0 m | around hut 1 | |
| VOL_A3_APPROACH | TutorialTriggerVolume | **Beat A3**, Leader | (11020, -2490) | hut + 132 m (lane Y = hut Y + 46 m) |
| WindStrip_A3 + VOL_A3_WIND | CameraZoneVolume + TutorialTriggerVolume | Blizzard x2.5 camera off; volume Any Operative, same 8 x 30 m box | (12820, -2490) | 18 m past the approach volume |
| VaultRidge_A3 | StaticMeshActor, tag `Vault` | 0.8 m high, 8 m long | (14820, -2490) | 20 m past the wind strip |
| VOL_A3_RIDGE / VOL_A3_RIDGE_FAR | TutorialTriggerVolume | Leader | (14370 / 15270, -2490) | 4.5 m before / after the ridge |
| LowFence_North / LowFence_South | StaticMeshActor | 1.2 m solid fence, closed from floor edge to floor edge | X 17020 | 22 m past the ridge |
| LowFence_GapRail | StaticMeshActor | rail 0.9-1.2 m over the gap, no collision, no navmesh effect (look only) | (17020, -2490) | |
| CrawlGap_A3 | CrawlGapActor | Gap Width 600, Clearance 90, Bar Top 200, Depth 20; local X across the fence | (17020, -2490, 0) | the only opening in the fence |
| VOL_A3_FENCE | TutorialTriggerVolume | Any Operative | (16670, -2490) | 3.5 m before the fence |
| VOL_A3_FIELD | TutorialTriggerVolume | Leader | (17920, -2490) | cover field entrance, 9 m past the fence |
| Cover_A3_1..5 | StaticMeshActor | zigzag: low 1.0 m x 4 m / high 2.2 m x 3 m, 6 m apart, +-6..8 m off the lane | X 18820..21220 | |
| PMC_Mine_A3_1 / _2 | ProximityMineActor | level mine (hidden until spotted), damage 80, 5 m apart | (19720, -2540) / (20220, -2440) | the straight "shortcut" through the middle of the field |
| Rock_A3_GapNorth / South | StaticMeshActor | 3 x 6 x 2.2 m, 3 m gap | X 22020 | field exit |
| PMC_Tripwire_A3 | PMCTripwireActor | PMC, hidden, anchors (0, +-140, 0) = 2.8 m wire across the gap | (22020, -2490) | the obvious gap at the field exit |
| VOL_A4_OVERLOOK | TutorialTriggerVolume | **Beat A4**, Leader | (23320, -2490) | hut + 255 m |
| Overlook_LowCover | StaticMeshActor | low 1.0 m x 6 m | (23720, -2490) | overlook |
| Container_A4_North / South | StaticMeshActor | 2.4 x 6 x 2.6 m, 3 m choke between | X 25320 | 20 m past the overlook |
| PatrolRoute_A4_Frostbitten | PatrolRouteActor | linear closed loop, 4 points, 61 m, through the choke | pivot (25320, -2490) | choke |
| PatrolRoute_A4_Hounds | PatrolRouteActor | linear closed loop, 4 points, 39 m, through the choke | pivot (25320, -2490) | choke |
| PatrolSpot_Frostbitten_1/2, PatrolSpot_Hound_1/2 | TargetPoint | where each patrol member stands (place `BP_Enemy_*` here in Phase 2) | route corners | |
| Compound walls (6) | StaticMeshActor | 40 x 40 m, 2.5 m high; gate 4.5 m west (towards the overlook), back gap 2 m east | X 28320..32320 | hut + 305 m |
| Base walls (5) + Base_Roof | StaticMeshActor | main building 10 x 18 x 3.2 m, door 1.6 m in its west wall | (31420, -2490) | east side of the yard |
| Base_Interior_Camera + Base_Interior_Shelter | CameraActor + CameraZoneVolume | Shelter, fixed camera (same pattern as the user's hut) | inside the base | |
| VOL_A5_BASE | TutorialTriggerVolume | **Beat A5**, Leader | (30670, -2490) | in front of the base door |
| Base_Crate_Clean | LootCrateActor | rifle ammo 30, medkit 1, matches 2 (rest 0) | (31670, -3190) | supply corner |
| Base_Crate_Trapped | LootCrateActor | trapped, trap damage 60; heat pack 1, canned food 1 (rest 0) | (31670, -1790) | supply corner |
| Base_Barrel_Outside, Yard_Barrel_1/2 | BarrelActor | unlit | yard | A5 warm-up + A6 "3 barrels" |
| Yard_Barricade_1/2 | BarricadeActor | | near the gate | A6 "move / mine a barricade" |
| VOL_A6_PREP | TutorialTriggerVolume | **Beat A6**, Leader, **Required Beat A5** (so passing the yard before the base cannot start A6 first) | (29720, -2490) | yard centre |
| HighGround_Platform + HighGround_Ramp | StaticMeshActor | 4 x 4 m, 2 m high (elevated wind applies), ramp 1.2 m wide at 24 deg | (28620, -4190) | yard SW corner |
| VOL_A6_HIGH | TutorialTriggerVolume | Any Operative, on the platform top | (28620, -4190, 300) | |
| GeneratorShed_Wall_0..4 | StaticMeshActor | 4 x 3 x 2.4 m, door 1.6 m towards the yard | (30320, -790) | A7 / A10 placeholder |
| BeaconPole_01..10 | StaticMeshActor (cylinder) | 4 m amber-road poles, no collision | every ~25 m: beacon 1 -> bridge -> hut -> beacon 3 -> camp | |
| NavMeshBounds_AutoLayout | NavMeshBoundsVolume | covers every placed actor + 20 m (X -19400..34300, Y -9100..6500, Z -300..700) | | the user's bounds stop at X 10343 |

Counts (rev 5c): TutorialTriggerVolume 17, TutorialMarkerActor 5, CameraZoneVolume 4, BridgeCrossingVolume 1, CrawlGapActor 1,
ProximityMineActor 2, PMCTripwireActor 1, PatrolRouteActor 2, TargetPoint 4, BarrelActor 5, LootCrateActor 2, BarricadeActor 2,
CameraActor 1, NavMeshBoundsVolume 1, StaticMeshActor 53 (graybox; + the door jamb).

### 2a. The tactical hut entry (rev 5c, `demo_01_steps.json` `A1_wt_hut_approach` .. `A1_hut_enter`)

Read from the user's hut geometry (`find_hut_frame` / `hut_sides` / `build_hut_entry` in the script): the walls are the
axis-aligned static meshes >= 1.8 m tall round the Shelter zone `Trigger_BoatsCabin` - west `Cube` (X -2747), east `Cube3`
(X -1647), south `Cube4` (Y -7478), north `Cube6` (Y -6748, X -2317..-1614), 50 cm thick, 3.5 m high (+ the roof `Cube7`
3.3-3.8 m, which juts ~1.9 m out over the north wall). Interior X -2722..-1672, Y -7453..-6773. **The door = the only opening:
the north side, X -2722..-2317 (4.04 m), between the west wall's end and `Cube6`** - the side facing the approach from the
bridge (it comes from the north-west), so the choice is not ambiguous. Left / right = as seen by the squad arriving (facing
south): left = west (-X), right = east (+X).

| Actor | Class | Id / settings | Position | Why |
|---|---|---|---|---|
| **HutDoor_Jamb_L** | StaticMeshActor (engine cube, the hut wall's own material) | 2.54 x 0.5 x 3.5 m, collision, affects the navmesh | X -2772..-2518 on the north wall line (Y -6748) | **graybox, review / replace with art:** the 4 m opening had no wall to hug on its west side (only the 50 cm end of the west wall); the jamb narrows the door to **2.0 m** (X -2518..-2317) and gives the left stacker a wall |
| VOL_A1_DOOR_L | TutorialTriggerVolume | Any Operative; the cover `in_volume` filter of `A1_hut_stack_l` | (-2632, -6663), 2.7 x 1.2 x 3 m | the jamb's outer face, from the door edge 2.5 m west, 1.2 m out |
| VOL_A1_DOOR_R | TutorialTriggerVolume | Any Operative; `in_volume` of `A1_hut_stack_r` | (-2202, -6663), 2.7 x 1.2 x 3 m | `Cube6`'s outer face, from the door edge 2.5 m east, 1.2 m out |
| VOL_A1_HUT | TutorialTriggerVolume | see the table above (interior only, Fire Once off) | (-2197, -7113) | the stackers outside never stand in it; any operative may enter after a stacker |
| VOL_A1_HUT_APPROACH | TutorialTriggerVolume | Leader | (-2417, -5723), 24 x 6 m | 10 m in front of the door: the walk-and-talk + the select card |
| Marker_A1_hut_stack_l / _r | TutorialMarkerActor | Step `A1_hut_stack_l` / `_r`, ring 0.7 m | (-2607, -6668) / (-2227, -6668) | 90 cm from each door edge, 55 cm off the wall |
| Marker_A1_hut_enter | TutorialMarkerActor | Step `A1_hut_enter`, ring 0.9 m | (-2417, -6893) | 1.2 m inside the door |

Cover there (in-game check, `CoverQueryLibrary.find_cover_slot_at`): **left** slot (-2607, -6669) high (wall top 380 = the roof),
door edge 89 cm, the hut's NW corner 164 cm; **right** slot (-2227, -6678) high, door edge 91 cm. Both reachable from the
approach (10 m). The roof's eaves over the north wall broke every cover click under them (the cover trace's ground search
started inside the roof slab): fixed in C++ (`CoverTraceRules::FindGround`, rev 5c; `CoverHugSmoke` eaves check).

**Not placed on purpose:** enemies (placed enemies turn the level into an ambush level, beat sheet §1.3 - Phase 2 adds
`EncounterGroup`), quest objects (canister / generator / console `AInteractableActor`s - A7 is designed only; a GateTerminal would
start the existing quest chain), `EnemySpawnPoint`s (the wave lanes `NORTH_ICE` / `EAST_GULLY` / `WEST_SHORE` need the final
arena), the `CombatStart` / `SusaninSpawn` target points.

## 3. Graybox to replace with art

Everything in the folders `AutoLayout_Demo01/Camp`, `Approach` (ridge, fence segments, gap rail), `CoverField`, `Traps` (the two
rocks), `Overlook`, `Patrols` (containers), `Compound` (walls, base, roof, platform, ramp, generator shed) and `Beacons` (poles) is
engine cubes / cylinders with `WorldGridMaterial` / `BasicShapeMaterial` (engine content only, no dependency on the untracked
`Content/LevelPrototyping`). Replace the meshes freely; keep: the fence line closed (or the standing path walks round), the
ridge's `Vault` tag and 0.8 m height, low cover 0.9-1.1 m / high 2.2-2.5 m, the 1.6 m base door, the platform >= 1.5 m high.
The gameplay actors (volumes, markers, bridge, crawl gap, zones, mines, wire, routes, crates, barrels, barricades) stay.

## 4. Verification (2026-10-13, all on the copy)

- **Build** (`Saved/Logs/Demo01Layout.txt`): 94 placed, saved; a second run removes the 94 tagged actors and places them again
  (idempotent, same result). The user's map SHA256 is unchanged after every run. **Rev 5c (in the agents' worktree):** the 94 old
  tagged actors removed, 101 placed, saved; the user's map unchanged (worktree `7d31703f...`, main folder `3622cd28...`).
- **In game** (`-game -nullrhi`, mode `verify`, `Saved/Logs/Demo01LayoutVerify.txt`): **RESULT: PASS**.
  - Every volume id used by `demo_01_steps.json` exists exactly once (VOL_A0_BEACON1, VOL_A1_BEACON2, VOL_A1_BEACON3, VOL_A1_HUT,
    VOL_A2_CAMP, VOL_A3_FENCE, VOL_A3_FIELD, VOL_A3_RIDGE, VOL_A3_RIDGE_FAR, VOL_A3_WIND, VOL_A4_OVERLOOK); beats A1-A4 each have a
    starting volume (VOL_A0_BEACON1, VOL_A2_CAMP, VOL_A3_APPROACH, VOL_A4_OVERLOOK).
  - Nav paths with the standing operatives' filter (`UNavFilter_NoCrawl`), leg by leg: PlayerStart -> beacon 1 (20 m) -> VOL_A1_BEACON2
    (104 m) -> bridge far end (12 m) -> VOL_A1_HUT (96 m) -> VOL_A1_BEACON3 (55 m) -> VOL_A2_CAMP (34 m) -> VOL_A3_APPROACH (57 m) ->
    WIND (18 m) -> RIDGE (16 m) -> RIDGE_FAR (9 m) -> FENCE (14 m); beyond the fence -> FIELD (6 m) -> OVERLOOK (54 m) -> VOL_A5_BASE
    (75 m) -> VOL_A6_HIGH (29 m, up the ramp). All complete (not partial).
  - The crevasse: the bridge leg crosses the crack line (X -6423, found by ground traces) only at Y 1804 (the span); path requests
    from 60 m north / south of the bridge on the west bank to the east bank are valid only by going over the span (124 / 126 m).
  - The low fence: the standing path through it is partial (stops at the fence, 2 m); the prone (default filter) path goes under
    through the gap.
  - Patrol loops: 61 m / 39 m, every leg walkable and unobstructed (path / straight <= 1.01).
  - 35 tagged gameplay actors stand on the ground (within 40 cm of a downward trace) (rev 5c: 41).
  - **Rev 5c hut entry** (all ok): every id of the steps incl. `in_volume` (`VOL_A1_DOOR_L/_R`, `VOL_A1_HUT_APPROACH`) exists once;
    bridge far end -> `VOL_A1_HUT_APPROACH` 82 m -> `VOL_A1_HUT` (through the door) 14 m; `VOL_A1_HUT` Fire Once off; neither
    door volume overlaps the interior volume; the wall at each stack spot gives a HIGH cover slot inside its door volume, outside
    `VOL_A1_HUT` (+40 cm), with the door edge <= 2 m; both spots reachable from the approach.
- **With the demo level JSON** (`-LevelJson=L_Demo01_A_FrozenLake.json`): demo mode + tutorial (`demo_01_steps.json`) load and the
  same checks pass. Note: the copy has another map name, so WITHOUT that switch it gets the default `level_01_outpost.json` (no
  tutorial) - see §5.
- **Screenshots** (mode `shots`, `-game -RenderOffscreen`, a top-down in-game camera, HighResShot 3840 x 2160; labels drawn on
  by `annotate` from the actors' positions): `Saved/Screenshots/demo01_autolayout_top.png` and the three close-ups. **Rev 5c**
  (worktree `Saved/Screenshots/`): re-shot top + close-ups, plus `demo01_autolayout_hut.png` (top-down, the hut roof hidden for the
  shot) and `demo01_autolayout_hut_angle.png` (perspective from the approach, labels projected with the camera).

## 5. Review items (moved / ambiguous / for the user)

0. **Rev 5c, the hut door:** (a) the graybox **`HutDoor_Jamb_L`** (2.54 m, folder `AutoLayout_Demo01/HutEntry`) narrows the 4 m doorway
   to 2 m so the left stacker has a wall - replace it with art or widen your hut's door wall; (b) the hut's camera zone
   `Trigger_BoatsCabin` reaches 33 cm past the north wall: the stack slots stand 12-21 cm outside it (a stacker made leader keeps the
   outside camera) - if you enlarge the zone northwards the fixed interior camera would switch on at the door; (c) VOL_A1_HUT must stay
   **Fire Once off** if you re-place it by hand (a stacker walking in first must not use it up).
1. **The hut is 96 m from the bridge** (beat sheet: "close to the far side"). Kept where the user put it; move the hut or accept the walk.
2. **The user's crevasse is 1.2 m wide** (beat sheet: 2 m) and the plank is 1.23 m wide (navmesh strip ~0.5 m). It works (verified);
   widen the plank to ~2 m if operatives queue awkwardly.
3. ~~**The user's hut crate holds Heat Packs 1**~~ - rev 5d: the user set it to 3 in his copy (2026-10-14); the additive edit `hut_crate_heat_packs` (section 8) keeps it at 3.
4. **`VOL_A0_BEACON1` carries its id in Step Id**, Volume Id empty: it works (Volume Id falls back to Step Id) but the volume also
   tries to start a step named `VOL_A0_BEACON1` (harmless no-op). Cleaner: move the id to Volume Id, clear Step Id.
5. **The low fence closes the whole map width** (so the crawl lesson is forced). The squad can only return west by crawling; open a
   gate in the fence if a way back is wanted.
6. **Everything east of the hut is new graybox** (camp, approach, compound) laid out along +X from the hut: the camp at hut + 102 m,
   the approach at + 132 m, the overlook at + 255 m, the compound at + 305 m. All of it is "moved, review": positions are a first pass
   for the user to drag.
7. Beats A5 / A6 do not exist in `demo_01_steps.json` yet (Phase 2): their volumes do nothing until then (an unknown beat is ignored).
8. ~~Unrelated to the map, seen in the log: `Tutorial: step A0_move: after_step names an unknown step "None"`~~ - gone (rev 5b
   merged; rev 5c `A0_move` follows `A0_cam_zoom` only).

## 6. How to rerun on the user's latest map

> **Rev 5d: do NOT run `build` (or `CODEX_LAYOUT_FRESH`) on the review copy any more - it re-lays every `AutoLayout_Demo01` actor
> and would undo the user's hand edits. Use the ADDITIVE mode of section 8; steps 3-4 below (verify, shots) stay valid.**

The script never touches the user's map. To redo the layout from the user's latest version (e.g. after he edited the original):

```powershell
$p  = "C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics"
$ue = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64"
# 1. Back up the user's map (copy + SHA256) as usual.
# (Rev 5c ran these from the agents' worktree with its own binaries: $p = "...\CodexTactics-agents"; no CODEX_LAYOUT_FRESH.)
# 2. Rebuild the copy from the user's map (FRESH=1 deletes the old copy first; without it the existing copy is re-laid in place,
#    keeping hand edits on untagged actors of the copy).
$env:CODEX_LAYOUT_FRESH = "1"; $env:CODEX_LAYOUT_MODE = "build"
& "$ue\UnrealEditor-Cmd.exe" "$p\CodexTactics.uproject" -run=pythonscript "-script=$p\Scripts\Editor\build_demo01_lake_layout.py" -unattended -nullrhi -nosplash
Get-Content "$p\Saved\Logs\Demo01Layout.txt"
Remove-Item Env:\CODEX_LAYOUT_FRESH
# 3. Verify in game (the py console command needs a path without spaces).
Copy-Item "$p\Scripts\Editor\build_demo01_lake_layout.py" "$env:TEMP\demo01_layout.py" -Force
$env:CODEX_LAYOUT_MODE = "verify"
Start-Process "$ue\UnrealEditor-Cmd.exe" -Wait -ArgumentList "`"$p\CodexTactics.uproject`" /Game/Maps/Dev/L_Demo01_A_FrozenLake_AutoLayout -game -nullrhi -nosplash -nosound -unattended -LevelJson=L_Demo01_A_FrozenLake.json `"-ExecCmds=py $env:TEMP\demo01_layout.py`""
Get-Content "$p\Saved\Logs\Demo01LayoutVerify.txt"
# 4. Screenshots + labels.
$env:CODEX_LAYOUT_MODE = "shots"
Start-Process "$ue\UnrealEditor.exe" -Wait -ArgumentList "`"$p\CodexTactics.uproject`" /Game/Maps/Dev/L_Demo01_A_FrozenLake_AutoLayout -game -RenderOffscreen -ResX=1920 -ResY=1080 -nosplash -nosound -unattended `"-ExecCmds=py $env:TEMP\demo01_layout.py`""
python "$p\Scripts\Editor\build_demo01_lake_layout.py" annotate
```

To lay the actors straight onto another map (e.g. the user decides to run it on his own map after a backup):
`$env:CODEX_LAYOUT_TARGET = "/Game/Maps/L_Demo01_A_FrozenLake"` before step 2 (without `CODEX_LAYOUT_FRESH`). Only actors tagged
`AutoLayout_Demo01` are ever deleted; the user's own actors stay; ids the user already placed are kept and not duplicated.
`CODEX_LAYOUT_SOURCE` picks the map a fresh copy is made from.

## 7. How to swap the copy in

The game picks the level JSON by map name (`Content/Data/LevelJson/<MapName>.json`), so the copy runs the tutorial only under the
original name (or with `-LevelJson=L_Demo01_A_FrozenLake.json` in a standalone game; PIE has no switch).

1. Close the editor's tabs on both maps; back up `Content/Maps/L_Demo01_A_FrozenLake.umap` (copy + SHA256).
2. Either **(a) replace**: Content Browser -> delete `L_Demo01_A_FrozenLake` (or move it to `Maps/Dev/Old`), then rename
   `Maps/Dev/L_Demo01_A_FrozenLake_AutoLayout` to `L_Demo01_A_FrozenLake` and move it to `Maps/`; fix up redirectors
   (right-click the folder -> Fix Up Redirectors). Or **(b) merge onto the user's latest map**: run the script with
   `CODEX_LAYOUT_TARGET=/Game/Maps/L_Demo01_A_FrozenLake` (§6) - this keeps every hand edit the user made in the meantime and is the
   safer way if he kept working on the original.
3. Open the map, PIE: the log prints `Demo mode: ... tutorial 1 (demo_01_steps.json)`, `Tutorial volume ... entered by ...` lines,
   `Bridge ...` / `CrawlGap ...` lines at the new actors.
4. Commit the map + the script + this doc after the user's review (orchestrator decision).

## 8. Rev 5d: the ADDITIVE section (2026-10-14) - the copy is the user's now

The user edits the review copy by hand, so from rev 5d on the script **never resets, moves or deletes his actors** (including the
`AutoLayout_Demo01` ones he may have moved). Mode **`additive`** loads the EXISTING copy (never a fresh duplicate), deletes and
re-places **only** actors tagged **`AutoLayout_Demo01_5d`** (Outliner folder `AutoLayout_Demo01_5d/...`), applies the explicit edits
below, saves only when something changed, and writes `Saved/Logs/Demo01Layout5d.txt` (incl. a proof line "pre-existing actors moved /
deleted: 0 []") and dumps `Saved/LayoutDumps/..._5d_before_* / _5d_after_*.json`. The old `build` mode must not be run on the copy any more.

**Edits of pre-existing actors (the complete list, `EDITS_5D` in the script):**

| Edit | Actor | Property | Value | Why |
|---|---|---|---|---|
| `hut_crate_heat_packs` | the user's `LootCrateActor` inside the hut's Shelter zone (`Trigger_BoatsCabin`) | `Contents.HeatPacks` only | 3 | rev 5d heat pack sharing (3 starters - 1-2 used + 3 >= 3); the user's 2026-10-14 copy already had 3 -> unchanged |

**What the additive section places (11 actors, tag `AutoLayout_Demo01_5d`, folder `AutoLayout_Demo01_5d/...`):**

| Actor | Class | Settings | Position | Why |
|---|---|---|---|---|
| Ruin_Wall_S | StaticMeshActor | back wall, whole, 6 m x 2.6 m | Y -6950, X 7000..7600 | graybox ruin (review / replace with art) |
| Ruin_Wall_W | StaticMeshActor | 3 m standing (its north end fallen), 2.6 m | X 7000 | |
| Ruin_Wall_E_High / Ruin_Wall_E_Rubble | StaticMeshActor | 2 m broken to 2.0 m, then 2 m of 0.6 m rubble (low cover), then open | X 7600 | |
| Ruin_Wall_N_W / Ruin_Wall_N_E | StaticMeshActor | stubs 1.2 m (1.6 m high) / 1.2 m (2.2 m high) either side of the **3.6 m gap** (X 7120..7480) | Y -6450 | the wide gap to push through, facing the camp |
| Ruin_Roof_Partial | StaticMeshActor | 3.9 x 3.2 m over the south-west third at 2.6 m, no navmesh effect | | "half the roof is gone" |
| VOL_A2_RUIN | TutorialTriggerVolume | Any Operative, Fire Once off, 4.8 x 3.8 m | (7300, -6700) | `in_volume` of `A2_push` / `A2_light` / `A2_obj_warm` |
| Ruin_Interior_Shelter | CameraZoneVolume | Shelter (cold x0.5), camera switch off | (7300, -6700) | out of the wind; a campfire here counts as indoors (alert x0.4) |
| Marker_A2_push | TutorialMarkerActor | Step `A2_push`, ring 1.1 m | (7300, -6740) | where the barrel goes |
| Ruin_Barrel | BarrelActor | unlit | (7300, -5800) | 6.5 m in front of the gap, straight push path; the user's camp barrels are untouched |

Placed relative to the (pre-existing) `VOL_A2_CAMP`: ruin centre = its centre + (14.8, -20.1) m - if the user moves the camp volume,
a rerun moves only the ruin with it.

**Verification (2026-10-14, `Saved/Logs/Demo01LayoutVerify.txt` RESULT: PASS):** everything of section 4 still passes on the user's
copy, plus `VOL_A2_RUIN` once, Fire Once off, the barrel outside; `VOL_A2_CAMP` -> inside the ruin 25 m; 4 / 4 interior spots
reachable (room for three operatives and the barrel); push path barrel -> interior 8.0 m (straight 8.0 m) crossing the wall line at
X 7300 inside the gap; then a REAL push in game (`URelocationSubsystem::ExecuteRelocate`, an operative behind the barrel): the barrel
ends inside the ruin after 14 s, the 4 interior spots still reachable. Additive runs: first 11 placed, a rerun removed / re-placed
the same 11, "pre-existing actors moved / deleted: 0" both times; the user's map `L_Demo01_A_FrozenLake.umap` unchanged (`3622cd28...`).
Screenshots (`Saved/Screenshots/`): `demo01_autolayout_top.png`, `_crossing`, `_approach`, `_compound`, `_hut`, `_hut_angle`,
**`_ruin`** (top-down, the partial roof hidden) and **`_ruin_angle`** (perspective from the barrel's side).

Besides these, a save drops the generated `RecastNavMesh-*` actors (navigation data, not user content; HANDOFF section 2: a
commandlet-saved navmesh stays empty at runtime, the map rebuilds it at load).

**Rerun on the user's latest copy** (e.g. he edited it again in the main checkout; run with the editor of THAT checkout closed):

```powershell
$p  = "C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics"   # or ...\CodexTactics-agents
$ue = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64"
# 0. Back up the copy first (copy + SHA256):
$bk = "$p\Saved\Backups\autolayout_$(Get-Date -Format yyyyMMdd_HHmm)"; New-Item -ItemType Directory -Force $bk | Out-Null
Copy-Item "$p\Content\Maps\Dev\L_Demo01_A_FrozenLake_AutoLayout.umap" $bk; Get-FileHash "$bk\*.umap"
# 1. The additive section (only AutoLayout_Demo01_5d actors + EDITS_5D):
$env:CODEX_LAYOUT_MODE = "additive"
& "$ue\UnrealEditor-Cmd.exe" "$p\CodexTactics.uproject" -run=pythonscript "-script=$p\Scripts\Editor\build_demo01_lake_layout.py" -unattended -nullrhi -nosplash
Get-Content "$p\Saved\Logs\Demo01Layout5d.txt"
# 2. Verify in game (section 6 step 3: the same command with CODEX_LAYOUT_MODE = "verify"), 3. shots + annotate (section 6 step 4).
```

## 9. Rev 5h: the A3 corner-peek mines (2026-10-15, additive section `5h`)

The game designer's play-test: the A3 card "peek around the corner" should be meaningful - hidden mines lie beyond that corner and the
peek reveals them (C++: the corner peek spots hidden traps in its 60 deg / 12 m sight cone, HANDOFF §5). Section **`5h`** (tag
**`AutoLayout_Demo01_5h`**, folder `AutoLayout_Demo01_5h/...`), placed relative to the first high wall of the cover field (the
`Cover_A3_*_High` nearest `VOL_A3_FIELD` = `Cover_A3_2_High`, wherever the user moved it); no pre-existing actor is edited.

| Actor | Class | Settings | Position (worktree map 2026-10-15) | Why |
|---|---|---|---|---|
| Marker_A3_peek | TutorialMarkerActor | Step `A3_peek`, ring 0.7 m | (19345, -3380) | the wall's OUTER (-Y, away from the lane) corner on the approach face: where to peek |
| PMC_Mine_A3_Peek_1 | ProximityMineActor | hidden level mine, damage 80, **Spot Event Tag `A3PeekMines`** | (20220, -3490) | wall centre + 8 m beyond, 2 m towards the outer end: 8.7 m from the wall face |
| PMC_Mine_A3_Peek_2 | ProximityMineActor | same | (20370, -3240) | 9.5 m beyond, 0.5 m back: 10.2 m from the face, in front of `Cover_A3_4_High` (the next bound) |

Rules the placement keeps (checked by `verify`): both mines further than 6 m (mine spot 4.5 m + the Medic-Sapper's 1.5 m) from the wall's
approach face / the cover slot and the lane (7.5 m+), within 12 m and 30 deg of the corner-aim direction round that corner (corner +
100 deg outward from along the wall) with a clear line of sight (8.7 m / 9 deg and 10.5 m / 4 deg); the cover slot at the marker is
HIGH and reachable from the field entrance (17 m).

**Verification (2026-10-15, worktree):** additive run `Saved/Logs/Demo01Layout5h.txt`: 0 removed, 3 placed, saved, "pre-existing actors
moved / deleted: 0" (the 2 generated RecastNavMesh actors dropped as always); backup before the run
`Saved/Backups/lake_before_5h_*/L_Demo01_A_FrozenLake.umap` (SHA256 `458f015a...`). In-game verify `Saved/Logs/Demo01LayoutVerify.txt`
**RESULT: PASS** incl. the new `peek:` lines; `PeekMinesSmoke` PASS (no spot from cover, both spotted by the peek).

**Rerun on the user's latest map** (e.g. he edited it in the main checkout; run with that checkout's editor closed): back the map up, then
```powershell
$env:CODEX_LAYOUT_MODE = "additive"; $env:CODEX_LAYOUT_SECTIONS = "5h"
& "$ue\UnrealEditor-Cmd.exe" "$p\CodexTactics.uproject" -run=pythonscript "-script=$p\Scripts\Editor\build_demo01_lake_layout.py" -unattended -nullrhi -nosplash
Get-Content "$p\Saved\Logs\Demo01Layout5h.txt"
```
(a rerun deletes and re-places only the three `AutoLayout_Demo01_5h` actors; `CODEX_LAYOUT_SECTIONS=5d` re-places the ruin, `5d,5h` both).

## 10. Rev 5i: the A3 lesson mines only in the green zone, found only by the peek (2026-10-15, additive section `5i`)

The game designer's play-test (a marked screenshot): an operative in cover at a LOW wall of the field spotted a mine before the peek
lesson (a crossed-out red disc) - the lesson order broke. **Which mine:** the rev 5 "shortcut" mines of the field, `PMC_Mine_A3_1`
(19720, -2540) / `PMC_Mine_A3_2` (20220, -2440) (`AutoLayout_Demo01`, the straight line through the middle of the field) - 6-7 m
from the cover slot of `Cover_A3_3_Low`, inside the Medic-Sapper's 6 m spot range (the two 5h peek mines lie 8.7 / 10.2 m from any
cover). **The green zone** = beyond the peek corner of the first high wall (`Cover_A3_2_High`), in front of the next high wall
(`Cover_A3_4_High`): the peek sight cone of that corner (12 m, 60 deg). Section **`5i`** (tag `AutoLayout_Demo01_5i` reserved;
**no new actors, only explicit edits**, `EDITS_5I`):

| Edit | Actor | Change |
|---|---|---|
| `field_mines_to_green_zone` | `PMC_Mine_A3_1` | (19720, -2540) -> **(19950, -3560)** (peek frame offset (-530, +270)); SpotEventTag `A3PeekMines`; **SpotRequiresStep `A3_peek`** |
| `field_mines_to_green_zone` | `PMC_Mine_A3_2` | (20220, -2440) -> **(20100, -3330)** (peek frame offset (-680, +40)); SpotEventTag `A3PeekMines`; SpotRequiresStep `A3_peek` |
| `peek_mines_require_step` | `PMC_Mine_A3_Peek_1` / `_2` (5h) | SpotRequiresStep `A3_peek` (the 5h builder now sets it too) |

The places are computed from the peek wall's frame (`FIELD_MINE_FRAME_OFFSETS`), so a rerun on a map where the user moved the wall
moves them with it. With the two 5h mines the four lesson mines form a small minefield 2-2.8 m apart (the trigger radius is 1.6 m)
just beyond that corner, near the corner-aim axis: the in-game lean-out muzzle stands ~40 cm nearer the wall than the script's
corner point, and a sight line steeper than ~0.24 towards the lane clips the wall's end (found by the smoke; the first two tries
were blocked). **C++ (every level):** `AInteractableActor::SpotRequiresStep` - until that tutorial step has started, the trap is not
spotted by proximity (only a corner peek reveals it); ignored with the tutorial off / skipped or on a level without that step
(`TrapSpotRules::AllowsProximitySpot`); the peek's line of sight now ignores every hidden trap (a mine never hides another one).

**Verification (2026-10-15, worktree):** additive run `Saved/Logs/Demo01Layout5i.txt` (backup before the first run:
`Saved/Backups/lake_before_5i_20261015_1612/L_Demo01_A_FrozenLake.umap`, SHA256 `ee0eb221...`); "pre-existing actors moved /
deleted: the two field mines (expected by the explicit edits)"; in-game verify `Saved/Logs/Demo01LayoutVerify.txt` **RESULT: PASS**
(four hidden lesson mines tagged A3PeekMines, all waiting for A3_peek, **no other mine in the cover field**, each > 6 m from the peek
wall's face / slot and inside the peek cone with a clear sight line); **PeekMinesSmoke PASS** (before the peek lesson an operative
is put 3 m from every lesson mine and in front of every `Cover_A3_*` face - 13 probe points: nothing spotted; then the corner peek
spots all four). Overhead still: **`Saved/Screenshots/a3_mines_green_zone.png`** (the green cone = the zone, red = the four mines,
grey crosses = where the field mines were).

**Rerun on the user's latest map** (back it up first; run with that checkout's editor closed):
```powershell
$env:CODEX_LAYOUT_MODE = "additive"; $env:CODEX_LAYOUT_SECTIONS = "5i"
& "$ue\UnrealEditor-Cmd.exe" "$p\CodexTactics.uproject" -run=pythonscript "-script=$p\Scripts\Editor\build_demo01_lake_layout.py" -unattended -nullrhi -nosplash
Get-Content "$p\Saved\Logs\Demo01Layout5i.txt"
```
(the C++ property `SpotRequiresStep` must be compiled into that checkout's editor first; `5h,5i` re-places the peek mines and then edits.)
