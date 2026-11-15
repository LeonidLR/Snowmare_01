# Mantling slice 1 - report (2026-11-15, branch `feat/mantling-slice1`)

Spec: [docs/design/MANTLING.md](../design/MANTLING.md) section 11, the designer's answers 11.6 (binding). One operative, Solo, end-to-end, on the
reworked `L_Gen_Depot_1`. Formation queue / overwatch is NOT in this slice.

## 1. What works

| Piece | Where | State |
|---|---|---|
| Pure rules: height class (nominal +- tolerance from `Content/Data/LevelGen/obstacle_heights.json`), top vs over by depth, wall above the 320 cm ceiling, eligibility by kind, headroom, run-up -> standing / running clip, male / female clips with round-robin, missing-clip fallback, warp limits, refusal reasons | `Source/CodexTactics/Public/Characters/MantleRules.h`, `Private/Characters/MantleRules.cpp`, data `Content/Data/Movement/mantle.json` | 7 unit tests `CodexTactics.Characters.Mantle.*` green |
| Runtime climb: probe -> reserve the landing -> snapshot -> `MOVE_Flying`, collision off, weapon slung -> the clip's root motion on `FullBodySlot`, warped by the engine **Motion Warping** plugin onto the planned landing -> land (one end-of-clip correction, root motion disabled first: no double displacement) -> restore exactly once -> crouch on a top / walk on | `Private/Characters/OperativeCharacterMantle.cpp` (+ fields on `AOperativeCharacter`, `UMotionWarpingComponent` default subobject `MotionWarping`) | MantleSmoke |
| Knockdown / heavy hit (>= `heavy_hit_damage` 35) mid-climb: dropped back to the base, state restored once; death / EndPlay release everything | `KnockdownComponent.cpp` (StartKnockdown), `OperativeCharacterHealth.cpp` (HandleDamaged / death), `OperativeCharacter.cpp` (EndPlay) | MantleSmoke case i |
| Trigger C, click: in Solo a plain click on an eligible ledge top orders the climb (walk to the nearest face, climb, crouch on top) | `Private/Core/CodexTacticsPlayerControllerOrders.cpp` (before the cover click, after Ctrl / Alt / Shift / ladders / objects; real-time dispatch only, not on the grid, not for a multi-selection) | MantleSmoke cases f / g |
| Trigger C, path: generated directed nav links (`UNavArea_Mantle` up / over, `UNavArea_MantleDown` hop down off low tops), open only for the operative the player commands in Solo; formation moves / follows / the autonomy use `*NoMantle` filters; enemies never (`UNavFilter_NoVault` closes both). Links onto tops with no way back (FDropLinkReach) are dropped | `Public/Interactables/MantleNavigation.h`, `Private/Interactables/MantleNavigation.cpp`, `OperativeCharacterMovement.cpp` (ChooseMoveFilter / OrderMoveTo / RequestMove) | MantleSmoke cases j / k |
| Coming down off a low top (crate / wreck / boiler): its down link, or - when the top has no navmesh - a move order hops him down (the vault's arc) towards the goal | `OperativeCharacterMantle.cpp` TryMantleStepDown / StartMantleHop | MantleSmoke case a |
| Barrels no longer vaulted (explosive) | `VaultRules.cpp` (vault area, limits and cost 3 unchanged) | `CodexTactics.Movement.Vault.LevelGenCovers`, DepotVaultSmoke |
| Depot obstacles on the height classes, map rebuilt + baked headless | `Scripts/Tools/apply_depot_height_classes.py`, `Content/Data/LevelGen/accepted/gen_ref_01_02_depot_1.json`, `Content/Maps/Generated/L_Gen_Depot_1.umap` | see 4 |

## 2. How the designer triggers it (PIE / game on `L_Gen_Depot_1`)

- Solo mode (the level starts in Solo). Select one operative (1..4 / portrait). **Click on the top of a crate, a wreck, a boiler or the cut's
  parapet**: he walks to the face nearest to him and climbs; on top he crouches. A fence is not a top: **click on the ground behind a 135 cm
  fence** - the path goes over it. Any click on the ground beyond an obstacle whose way round is more than ~6 m longer (`links.entering_cost_cm`
  600) takes the mantle link by itself.
- Run clips: only when he is already running (double-click a sprint order, then click the top / the far side) and the face is at the run
  clip's run-up distance (RunLowClimb ~1.1 m, RunHighClimb ~3.8 m, RunJumpClimbNEW ~4.4 m); closer he stops and climbs standing.
- Down: any move order from a top (a hop down on low tops; the drop links off the 230 cm parapet).
- Not climbable: barrels (explosive), the 330 cm fences, building walls / 300 cm roofs (no marked edges yet), silos, chimneys.
- Formation mode: today's behaviour - nobody climbs; the formation paths never take a mantle link.
- Console: `Codex.Mantle.Links 0` switches the path links off (the click still works); `CodexTactics.MantleLinks` regenerates them.

Climbable spots on the Depot (after the rework): 50 crates at 85 cm and 50 at 140 cm (alternating by id), 69 wrecks and 16 boilers at 140 cm,
the six cut parapet walls at 230 cm (both banks, from the ground), 11 lowered fences at 135 cm (F_compound_N_1, F_compound_E_0,
F_boilers_S_0 / S_1 / E_0, F_lot_N_0 / S_0 / S_1 / W_0, F_junkyard_S_2, F_east_N_0 - interior fences >= 12 m from every ambush point, none on the
map's perimeter). Sandbags and jersey barriers 95 cm stay the vault.

## 3. Root motion: Motion Warping, not scaling

Chosen: **the engine Motion Warping plugin** (`MotionWarping` in `CodexTactics.uproject` and `CodexTactics.Build.cs`). One
`URootMotionModifier_SkewWarp` window over the whole clip (added at runtime with `AddRootMotionModifierSkewWarp`, Z warped too) targets the
planned landing (feet). Why: the retargeted male clips rise ~8 % more than the source (x1.084), the female ~4 % (x1.039), and the real obstacles
vary inside their +-10 cm bands and the operative never stands exactly at the clip's start distance - one warp absorbs height, forward
distance and facing per climb; scaling the root translation would fix only the constant body ratio, not the per-obstacle error. The
warp is limited (`max_vertical_warp` 0.25, `max_forward_warp` 0.35 for the run window). The auto blend-out is off, so the warp window ends
exactly at the landing; at the end root motion is disabled on the instance before the clip blends out and the capsule is put on the landing once
(measured error: see 5). Collision is off during the climb (as the vault) - the landing footprint and headroom are checked before it starts.

## 4. Depot rework + route gates

- `python Scripts/Tools/apply_depot_height_classes.py` (idempotent; `--check`): boilers 160->140, crates 110->85/140 (50/50), cut_wall
  220->230, fences 250->135 (11) / 330 (23), jersey 90->95, sandbag 100->95, wrecks 150->140; barrels 90 unchanged.
- `Scripts/levelgen_build_map.ps1 -Layout gen_ref_01_02_depot_1 -Target /Game/Maps/Generated/L_Gen_Depot_1`: 545 actors, navmesh baked
  (Default 440 tiles). The builder rewrites `Content/Data/LevelJson/L_Gen_Depot_1.json`; it was restored byte-for-byte afterwards
  (sha256 c2fcbb8c..., the designer's wave file is untouched).
- Golden data that pinned the old Depot: `Scripts/Editor/tests/test_build_levelgen_buildings.py` (plan hash updated to the new layout), the
  props placement goldens now read a frozen copy of the pre-rework layout (`Content/Data/LevelGen/props/fixtures/golden_depot.layout.json`) -
  they test the placement, not the live map (C++ `PropsPlacementRulesTest.cpp` / `PropsPlacementC4Test.cpp`, TS `placement.test.ts` via
  `golden_cases.json`). Training Ground `npm test`: 359 / 359.

## 5. Gates

| Gate | Result |
|---|---|
| `Scripts/build.ps1` (CodexTacticsEditor Development) | Succeeded (only the pre-existing unity-clash warnings `Defaults` / `MakeSlot`, not from this branch) |
| `test.ps1 -Filter CodexTactics.Characters.Mantle` | 7 tests, 0 failed |
| `test.ps1 -Filter CodexTactics.Characters.Vault+CodexTactics.Movement.Vault` | 2 tests, 0 failed (`Movement.Vault.LevelGenCovers` now expects barrels NOT vaulted) |
| `test.ps1 -Filter ...Mantle+...Vault+CodexTactics.LevelGen.Props` | 70 tests, 0 failed (props goldens on the frozen layout) |
| `test.ps1 -Smart -Base a3b9379e` (the branch's whole diff; the map chose all `CodexTactics`) | 1389 tests, 0 failed |
| `smoke_parallel.ps1` MantleSmoke, VaultSmoke, DepotVaultSmoke, BarricadeVaultBackSmoke, ClickRulesSmoke, FallDropSmoke, DropLinkReachSmoke, LevelGenRouteSmoke, LevelGenLaddersSmoke, SoloStartSmoke, FormationLabSmoke, SquadControlSmoke | final run after the last code change: 12 run, 0 failed, 0 re-run passes (wall 256 s): MantleSmoke 51 s, Vault 49, DepotVault 49, BarricadeVaultBack 49, ClickRules 48, DropLinkReach 49, FallDrop 58, LevelGenRoute 98, SoloStart 57, LevelGenLadders 73, SquadControl 44, FormationLab 147 (an earlier full run before the link-floor fix: 12 / 12 too) |
| Training Ground `npm test` | 359 / 359 |
| `Scripts/Editor/tests/test_build_levelgen_buildings.py` | Depot plan hash updated; `test_train_1_openings...` fails on main too (sill 95 vs 90 since 58a7e34e, not this branch) |
| `typesafe_triage.py --audit-diff --agent claude` | PASS, no boundary breach (risk 2.2 / 4 on the full runtime diff before it was committed; 0.0 on the final docs diff) |

MantleSmoke (L_Gen_Depot_1): 910 links generated (507 up / over: high_mantle 400, high_fence_over 59, tall_ledge 45, low_mantle 3; 403 down; 1084 samples, 98 ms, 2 dead ends dropped);
85 cm crate Low_Climb on top crouched (landing 2 cm off the plan, restored once, flown with collision off), a move order hops him down;
140 cm wreck HighClimb; 230 cm parapet TallLedge_ClimbUpNEW / Standing_JumpClimbUpNEW (round-robin); 135 cm fence HighFenceVault landing
234 cm past the face on the ground; sandbag refused, the vault takes it; sprint + click 193 cm off the face -> stopped, standing HighClimb;
sprint from 7.5 m -> RunHighClimb started 400 cm off (warp fwd x1.04); 330 cm fence refused (wall); a beam 140 cm over a crate's landing
refused (no headroom); knockdown 1.8 s into a tall-ledge climb -> back at the base (0 cm), restored exactly once, he gets up; a Solo order
to the far side of a 135 cm fence goes over it by itself (HighFenceVault) and arrives (51 cm off, 3.1 s; an earlier run did the same over the cut parapet); the same order in formation: nobody mantles. The female case is skipped on
the Depot (its default squad has no female operative).

## 6. Not done / open

- **The canal geometry contradicts "climb out of the canal"**: the cut floor is 300 cm below ground and the `cut_wall` parapets (now 230) stand
  ON the ground at the trench edge - from the canal floor the face is 530 cm (a wall). The 230 climb works from the ground onto the parapet
  (from there the drop links lead into the canal or back). A real climb-out needs a designer decision: e.g. 300 cm canal faces at parapet gaps
  as `very_tall` marked edges, or a shallower cut.
- very_tall 310 (one-storey roofs at 1-2 marked edges per building): the rules and clips are in (`marked_edge_only`), the Depot has no
  marked edges yet (slice 2: mark pipes / annexes / crates next to the 300 cm roofs in the layout, `CodexMantleEdge`).
- Formation queue / overwatch / squad reservations (slice 2, after the designer's test).
- Female clips: in the table, chosen by the body's skeleton (`GetMantleBody`), a montage on a mismatched skeleton refuses the climb. MantleSmoke has a female case but the Depot squad has no female operative, so it was skipped; contacts not checked frame by frame.
- No hover cursor for a climbable top; no HUD marker for a mantle on the path.
- The four NEEDS_REVIEW clips stay out (designer Q4).
- Turn-based: no mantle (as the vault). The tactical pause: a top click is not planned (only the real-time dispatch climbs).
- Only 3 low_mantle path links: the 85 cm crates (120 x 120) carry no navmesh on top for the 35 cm agent - they are climbed by the click, not by paths.
- Navmesh on small tops: 378 link samples found no navmesh on the top (85 cm crates 120 x 120 are too small for the 35 cm agent) - those tops
  are climbed by the click (no path link); coming down is the hop on the next move order.

## 7. ABP

No ABP asset was changed. The climb plays as a dynamic montage on `FullBodySlot` (the ABP already has it - vault, knockdown and ladders use it).
`UOperativeAnimInstance::bIsMantling` (new, BlueprintReadOnly) and `bIsVaulting` (true while mantling too) are there for the designer's polish;
every native gate on `bIsVaulting` (aim offset, left-hand IK, the weapon layers, sniper / stealth / injured / interaction / warm clips) holds
for the climb. The weapon mesh is hidden for the climb (the clips are unarmed) and shown again on landing.
