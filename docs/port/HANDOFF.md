# HANDOFF — CodexTactics (UE 5.8 port of Operation: Cold Silence)

**Purpose.** The short entry point for any agent (Claude, Gemini, ...): how to work in this repo, the traps, the current state and a map to the
per-system docs. Detail lives in `docs/systems/*.md`, the history in `docs/port/CHANGELOG.md`. (Until 2026-10-19 this file held all of it, 668 KB;
it was split verbatim, nothing was dropped - old `HANDOFF §N` references resolve through the table in section 6 and in `docs/systems/README.md`.)

Last update: 2026-10-19 (split into an index + per-system docs + CHANGELOG; the per-system content is current to main `2a6561c`).

**Project status report (non-technical, for planning): `docs/port/PROJECT_STATUS_2026-10-14.md`.**

**State (2026-10-19).** The Unreal port is the reference version (the Godot repo is a frozen archive). Playable: the exploration -> preparation -> wave combat
(real time / tactical pause / turn-based) loop, squad cover and peek, stealth lure and hound scent, cold and tactical heat, loot / traps / turrets / APC,
CommonUI frontend with save / load, minimap, and the Demo 01 tutorial on `/Game/Maps/L_Demo01_A_FrozenLake` (one lake map since 2026-10-15; rev 5j).
Last recorded test run (CHANGELOG head, quick fire): 529 unit tests green; all smokes are registered in `Scripts/test_map.json`.
Latest work (2026-10-19): quick fire (Ctrl + click on a point), peek sight, symmetric sight while exploring, occluded vision cones, load = reopen the level,
unity-clash finder in `build.ps1`. 2026-10-20 (tutorial, `agents/work`): one current navigation target (edge arrow + minimap highlight, guide
beacon; places behind never call back - the lake bridge marker bug), the A4 ambush by the Medic-Sapper alone, the A3 fence without the
one-at-a-time queue, the pacing pass (breather 0.8 s, `Tutorial.Flow.Pacing`) - `docs/systems/tutorial-engine.md` sections 17-18, 554 unit tests.
2026-10-20 (`agents/pmcfighter`): the Frozen PMC Fighter (`/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/BP_Enemy_FrozenPMC_Fighter`, a Frostbitten variant on the user's Cyberpunk_Explorer_A body with a retargeted copy of the Frostbitten ABP; the pack mesh has a stray FBX root bone - see `docs/systems/animation-setup.md` "Frozen PMC Fighter").
2026-10-20 (`agents/zombie2`): the Military Zombie (`/Game/Characters/Enemies/MilitaryZombie/BP_Enemy_MilitaryZombie`, a Frostbitten variant; `ABP_Enemy_MilitaryZombie` = a child of the Frostbitten ABP on the pack's own Manny-copy skeleton with its 20 clips; rage roar on alert, transformation entrance on wave spawns, devour idle, 3 bodies x 3 texture sets - see `docs/systems/animation-setup.md` "Military Zombie").
2026-10-20 (`agents/coverswitch`): the wall-edge cover switch - in cover at a wall edge, a double-click on another cover within 8 m dashes there and arrives through the M4 pack's `std_jog_fwd[_L|_R]_to_cvr_*` run-in clip (`docs/systems/cover-and-peek.md` "Wall-edge cover switch").
2026-10-20 (`agents/work`): corner-to-corner switch (the nearest corner of the other object, mirrored pose) and the wall-following round a cover object (click along the same object's outline -> round its corners in cover) - `docs/systems/cover-and-peek.md` "Corner to corner" / "Wall-following round a cover object".
2026-10-21 (`agents/work`): the Demo 01 A4 ambush in two parts, each bound to ITS zombie on the user's lake layout (steps-file `"enemies"` bindings + `"enemy"` event filters, event `EnemyKilled`, guide spots `@lure_spot` / `@enemy`; smoke `TutorialAmbushLakeSmoke`) - `docs/systems/tutorial-engine.md` section 19, `docs/systems/demo01-lake.md` block 2026-10-21.
2026-10-27 (`env/start-flicker`): the START FLICKER (user report: "flashes as if there's a thunderstorm" in the first ~1 s, PIE) was the presenter's 1 s eye adaptation window at 1000 f-stops / s (section 16) chasing its own read-back pre-exposure - the lake jumped 17 <-> 237 every 2-4 frames (largest step 169, PIE at 60 fps 97); now no fast window (`ExposureSnapSeconds` 0: the 3 camera-cut frames settle the exposure, then the authored speeds; an optional window is capped stable), lightning gated on the temperate climate at the source; per-frame capture `CodexTactics.StartFlickerShots` / `PIEFlickerShots`, trace category `LogCodexEnvTrace`, strips `Scripts/Tools/capture_start_flicker.ps1` + `flicker_strip.py`, smoke `StartFlickerSmoke` (rendered) - `docs/systems/environment-weather.md` section 19.
2026-10-26 (`env/rain-sunny`): temperate climate profile (section 17) + the RAIN VISUALS of the narrative levels (section 18): `UWeatherRainFxComponent` (created by the presenter only on a temperate level; NS_Weather_Rain / NS_Weather_PuddleSplashes, screen drops, storm lightning off under smokes), `DA_RainFX` (the user's tuning), wet overlays `M_WetOverlay` / `M_WetGroundOverlay` (the overlay component's wet mode), `MF_RainWetSurface` in `M_Landscape_Master` (neutral at 0: arctic A/B identical; PS 355 -> 456, samplers 3 -> 3); smoke `RainSmoke` (rendered); captures `EnvironmentShots rain / rainterrain / rainperf` - `docs/systems/environment-weather.md` sections 17-18.
2026-10-26 (`camera/stay-after-pan`): a panned-away camera stays where the player left it (no follow while a character walks) until a number key 1-4 / a portrait click re-centres it (`ATacticalCameraPawn::ReattachToFollowTarget`, `bDetached`; not in turn-based combat); smoke `CameraStaySmoke`; see camera-hud-minimap.md.
2026-10-21 (`agents/work`): the Demo 01 canister quest (A7) in the lake's last compound - blast door in the east wall, generator in the shed, the burnt BTR-82 (`AInteractableActor::VisualMesh`), the jerrycan at the far A4 container, `QuestStep.*` / generator / gate events, stat `quest_progress`, beats A7 / A8, the horde's spawn points, smoke `LakeQuestSmoke` - `docs/systems/demo01-lake.md` block 2026-10-21 b, `docs/demo/DEMO01_AUTOLAYOUT.md` section 12.
2026-10-21 (`agents/work`): the minimap / full map is top-down (straight down at the start camera's yaw; `Codex.Minimap.TopDown 0` = the old iso view), roofs over the lake interiors left out (traces now see WorldDynamic) - `docs/design/MINIMAP.md`.
2026-10-21 b (`agents/work`): A4 failure paths (spotted -> free tactical pause + posture card), the tactical approach cards, level JSON `"ambush_fight": "skirmish"` (placed-enemy fights end straight in exploration, no waves) and `"susanin_wave": 0` on the lake - `docs/systems/waves-horde-encounters.md` "Skirmishes", `docs/systems/demo01-lake.md` block 2026-10-21 b.
2026-10-22 (`agents/work`): the designer's Demo 01 play-test - the tactical pause lesson (two cards on the first pause, events `TacticalPauseStarted` / `TacticalPauseEnded`), a failed defuse retried by clicking the trap again, the posture lesson asks for DEFENSIVE, the tripwire card shows for the first PMC wire met (it was precredited), `"cancel_on": { "condition" }` + stat `all_dead.<binding>`: the A4 steps of a dead zombie close, `A7_traps` - `docs/systems/demo01-lake.md` block 2026-10-22, `docs/systems/tutorial-engine.md` section 20.
2026-10-22 b (`agents/work`): play-test part 2 - the squad stays where a fight ended (no prep-spot teleport, followers held), moved barricades vaulted both ways (box kept off the navmesh), the horde fly-in frames the wave's main cluster in the frustum, no heat fear through walls, the turn-based grid only on open level ground (`TurnArenaRules`, refusal line), operatives turn back to the enemy after a retreat (`ThreatFacingRules`) - CHANGELOG head, `docs/systems/combat-and-fire.md` "Turn-based arena".
2026-10-22 c (`agents/work`): the level ends through the fully opened blast gate (level JSON `"gate_exit"`, `ULevelExitSubsystem`: every living operative out -> mission complete + fade to black; the lake: auto), the last wave goes straight to post-combat (no "End combat" button), level-up points never interrupt (gold "+N" badge on the squad slot) - `docs/systems/demo01-lake.md` block 2026-10-22 c, `waves-horde-encounters.md` "Fight end".
2026-10-22 c (design only): ladders - procedural `ALadderActor` + nav-link driven climbing, rewritten from the user's course spec after a spec audit, with the measured clip contract (pitch 36.4 cm) and a 5-phase plan; user decisions: no ladders in turn-based, PMC / marksmen climb, free-standing ladders allowed - `docs/design/LADDERS.md` (4 open questions in its section 10).
2026-10-22 d (`agents/work`): the campaign `Content/Data/Campaign/campaign.json` "0.0 Frozen Dam" (the lake) -> "0.1 Frozen Factory" (`/Game/Maps/L_Demo01_B_FrozenFactory`, a copy of L_MovementTest with the lake's open blast door behind the start) - after the exit's black screen the stat-points screen (live portrait + character sheet) when points are left, then the next level with the carried squad (`UCampaignSubsystem`, `CampaignCarry`), start fade-in; the copy needs one editor re-save for its navmesh - `docs/systems/demo01-lake.md` block 2026-10-22 d.
2026-10-23 f (`ladder/work`): the queue at a ladder - one climber at a time, the others wait at the foot in the order they came (`ALadderActor` FIFO, `ULadderClimbComponent::TickQueue`), the formation follow leaves climbers / waiters alone; smoke `LadderQueueSmoke` (3 operatives).
2026-10-23 e (`ladder/work`): turning back mid-climb - a new order behind him turns the climber round at the next rung (`ULadderClimbComponent::HandleNewOrder`, `LadderRules::NextRungBoundary` / `PreferTopEnd`).
2026-10-23 d (`ladder/work`): fix - the climb out on top no longer sinks the legs into the roof (the clip steps onto a top 5 rungs up and lifts itself 73 cm at the end: ClimbOutRungs 5, the lift removed from its root track, an edge push for the knees) - `docs/design/LADDERS.md` note "Fixed 2026-10-23 b".
2026-10-23 c (`ladder/work`): ladders x camera / roof cutaway / minimap (LADDERS.md 7.3) - spring-smoothed camera height on a climber, the cutaway / minimap / camera zones count him on the floor he left until he is off; Frozen Factory level JSON minimap floor 400 cm.
2026-10-26 (`anim/blizzard-walk-rifle`): the rifle stays in the right hand in the blizzard walk - `bHideWeaponInBlizzardWalk` default false (game designer decision); ABP_Operative does not override it; `BlizzardWalkSmoke` asserts the weapon stays visible.
2026-10-23 b (`ladder/work`): ladders x turn-based (LADDERS.md 7.6) - a climber (operative or enemy) is left out of the grid, finishes the climb at normal speed and joins on the next round (on the roof where he stands); `LadderClimbSmoke` stages 4-7.
2026-10-23 (`ladder/work`): climbing enemies (LADDERS.md 7.5) - the Marksman, the Frozen PMC Fighter and the Military Zombie path through ladder links (`UNavFilter_NoVaultLadder`) and climb with the operatives' component and clips (rung IK on the enemy bodies via `FEnemyAnimInstanceProxy`, no ABP change); mutants never; flag set on the two BPs by `Scripts/Editor/setup_enemy_ladder_climbing.py`; smoke `LadderEnemySmoke` - `docs/design/LADDERS.md` section 11.
2026-10-22 e (`ladder/work`): ladders implemented (`ALadderActor`, `ULadderClimbComponent`, native rung IK, no ABP change) with two test ladders on the Frozen Factory (`Scripts/Editor/place_factory_test_ladders.py`), smoke `LadderClimbSmoke`; the factory start's set-dressing blast door is now CLOSED and blocking - `docs/design/LADDERS.md` section 11 (status).
2026-10-22 e (`agents/work`): headless navmesh bake - `Scripts/bake_navmesh.ps1 -Map <name>` (`-run=CodexTacticsEditor.CodexBuildNav`, `CodexNavBake::BuildWorldNavigation`, tests `CodexTactics.Editor.Navigation.*`): releases the editor AsyncLoadLock 0x20 that kept every commandlet build empty; replaces the editor re-save after a scripted layout pass (the factory map itself is NOT re-baked yet: the ladders branch touches it) - [TRAPS.md](TRAPS.md) navmesh rows.
2026-10-22 f (`agents/work`): the blizzard walk - outdoors in a blizzard a walking operative plays the pack's Storm_1 loops (retargeted copies in `/Game/Characters/Operatives/Anims/Weather/Storm/`) on the upper-body slot, weapon hidden, gated by `BlizzardWalkRules` (weather amount, indoors, speed, aim / actions); no ABP change; smoke `BlizzardWalkSmoke` - `docs/systems/animation-setup.md` "Blizzard walk".
2026-10-23 (`agents/work`): the game designer's play-test - every tutorial marker is the orange hologram (`/Game/UI/Materials/MI_HoloMarker_Orange`, check `CodexTactics.HoloMarkerShots`) and the hut door operatives are LOCKED in place until the third is inside (steps JSON `"lock_in_place"`) - `docs/systems/tutorial-engine.md` section 21.
2026-10-23 b (`agents/work`): the A4 part 1 redesign - zombie #1 dormant until the squad is in cover at the overlook, wakes hidden behind its container, killed quietly by ANY one operative's tripwire or mine (binding `"dormant"`, confine `"free": "first"`, `EnemyKilled.SquadTrap`) - `docs/systems/tutorial-engine.md` section 22, smokes `TutorialAmbushLakeSmoke` / `TutorialAmbushLakeMineSmoke`.
2026-10-25 (`agents/enemy-priority`): operatives first - enemies attack an operative before a barricade / turret unless every operative already has `Codex.Enemy.OperativeAttackersBeforeObjects` (default 2) others on him or is unreachable on foot; barricade vault nav areas now cover the whole box - `docs/systems/enemy-ai-and-perception.md` "Operatives first", smoke `EnemyPrioritySmoke`.
2026-10-25 (`formation/integration`): formation look sectors in the game (stage 6 of `docs/design/FORMATION_SECTORS.md`) behind `Codex.Formation.Sectors` (default 1, 0 = the old formation): terrain-picked diamond / echelon / column / wall layout, sector looks (the rear man walks backwards), patrol pace 0.8, the group cover order in group mode = crouched along the wall facing out; smoke `FormationLabSmoke` - `docs/systems/squad-and-orders.md` "Formation look sectors".
2026-10-25 (`formation/runtime-v22`): formation rules v2.2 in the game behind `Codex.Formation.Sectors` - the triad K / P / Z with places by minimal travel, the coverage allocator's patrol looks (the rear guard holds 5-7), the wall group after a group cover switch / wrap / re-entry (the leader keeps his corner, the others close the open arc - the user's playtest bug), the wall slide along the same wall - `docs/systems/squad-and-orders.md` "Rules v2.2 in the runtime", smokes `FormationWallGroupSmoke` / `FormationSlideSmoke`.
2026-10-26 (`formation/runtime-v23`): formation rules v2.3 in the runtime - the trail column keeps its stagger off the walls, ClearLook holds each man's last look, the narrow column's K / P / L looks stay fixed for the allocator, fallback stand-ins never land on another man's slot, HUD "(turn)" for the turn fold; smokes with -PreCmds start on the neutral environment - `docs/systems/squad-and-orders.md` "Rules v2.3 in the runtime".
2026-10-26 (`formation/runtime-v21`): formation rules v2.1 in the game behind `Codex.Formation.Sectors` + `Codex.Formation.V21` - the squad never splits round a wall (the mirror echelon / the trail column along the leader's path, HUD `Formation: Echelon left -> Column (wall)`), the sharp-turn fold, the bypass side of a free-standing wall by a seen / heard / reported threat, the corner watch (slicing the pie), ClearLook in every formation; the level read as walls from its box collision; dev console `Codex.Formation.Menu` (the hold-G choice) - `docs/systems/squad-and-orders.md` "Rules v2.1 in the runtime", smokes `FormationLabSmoke` / `FormationBypassSmoke`.
2026-10-26 (`combat/suppression`): suppressive fire (game designer decisions 2026-10-26) - a symmetric suppression level per unit from near misses / strikes on his cover / hits, weighted by weapon (`SuppressionRules`, `USuppressionSubsystem`); a suppressed ranged enemy does not fire back and holds his spot, a suppressed operative fires blind with a large spread, PINNED = no fire (both sides; rage ignores it); melee packs are not suppressed but scatter round a temporary fear zone of dense fire; explosions suppress briefly; the SUPPRESS order = **Shift + click on enemies** (red marker; on the marker again = cease; elsewhere Shift + click stays the green watch sector / hold line), full-auto bursts, lots of ammo; turrets and the APC MG / cannon suppress, none in turn-based, A/B `Codex.Combat.Suppression`; tuning `Content/Data/Combat/suppression.json` - `docs/design/SUPPRESSION.md`, smoke `SuppressionSmoke`.
2026-10-26 (`formation/v24-corner-peek`): the squad at a wall's end (the game designer's bug report on rules v2.3) - a move along the wall to its free end is the corner stack (the point man peeks round the corner, the leader second, the rear man back), the wall layout never puts a man on the far side of a wall from the leader (T / L junctions), the squad's dashed guide lines along / round its wall, a leader switch at the wall keeps the hug and the next slide works - `docs/systems/squad-and-orders.md` "Rules v2.4 at a wall's end", `docs/design/FORMATION_SECTORS.md` v2.4.

2026-10-26 (`formation/v24-wall-end`): rules v2.4 - the wall END (user bug report): at the end face of a thin free-standing wall the wall layout's looks are spread over the whole open arc round the end, both flanks along the faces (`FormationWallRules::EndLooks`, 69 -> 100 % of the open ring held in the sim, 63 -> 98 % in the game); a face keeps the user's L 8 / K 6 / P 4; `Codex.Formation.WallEnd` (A/B), bot check 13, lab route `R_G_end` - `docs/systems/squad-and-orders.md` "Rules v2.4 in the runtime", contract 14, smoke `FormationWallGroupSmoke` scene 4.
2026-10-26 b (`formation/v24-wall-end`): rules v2.5 - the covered approach to the wall (new user rule): on the order to hug a wall the leader runs in and looks along it, the man already looking back covers the rear (walking backwards), the others a flank each, until everyone is at his slot (`FormationWallRules::Approach`, `Codex.Formation.Approach`, bot check 14) - `docs/systems/squad-and-orders.md` "Rules v2.5 in the runtime", contract 15, smoke `FormationWallGroupSmoke` scene 5; the interpretations wait for the user (FORMATION_SECTORS.md v2.5).
2026-10-26 c (`gameflow/realtime-horde-squad-combat`): the tactical pause is only an aid (user decisions) - (1) the horde warning keeps the fight in real time (the wave's first Space = the free pause; `Codex.HordeIntro.AutoPause`); (2) the squad stays in formation through the fight, only B switches to Solo, ordered members carry their orders out and rejoin (`Codex.Squad.CombatFormation`; root cause: the formation was "exploration only", Godot is_tactical_mode); (3) objects / barricades move in the real-time fight too (`Codex.Relocation.RealTimeCombat`); (4) only the fight's first wave is warned, later waves only the arrows (`Codex.HordeIntro.LaterWaves`) - `docs/systems/waves-horde-encounters.md` "The horde warning: real time...", `squad-and-orders.md` "The squad stays in formation in the fight", `interactables-traps-loot.md`; smokes `HordeIntroSmoke` (rewritten), `CombatFormationSmoke` (new), `PauseBarrelOrderSmoke` / `ClickRulesSmoke` / `LakePauseOrdersSmoke` updated. Open for the user: the other automatic pauses (tutorial A4 lesson pause, the grid's end) - listed in waves-horde-encounters.md.

2026-10-26 c (`formation/v24-squad-peek`): the squad's corner peek (user bug report) - the squad hugging a wall in formation shows the peek silhouette of the member nearest the hovered end of its wall and, on the click, he takes the corner in cover and peeks round it (the single operative's corner peek) while the others form the corner stack behind him (the leader second); the ground keeps the guide lines - `docs/systems/squad-and-orders.md` "The squad's corner peek", smoke `FormationSquadPeekSmoke`.
2026-10-26 c (`ui/formation-radial-menu`): the formation radial menu on HOLD B (stage 7; G is the grenade): tap B = solo / squad as before (on the release), hold 0.25 s = the radial menu at the cursor (8 formations + Auto in the centre, English labels, icons `/Game/UI/FormationMenu`), release = pick (`SetFormationMenuChoice`), Esc / RMB cancel; only squad mode with >= 2 members, exploration / tactical pause - `docs/systems/squad-and-orders.md` "The hold-B radial menu", smoke `FormationMenuSmoke`.
2026-10-26 c (`tactics/turn-arena-buildings`): the turn-based arena relaxed (game designer) - the grid round the leader as before 2026-10-22, buildings (roof / indoor zone outlines, walled sheds, their doors) and ladders subtracted (closed cells), an 8 x 8 / 8 x 4 open area required, operatives inside moved out through the door, deep inside refused - `docs/systems/combat-and-fire.md` "Turn-based arena", smokes `TurnArenaYardSmoke` / `TurnArenaLakeSmoke`.
2026-10-26 c (`formation/v24-squad-peek`): the squad's corner peek (user bug report) - the squad hugging a wall in formation shows the peek silhouette of the member nearest the hovered end of its wall and, on the click, he takes the corner in cover and peeks round it (the single operative's corner peek) while the others form the corner stack behind him (the leader second); the ground keeps the guide lines - `docs/systems/squad-and-orders.md` "The squad's corner peek", smoke `FormationSquadPeekSmoke`.
2026-10-26 c (`ui/formation-radial-menu`): the formation radial menu on HOLD B (stage 7; G is the grenade): tap B = solo / squad as before (on the release), hold 0.25 s = the radial menu at the cursor (8 formations + Auto in the centre, English labels, icons `/Game/UI/FormationMenu`), release = pick (`SetFormationMenuChoice`), Esc / RMB cancel; only squad mode with >= 2 members, exploration / tactical pause - `docs/systems/squad-and-orders.md` "The hold-B radial menu", smoke `FormationMenuSmoke`.
2026-10-26 c (`gameflow/realtime-horde-squad-combat`): the tactical pause is only an aid (user decisions) - (1) the horde warning keeps the fight in real time (the wave's first Space = the free pause; `Codex.HordeIntro.AutoPause`); (2) the squad stays in formation through the fight, only B switches to Solo, ordered members carry their orders out and rejoin (`Codex.Squad.CombatFormation`; root cause: the formation was "exploration only", Godot is_tactical_mode); (3) objects / barricades move in the real-time fight too (`Codex.Relocation.RealTimeCombat`); (4) only the fight's first wave is warned, later waves only the arrows (`Codex.HordeIntro.LaterWaves`) - `docs/systems/waves-horde-encounters.md` "The horde warning: real time...", `squad-and-orders.md` "The squad stays in formation in the fight", `interactables-traps-loot.md`; smokes `HordeIntroSmoke` (rewritten), `CombatFormationSmoke` (new), `PauseBarrelOrderSmoke` / `ClickRulesSmoke` / `LakePauseOrdersSmoke` updated. Open for the user: the other automatic pauses (tutorial A4 lesson pause, the grid's end) - listed in waves-horde-encounters.md.

2026-10-26 c (`squad/fallen-gear`): a fallen operative's gear after the fight - the remains are a click box fitted to the lying body (not a crate at the capsule's feet the cursor missed), never relocated, labelled "[Click] Search the body", holding everything he carried (clip + reserve, grenades, his weapon); a knocked-down operative gets up once the fight ends - `docs/systems/interactables-traps-loot.md` "A fallen operative's gear", smoke `FallenGearSmoke`; open question: may the squad take a knocked-down (living) mate's gear? (answered 2026-10-26: NO; the remains trimmed to the Godot list.)
2026-10-26 d (`gameflow/user-answers-1026`): the game designer's answers - the tactical pause only by Space (the A4 lesson pause retired, the grid's own end -> real time), no free first pause of a wave (an ordinary pause; banner "[SPACE] tactical pause N/M"), group cover in the fight (real time + the pause: planned, ordered on the release; `Codex.GroupCover.InCombat`), no gear from a knocked-down living mate, the remains = the Godot list, the squad corner peek confirmed - `waves-horde-encounters.md`, `squad-and-orders.md`, `interactables-traps-loot.md`, `combat-and-fire.md`; smokes `GroupCoverSmoke` (WF / WP / WS), `HordeIntroSmoke`, `TurnBasedSmoke`, `FallenGearSmoke`, `TutorialAmbushLake*Smoke`.
2026-10-27 (`formation/wall-hug-gap`): the squad hugs every wall tight (the game designer's play-test in the lake yard) - the formation's wall traces read only what blocks pawns (the yard's tutorial trigger volume made the wall layout read the wall AT the clicked slot: 45 cm farther out, facing the wall at the west face), and a member settled in the wall pose glides onto his exact place (one operative in cover's 45 cm, not the walk's stop 25 cm short): 69-281 -> 45-46 cm off the face; a wall slide keeps its lead men 40 cm clear of the leader (else a slide right after the hug stalled at a lead man still walking in); `Codex.Formation.WallTraceBlocking` / `Codex.Formation.WallSnap` (A/B), smoke `FormationWallHugSmoke` on the real yard - `docs/systems/squad-and-orders.md` "The squad hugging a wall".
What is waiting for the user: section 7 (open questions) and `docs/port/NEXT_STEPS.md`.

---

## 1. Read first (in this order)

1. This file.
2. `CLAUDE.md` — project rules (Godot is read-only, no GAS, commit per verified system, ask only on real design forks).
3. `docs/port/PORT_MATRIX.md` — Godot file → UE class table with status and tests; intentional deviations.
4. `docs/port/REFERENCE_PLAYTHROUGH_01.md` — the user's reference video, the order the port follows.
5. `docs/port/TANDEM.md` — coordination log between agents (claim files before editing, one builder at a time).
6. `docs/port/ARCHITECT_SUPERVISOR_DIRECTIVE.md` — Gemini's directive; §4 lists where Godot code wins over it.
7. `docs/systems/README.md` - the per-system docs (the old system map); start with the doc of the system you touch.

Godot source of truth: `C:/Users/Zephyrus15Duo/Documents/Codex/godot-test-01/` (READ ONLY — never edit).
The user's rule: **Godot code values win** (except explicit user decisions listed in PORT_MATRIX "deviations").

## 2. Environment facts and traps (read before touching anything)

One line per trap; the full rows (with consequences and exact commands) are in [TRAPS.md](TRAPS.md).

- **Paths / junction:** engine `C:/Program Files/Epic Games/UE_5.8`; `Documents/Codex/unreal` is a junction to this repo, so an agent "in Codex/unreal" edits the same files - claim work in TANDEM, build one at a time. [TRAPS.md](TRAPS.md)
- **Editor locks the DLLs:** close the Unreal Editor before `build.ps1` (graceful `CloseMainWindow`, wait 45 s; never force-kill a window with a save dialog). [TRAPS.md](TRAPS.md)
- **Build log is UTF-16-ish:** read `Saved/Logs/Build.log` with PowerShell `Get-Content`, not grep. [TRAPS.md](TRAPS.md)
- **UHT / MSVC names:** no members shadowing `AActor` (`Role`, `Instigator` -> `SquadRole`, `InstigatorName`); C4458 is an error. [TRAPS.md](TRAPS.md)
- **Commandlet output:** `unreal.log()` in `-run=pythonscript` does not reach stdout - write results to `Saved/Logs/*.txt`; editing an inherited component via Python CDO needs `set_editor_property` on the template. [TRAPS.md](TRAPS.md)
- **Git LFS** for `.uasset/.umap`, never mark them `lockable`. [TRAPS.md](TRAPS.md)
- **L_MovementTest was re-laid by the user** (rotated 180 deg, moved): smokes write design coordinates and map them with `SmokeUtils::LevelPoint` / `LayoutTransform`; never hard-code world coordinates. [TRAPS.md](TRAPS.md)
- **Empty navmesh at runtime:** a saved rotated / unaligned `RecastNavMesh-Default` stays empty; maps ship without one (RuntimeGeneration=Dynamic) and editor scripts destroy it before saving; bake it afterwards with `Scripts/bake_navmesh.ps1 -Map <name>` (commandlets otherwise never build: AsyncLoadLock 0x20). "leader on navmesh=0" in a smoke -> check this first. [TRAPS.md](TRAPS.md)
- **Unity builds merge .cpp files** (adaptive unity builds changed files alone: a branch builds, its merge into main fails): every file-local name of a .cpp must be unique in its module, `#undef` your `#define`s, shared helpers go to an `…Internal.h`; check with `python Scripts/Tools/find_unity_clashes.py`, proof builds `build.ps1 -FullUnity` / `-NoUnity`. [TRAPS.md](TRAPS.md)
- **No emoji in default fonts:** the HUD / menus strip them (`StripUnsupportedGlyphs`); texts stay verbatim. [TRAPS.md](TRAPS.md)
- **CommonUI input config:** keep `bEnableDefaultInputConfig=False`; screens are pooled - reset per-show state in `NativeOnActivated`, not the constructor. [TRAPS.md](TRAPS.md), [dialogue-frontend-mission](../systems/dialogue-frontend-mission.md)
- **Long Python in bash heredocs breaks:** write it to the scratchpad with the file tool and run `python <file>`. [TRAPS.md](TRAPS.md)
- **Smokes run on a fixed 60 fps step** (`smoke_parallel.ps1`): wait on CONDITIONS with generous caps, never on fixed seconds; `CustomTimeDilation = 0` does not stop an enemy's `TickBehavior` (also `SetActorTickEnabled(false)`, `SmokeUtils::ParkEnemyFarAway`). [TRAPS.md](TRAPS.md), [build-test-smokes](../systems/build-test-smokes.md)
- **`AOperativeCharacter` = one class, 16 .cpp files:** put new code in its cluster file, shared helpers in `OperativeCharacterInternal.h`. [TRAPS.md](TRAPS.md), [stances-and-movement](../systems/stances-and-movement.md), `REFACTOR_OPERATIVE_2026-10.md`
- **`ACodexTacticsPlayerController` = one class, 10 .cpp units:** new key / click handler in the unit of its input area, shared helpers in `CodexTacticsPlayerControllerInternal.h`, each unit keeps and `#undef`s `LOCTEXT_NAMESPACE`. [TRAPS.md](TRAPS.md), [squad-and-orders](../systems/squad-and-orders.md), `REFACTOR_CONTROLLER_2026-10.md`
- **`UPlaytestBotSubsystem` only in development builds and Game / PIE worlds:** null-check `GetSubsystem<UPlaytestBotSubsystem>()`; in Test / Shipping `-CodexBot` and `Codex.Bot.*` do not exist. [TRAPS.md](TRAPS.md), [tools-bot-waveeditor](../systems/tools-bot-waveeditor.md)
- **Dev console commands (smokes, probes, shots) live in module `CodexTacticsDev`:** link against `CodexTactics`, so runtime symbols they call need `CODEXTACTICS_API` and runtime `Private/` headers are not includable; `test.ps1 -CheckMap` scans the folder. [TRAPS.md](TRAPS.md)
- **Mesh on the capsule:** the skeletal mesh may move relative to the capsule only by the tracked cover offsets; `EnforceMeshBaseOffset` puts anything else back every frame (smokes: `GetMeshBaseOffsetCm`, `ResetBodyOffsets`). [TRAPS.md](TRAPS.md), [cover-and-peek](../systems/cover-and-peek.md)
- **Holding a montage on its last frame:** clear `bEnableAutoBlendOut` on the running instance, not on the montage asset. [TRAPS.md](TRAPS.md), [animation-setup](../systems/animation-setup.md)
- **Blend spaces made by script** need `ValidateSampleData` + `ResampleData`, else the player outputs the reference pose (T-pose) with no error. [TRAPS.md](TRAPS.md), [animation-setup](../systems/animation-setup.md)
- **Two agents, one checkout:** set `$env:CODEX_AGENT` (claude / gemini); every build / test / smoke / verify_all takes `Saved/agent.lock` (`Scripts/agent_lock.ps1 -Status`). [TRAPS.md](TRAPS.md), `TANDEM.md`

## 3. How to work, build, test, verify

**Worktrees and the lock.** Parallel work happens in a `git worktree` on an `agents/<topic>` branch (TANDEM protocol 8: its own `Binaries` / `Intermediate` /
`Saved/agent.lock`; untracked asset packs and `DerivedDataCache` are directory junctions to the main folder, read-only use; tracked user-edited assets are the committed
versions there). Never edit another agent's worktree or the main checkout from yours; the lead merges into `main` once the user's editor is closed, after build + tests
pass. One builder at a time: `$env:CODEX_AGENT = "claude"` (or `"gemini"`), `Scripts/agent_lock.ps1 -Status` shows the holder; builds refuse while the user's editor is open.

```
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1                    # [-FullUnity | -NoUnity]; runs the unity-clash finder first (yellow WARNING per hit)
python Scripts/Tools/find_unity_clashes.py                                   # the clash check alone (exit 1 on a clash)
powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Changed [-Quick]   # impact map Scripts/test_map.json decides which unit tests + smokes run (-Smart = -Changed)
powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 [-Filter CodexTactics.Cold]
powershell -ExecutionPolicy Bypass -File Scripts/smoke.ps1 -Command CodexTactics.DeployableSmoke
powershell -ExecutionPolicy Bypass -File Scripts/verify_all.ps1              # build + all tests + all smokes (nightly / milestone, editor closed)
```

- **Verification tiers.** edit: `build.ps1` then `test.ps1 -Changed -Quick`; pre-commit: `test.ps1 -Changed` + `test.ps1` (all unit tests) +
  `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude`; nightly / big merges: `verify_all.ps1`. Add a rule to `test_map.json` whenever you add a smoke or a system
  (`test.ps1 -CheckMap` fails on an unmapped `Debug/*Smoke*Command.cpp`).
- **The `Smoke` suffix rule.** Smoke command names are the FULL names (`MovementSmoke`, `CodexTactics.DeployableSmoke`); `smoke_parallel -Commands` with a short name
  (`Movement`) silently times out.
- **Fixed-step note.** `smoke_parallel.ps1` runs every smoke with `-UseFixedTimeStep -FPS=60 -FixedSeed` (each frame = exactly 1/60 s whatever the load; per-smoke
  `smokeOptions` in `test_map.json`: `realTime`, `fixedStepFps`, `timeout`). `smoke.ps1` still runs on the wall clock: add `-Extra "-UseFixedTimeStep -FPS=60 -FixedSeed"` to
  reproduce a batch result. A PASS* in the batch summary = passed only on the re-run (suspect flakiness).
- **Unity-clash check.** See the trap above; `build.ps1` runs it before every build.
- **Ownership with Gemini (TANDEM).** Claude: gameplay C++, logic, AI, UI, animation setup, data, tests, Wave Editor. Gemini (Lead Architect / Technical Director): ADRs, shaders /
  materials, VFX, rendering / scalability, profiling; he formulates specs in `docs/port/TANDEM.md`. Cross-boundary changes go through TANDEM «Requests»; claim `.uasset`s in
  «In progress» (they cannot be merged). TypeSafe (Jev) tooling: `Scripts/Tools/typesafe_triage.py` - use it for the pre-commit audit and telemetry digests, not for C++ syntax.
- **Save rules (user files).** Before ANY merge in `main`, back up the user's uncommitted files (copy them and verify the hashes) - the user edits maps, packs and Blueprints
  in the main folder. Commit only your own files. Conventional Commits, body references the phase / Godot file, `Co-Authored-By` trailer; commit + push to `main` after
  build and tests pass (no PRs / branches unless asked).
- **Never touch:** `Content/Maps/L_MovementTest.umap` (the user's layout is the baseline: never reset / checkout / regenerate it), `Config/DefaultEditor.ini` and
  `Config/DefaultInput.ini` (local editor changes, never committed), `Config/DefaultScalability.ini` and `Content/VFX` (Gemini's), the imported packs (Combat_Dog, RifleAnims,
  Post_Apo_Survivor, monsters, Mannequins, `90+_Weather_Animations`, ...), and the Godot repo `Documents/Codex/godot-test-01` (read-only archive).
- **Docs with every commit.** The matching `docs/systems/*.md`, a row in `docs/port/CHANGELOG.md`, `PORT_MATRIX.md` where a row changes, and the TANDEM log. Keep this file short.

Full build / test reference, Unreal MCP notes and the table of every smoke command: [build-test-smokes](../systems/build-test-smokes.md).

## 4. Conventions

- C++ first, Blueprints for look / tuning only. **Characters are Blueprints the user owns** (mesh, AnimBP, collision):
  C++ exposes EditDefaultsOnly properties + BlueprintImplementableEvents, never hardcodes visuals a BP cannot override.
- Each class header names its Godot reference file / functions. Epic coding standard (A/U/F/E/I, TObjectPtr, IWYU).
- Player-facing texts: **verbatim from Godot** (Russian, emoji included) in `LOCTEXT`.
- Colours from Godot are sRGB: use `FLinearColor::FromSRGBColor(FColor(...))`.
- Units: Godot metres × 100 = cm; Godot `+Y up` → UE `+Z up`.
- Commits: Conventional Commits, body references the Godot file(s), `Co-Authored-By` trailer. Only verified work.
- After each system: update PORT_MATRIX row, TANDEM log, this file (§5, §8, §10).
- Since the 2026-10-19 split of this file: the system detail goes into the matching `docs/systems/*.md` (not here), the change-log row into `docs/port/CHANGELOG.md` (old §10), open questions into `docs/port/OPEN_QUESTIONS.md` (old §7); keep this index short.

## 5. System map (UE -> Godot): the per-system docs

One doc per system in [docs/systems/](../systems/README.md); each starts with a 3-5 line summary and the key files, followed by the old text verbatim.

| Doc | What is in it |
|---|---|
| [squad-and-orders](../systems/squad-and-orders.md) | GameMode squad spawn, input (PlayerController), orders / who owns an operative's movement, formation, fire postures, per-member Blueprints. |
| [stances-and-movement](../systems/stances-and-movement.md) | AOperativeCharacter overview: stances / capsule per stance, sprint, vault, carrying, inventory limits, source layout of the 16 cluster files. |
| [cover-and-peek](../systems/cover-and-peek.md) | Cover slots / enter / leave / shimmy, followers leaving cover, the group cover order (others beside / prone behind), corner peek that reveals traps (physically, one by one), return to cover after a knockdown. |
| [combat-and-fire](../systems/combat-and-fire.md) | Targeted shots, quick fire (Ctrl + click), fire postures / ROE, combat feedback, knockdown values, M16 range, turn-based combat manager and grid. |
| [sight-and-vision-cones](../systems/sight-and-vision-cones.md) | Squad sight (symmetric, also while exploring), peek sight from the leaned-out eye, occluded vision cones, how to switch cones on. |
| [enemy-ai-and-perception](../systems/enemy-ai-and-perception.md) | Enemy registry (no actor iterators in hot paths), pack flanking detour; parity notes for enemy AI live in KNOWN_GAPS. |
| [stealth-lure-and-scent](../systems/stealth-lure-and-scent.md) | Whistle / stone lures, enemy investigation and re-target rules, hearing round walls, hound scent (sniff, follow, detect), tutorial locks. |
| [environment-weather](../systems/environment-weather.md) | Day cycle + procedural weather: phase 1 core implemented (`UEnvironmentSubsystem`); cold / roster / visuals designed. |
| [survival-cold-heat](../systems/survival-cold-heat.md) | Cold accumulation / tiers, mobile and tactical heat (campfire, stove, flares, heat packs, fire puddle), weather idle poses and the squad gathering at the fire. |
| [interactables-traps-loot](../systems/interactables-traps-loot.md) | Interaction menus, barrels, relocation, deployables (barricade / mine / turret), loot crates, generator, quest chain and gate, spotted-trap Solo, PMC tripwire how-to. |
| [waves-horde-encounters](../systems/waves-horde-encounters.md) | Phases (Exploration -> Preparation -> WaveCombat -> ...), sudden horde start, horde intro flyover, level enemies / guard posts / ambush start, wave data. |
| [tutorial-engine](../systems/tutorial-engine.md) | **The steps JSON schema reference** (every key, default, precedence, save behaviour, examples), command choke point and locks, state machine / overlay, demo flags, how to place volumes / markers / steps; revision history below it. |
| [demo01-lake](../systems/demo01-lake.md) | Per-revision decision blocks of the Demo 01 tutorial level (rev 4 .. rev 5i, 2026-10-19), what the designer places in the lake map, lessons (posture, medkit, A4 ambush). |
| [save-load](../systems/save-load.md) | JSON save slots (format v3), save policy (outside combat / dialogue), load = reopen the level, the 11-step restore order, the state inventory (saved / derived / gaps), how to add saved state, robustness (corrupt files, missing map). |
| [camera-hud-minimap](../systems/camera-hud-minimap.md) | Tactical camera and camera zones, roof cutaway, building exits, minimap / full map, HUD, phase banners, action bar. |
| [dialogue-frontend-mission](../systems/dialogue-frontend-mission.md) | Dialogue sequences, CommonUI frontend (title, main menu, pause menu, save slots), mission start / mission rules, narrative pipeline. |
| [vehicles](../systems/vehicles.md) | Kinematic nav-driven APC: boarding / seats, driving, cover, turret and cannon, hull damage / repair, run-over decisions, how to place it and make L_VehicleTest. |
| [animation-setup](../systems/animation-setup.md) | AnimInstance, left-hand IK and aim offsets (what the user adds to the graph), grenade / knockdown / hit / sniper clips, interaction clips, art (operatives, enemies). |
| [tools-bot-waveeditor](../systems/tools-bot-waveeditor.md) | Editor python scripts, the playtest bot, the Wave Editor and Jev (TypeSafe) tooling - pointers into the code and the change log. |
| [build-test-smokes](../systems/build-test-smokes.md) | Unreal MCP, build / test / verify commands, verification tiers, fixed-step smokes, and the full table of every smoke command with what it checks. |

Also: [TRAPS.md](TRAPS.md) (full §2), [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md) (§7), [NEXT_STEPS.md](NEXT_STEPS.md) (§8), [KNOWN_GAPS.md](KNOWN_GAPS.md) (§9),
[CHANGELOG.md](CHANGELOG.md) (§10), [PORT_MATRIX.md](PORT_MATRIX.md) (Godot file -> UE class table), [TANDEM.md](TANDEM.md), [PROJECT_STATUS_2026-10-14.md](PROJECT_STATUS_2026-10-14.md).

## 6. Data / content status (old §6): where it went

The old §6 blocks (how the user places things, what the artist edits, ABP setup) are now in the doc of their system:

| Old HANDOFF.md | Now |
|---|---|
| §1 Read first | [HANDOFF.md](HANDOFF.md) §1 |
| §2 Environment facts and traps | [TRAPS.md](TRAPS.md) (full table), condensed list in [HANDOFF.md](HANDOFF.md) §2 |
| §3 Build, test, verify (+ the smoke table) | [build-test-smokes.md](../systems/build-test-smokes.md) |
| §4 Conventions | [HANDOFF.md](HANDOFF.md) §4 |
| §5 System map | [docs/systems](../systems/README.md), by system (section 5 above) |
| §6 «Tutorial: how the user places volumes ...» | [tutorial-engine.md](../systems/tutorial-engine.md) (how-to) and [demo01-lake.md](../systems/demo01-lake.md) (rev blocks) |
| §6 «APC: how the user places it ...» | [vehicles.md](../systems/vehicles.md) |
| §6 «How to place a PMC tripwire» | [interactables-traps-loot.md](../systems/interactables-traps-loot.md) |
| §6 «ABP: left-hand IK / aim offset / grenade / knockdown / hit reactions / Female Soldier + sniper», «Interaction clips» | [animation-setup.md](../systems/animation-setup.md) |
| §6 «Weather idle poses» | [survival-cold-heat.md](../systems/survival-cold-heat.md) |
| §6 «Frontend: what the artist edits», «Narrative pipeline» | [dialogue-frontend-mission.md](../systems/dialogue-frontend-mission.md) |
| §6 «Save system» | [save-load.md](../systems/save-load.md) |
| §6 data blocks (tactical heat, vision cones, M16 range, sudden horde, per-member BP, GameMode squad) | [survival-cold-heat](../systems/survival-cold-heat.md), [sight-and-vision-cones](../systems/sight-and-vision-cones.md), [combat-and-fire](../systems/combat-and-fire.md), [waves-horde-encounters](../systems/waves-horde-encounters.md), [squad-and-orders](../systems/squad-and-orders.md) |
| §7 Open user decisions / questions | [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md) (condensed in [HANDOFF.md](HANDOFF.md) §7) |
| §8 Next steps | [NEXT_STEPS.md](NEXT_STEPS.md) |
| §9 Known gaps / tech debt | [KNOWN_GAPS.md](KNOWN_GAPS.md) |
| §10 Change log | [CHANGELOG.md](CHANGELOG.md) |

## 7. Open user decisions / questions (condensed; the full text is in [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md))

- **CLOSED 2026-10-26 (user answers, branch `gameflow/user-answers-1026`):** (1) "the tactical pause only by Space": YES - the A4 lesson pause (tutorial action `tactical_pause`, retired) and the grid's own end (now straight to real time) are gone; (2) the wave's first pause free: NO - an ordinary pause, the banner shows the charges; (3) group cover in combat: YES - real time and the tactical pause (planned, ordered on the release), not turn-based; Solo + single selection stays individual; (4) gear from a knocked-down living operative: NO (kept); (5) the remains trimmed to the Godot list (ammo, medkits, food, grenades, the weapon); (6) the squad's corner peek (peeker stands at a high wall, the others crouched): CONFIRMED. Details: `waves-horde-encounters.md`, `squad-and-orders.md`, `interactables-traps-loot.md`, `combat-and-fire.md`.

- **Melee close-in** shipped (2026-10-18, `ai_tuning.json`); its follow-ups (pack flanking detour, return to cover after a knockdown) are done; the bite-count balance is the open part.
- **Demo 01 rev 5 defaults to confirm** (beat sheet §12): the shaky bridge refuses a second operative; the low fence needs manual prone; moves refused while the HQ intro plays; cones on `O` are session-only; pre-horde patrol = 2 frozen + 2 hounds; stance / vault / cover volumes at the approach (`VOL_A3_*`); precredit on by default.
- **Placed-enemy archetype fix** (approved): a designer who wants another type on a `BP_Enemy_X` sets the field on the instance.
- **APC P4 hull** (9 choices): hull 1200 HP, per-enemy multipliers, enemies target only a manned and running APC within 40 m, crew ejected into cover, repair = Engineer only (5 s), object shot = cannon only, no wheel damage.
- **APC P2** (10 choices): small enemies are run over at >= 1.5 m/s, a downed Brute is not, ram deals 40 and no hull damage, MG stagger removed, turret holds fire in Passive, no sounds yet.
- **APC P1** (7 choices): the pause refuses vehicle orders, turn-based entry is refused while aboard, Alt + Y semantics, `VehicleNav` opt-in per bounds volume, engine heater zone, no path ribbon, slope limit 44 deg.
- **Real time vs turn-based damage numbers** (`tb_parity_2026-10-11.md`): crouch defence 0.75 / 0.70, barrel 120 / 80, mine 120 (90) / 50, blast dodge only in TB - choose per row.
- **Blast gate tied to the generator:** 0.4 %/s, wave caps 30 / 60 / 90 %; «Start Battle» still opens it at once (squad placed behind it); the Wave Editor has no UI for `gate_*` keys yet.
- **Sprint 15 heat** (10 gaps decided by Claude): heat pack -35 points, items from crates, keys U / Shift + G / F, crafting = 1 match + 1 firewood, campfire noise, wind rule, fire fear, no flares in turn-based, burning-barrel puddle, no night system.
- **Sniper vs move orders** (confirmed): automatic fire never interrupts a move order; direct fire orders stop her.
- **Enemy AI direction / bot balance** (2026-10-04): VETERAN target win rate 40-75 %, ranged TB enemies, per-archetype morale; wave 3 and the cold drain are the user's to rebalance (NORMAL 2/10, VETERAN 4/10, CASUAL 1/5 in the bot batch).
- **Settled user decisions** (kept as reference): L_MovementTest stays re-laid; `DefaultEditor.ini` never committed; M16 clip 30; level waves spawn all at once like Godot; start menu has two modes; `CombatStart` tag; turn-based deviations; stage loadout; turn-based entry on flat ground only; `FText::ToLower` for Cyrillic.

## 8. Next steps (old §8: [NEXT_STEPS.md](NEXT_STEPS.md))

Waiting for the user: place / re-id the Demo 01 rev 5 volumes and actors (`docs/systems/demo01-lake.md`, beat sheet §10.1); run `Scripts/Editor/setup_apc_vehicle.py` (editor
closed) and build `L_VehicleTest` (`docs/systems/vehicles.md`); confirm Sprint 15 heat, the sudden-horde flyover and the sniper model; approve or reject the bot's stealth proposal
(`Saved/Coach/stealth_proposal.md`). Ahead: Demo 01 phases 2-3 (encounter groups, A4-A10 steps), APC P3 (turn-based) and P5 (tutorial beat V). Items 1-7 of the old list (loot crates, turrets, generator, targeted shots, UI shell,
the turn-based manager, the Phase 2 data importers) are done; item 8 (content: level, VFX, cutscene; the "twisted" character look) stays open.

## 9. Known gaps / tech debt (old §9: [KNOWN_GAPS.md](KNOWN_GAPS.md))

Parity notes and deliberate deviations per system (enemy AI parity pass and re-path throttle, floating texts, silhouette, camera choreography, camera shake, wave victory, progression,
panic, space hold, barricade contact damage, vault, hidden-mine scan, weapon-switch meshes, grenades, turn-based visuals, click rules, save / load differences, tracers), plus the smoke-reliability
note (known flaky list is EMPTY since 2026-10-15) and the Phase 2 data importer (values still hand-typed). Read the part for the system you touch.

## 10. Change log (old §10: [CHANGELOG.md](CHANGELOG.md))

Newest first, one table row per commit (`| Commit | What |`). Append your row at the top of the table in `docs/port/CHANGELOG.md` with every commit, and keep the matching
`docs/systems/*.md` current. Search it for history: `grep -n "keyword" docs/port/CHANGELOG.md`.
