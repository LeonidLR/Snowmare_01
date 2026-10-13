# Demo 01 «Frozen Lake»: auto layout (2026-10-13)

The user asked: "build everything on the level yourself (beacons / volumes / markers / objects); I will adjust positions by
hand afterwards". This is that first pass, made by script on a **copy** of the user's map. Nothing is committed yet.

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
| Marker_A1_solo | TutorialMarkerActor | Step `A1_solo`, ring 1.2 m | (-6658, 1804) | near end of the bridge |
| BridgeCrossing_A1 | BridgeCrossingVolume | Capacity 1, span 6.9 x 2 x 2.5 m, local X along the span | (-6231, 1804, 124) | user plank `Floor3` + 1 m each end |
| WindZone_A1_FarBank | CameraZoneVolume | Blizzard x2.5, camera switch off, 12 x 12 m | (-5285, 1804) | far side of the bridge |
| VOL_A1_HUT | TutorialTriggerVolume | Any Operative, over the hut floor | (-2180, -7090) | user hut (`Trigger_BoatsCabin`) |
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

Counts: TutorialTriggerVolume 14, TutorialMarkerActor 2, CameraZoneVolume 4, BridgeCrossingVolume 1, CrawlGapActor 1,
ProximityMineActor 2, PMCTripwireActor 1, PatrolRouteActor 2, TargetPoint 4, BarrelActor 5, LootCrateActor 2, BarricadeActor 2,
CameraActor 1, NavMeshBoundsVolume 1, StaticMeshActor 52 (graybox).

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
  (idempotent, same result). The user's map SHA256 is unchanged after every run.
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
  - 35 tagged gameplay actors stand on the ground (within 40 cm of a downward trace).
- **With the demo level JSON** (`-LevelJson=L_Demo01_A_FrozenLake.json`): demo mode + tutorial (`demo_01_steps.json`) load and the
  same checks pass. Note: the copy has another map name, so WITHOUT that switch it gets the default `level_01_outpost.json` (no
  tutorial) - see §5.
- **Screenshots** (mode `shots`, `-game -RenderOffscreen`, a top-down in-game camera, HighResShot 3840 x 2160; labels drawn on
  by `annotate` from the actors' positions): `Saved/Screenshots/demo01_autolayout_top.png` and the three close-ups.

## 5. Review items (moved / ambiguous / for the user)

1. **The hut is 96 m from the bridge** (beat sheet: "close to the far side"). Kept where the user put it; move the hut or accept the walk.
2. **The user's crevasse is 1.2 m wide** (beat sheet: 2 m) and the plank is 1.23 m wide (navmesh strip ~0.5 m). It works (verified);
   widen the plank to ~2 m if operatives queue awkwardly.
3. **The user's hut crate holds Heat Packs 1** (beat sheet: 3, the resupply lesson). Not changed (user actor).
4. **`VOL_A0_BEACON1` carries its id in Step Id**, Volume Id empty: it works (Volume Id falls back to Step Id) but the volume also
   tries to start a step named `VOL_A0_BEACON1` (harmless no-op). Cleaner: move the id to Volume Id, clear Step Id.
5. **The low fence closes the whole map width** (so the crawl lesson is forced). The squad can only return west by crawling; open a
   gate in the fence if a way back is wanted.
6. **Everything east of the hut is new graybox** (camp, approach, compound) laid out along +X from the hut: the camp at hut + 102 m,
   the approach at + 132 m, the overlook at + 255 m, the compound at + 305 m. All of it is "moved, review": positions are a first pass
   for the user to drag.
7. Beats A5 / A6 do not exist in `demo_01_steps.json` yet (Phase 2): their volumes do nothing until then (an unknown beat is ignored).
8. Unrelated to the map, seen in the log: `Tutorial: step A0_move: after_step names an unknown step "None"` - `demo_01_steps.json`
   now gives `A0_move` an array of `after_step` steps that the C++ on main does not read yet (the other agent's tutorial change).

## 6. How to rerun on the user's latest map

The script never touches the user's map. To redo the layout from the user's latest version (e.g. after he edited the original):

```powershell
$p  = "C:\Users\Zephyrus15Duo\Documents\Unreal Projects\CodexTactics"
$ue = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64"
# 1. Back up the user's map (copy + SHA256) as usual.
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
