# Squad, orders and the player controller

> The squad is spawned by `ACodexTacticsGameMode` from `BP_Operative` (roster: name, colour, role, luck) and driven by `ACodexTacticsPlayerController`
> (Enhanced Input built at runtime; one header, members split into 10 .cpp units by input area) and `USquadSubsystem` (formation, follower rules).
> The order-ownership rules (`SquadOrderRules`: own move order, Solo holds, followers leaving cover) are the part most often touched by play-test fixes.
>
> Key files:
> - `Source/CodexTactics/Private/Core/CodexTacticsPlayerController*.cpp` (+ `…Internal.h`), `Core/CodexTacticsGameMode`, `Core/CodexTacticsGameState`
> - `Characters/SquadSubsystem`, `SquadFormation`, `SquadOrderRules`, `OperativeMovementRules`, `OperativeAIController`
> - Docs: `docs/port/REFACTOR_CONTROLLER_2026-10.md`; assets `/Game/Blueprints/BP_CodexTacticsGameMode`, `/Game/Characters/Operatives/BP_Operative`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Core (old §5): game mode, player controller, game state

**Core**
- `Core/CodexTacticsGameMode` — squad spawn from `BP_Operative` (roster: name, colour, offset, fortitude, role, luck),
  HUD class, deployable spawn classes. Godot `main.gd` bootstrap.
- `Core/CodexTacticsPlayerController` — input (Enhanced Input created at runtime): LMB click (select / object / move;
  double click = sprint; on another cover from a wall edge in cover = the cover switch dash, 2026-10-20, see cover-and-peek), 1–3 select, Z/C/V stance, B solo, Q/E + RMB drag rotate, MMB pan, Ctrl + click = fire (targeted shot / quick fire at a point, 2026-10-19), wheel zoom (BindKey),
  Space tap / hold, R rotate placement, **F set-up from supply**. Placement mode: LMB confirm, RMB cancel, wheel rotate.
  One header, members split by input area into `Private/Core/CodexTacticsPlayerController{,Selection,Orders,Stance,Items,Time,
  Menus,Camera,Cover}.cpp` + `CodexTacticsPlayerControllerInternal.h` (cleanup 2026-10-15, `docs/port/REFACTOR_CONTROLLER_2026-10.md`).
- `Core/CodexTacticsGameState`.

## Squad subsystem and who owns an operative's movement (old §5)

- `Characters/SquadSubsystem`, `SquadFormation`, `OperativeMovementRules`, `OperativeAIController`.
- **Who owns an operative's movement (rev 5g, game designer play-test 2026-10-14):** pure `Characters/SquadOrderRules` (`IsOnOwnOrder`, `ShouldStopOnBecomingLeader`, `ShouldHoldNow`; tests `CodexTactics.Squad.Orders.*`). `AOperativeCharacter::HasOwnMoveOrder` (`bOwnMoveOrder`: set by `OrderMoveTo` = player / interaction walk-up / relocation / autonomy / bot, cleared by arrival `HandleMoveFinished`, `StopOperative`, a formation `FollowTo`, `ReleaseOwnMoveOrder`); `USquadSubsystem::IsOnOwnOrder` = own move OR a running use order (`UInteractionSubsystem::HasUseOrder`) OR an object task (`URelocationSubsystem::HasActiveTask`). Root cause of "selecting B cancels A's order": `SetLeader` stopped the new leader unconditionally and, in Solo, stopped + crouched every follower (the old leader too); in group mode `UpdateFollower` re-pathed the old leader to his formation slot every 0.2 s (the formation had no notion of an own order). Now: the new leader is stopped only when he walked a formation move; Solo holds (`SetLeader` in Solo, `EnterSoloMode`, `SetFollowersHolding`) stop / crouch only idle followers; `UpdateFollower` skips a follower on his own order (he joins the formation on the first repath after it ended); `ExitSoloMode` (Regroup) releases the followers' own move orders. Smokes: `SoloRejoinSmoke` (stage 100+: group / Solo order keep), `RealtimeSelectSmoke` (real-time fight: A ordered, key 2, B ordered, key 1: both arrive), `GroupSelectSmoke` (exploration: a box-selected pair keeps its group move after the third is picked, then rejoins the formation).

- **Solo's guard crouch waits (2026-11-02, the game designer's review of ambush v3: the two waiting while the leader climbs a tower crouched at once).** Solo on (B, the bot's tower climb, a spotted trap) or a leader switch in Solo: a holding follower who stands crouches only after `squad_roe.json` `hold_crouch_after_sec` standing still - the autonomy's hold crouch delay, the same tunable (`SquadAutonomyRules::StartSoloGuardCrouch` / `StepSoloGuardCrouch`, `USquadSubsystem::ArmSoloGuardCrouch` / `UpdateSoloGuardCrouch`); a prone / crouched one takes the guard crouch at once as before; the wait ends without a crouch on Regroup, when he becomes the leader, on an order of his own, in the trap hold's cover, in a group cover prone or when anything else changed his stance. Test `CodexTactics.Characters.SquadAutonomy.SoloGuardCrouchDelay`, smoke `SquadHaltStanceSmoke`.

- **Regroup, then the leader runs: the others run too (bug 2026-10-20, lake demo after the hut lesson: "Regroup (B), the Commander RUNS (double-click) - the two others followed in formation but WALKED; Solo on / off again cured it").** Root cause: `ExitSoloMode` (Regroup) gives every follower the leader's stance of that moment - after the hut lesson the selected leader was one of the door men, crouched by Solo's guard crouch, so the others got a crouch too; then the Commander was selected and sent sprinting: his own sprint order stands HIM up (`OrderMoveTo`), but `UpdateFollower`'s `SetSprinting(leader sprinting)` needs a standing follower, and by the user decision 2026-10-01 a follower never copies the leader's stance -> they crouch-walked (~50 % of his speed). Solo on / off cured it because the second Regroup copied the now standing Commander's stance. Not the cover / traffic-hold / group-cover-slot / own-order state (all cleared the same by both paths). **Fix:** `AOperativeCharacter::MarkStanceFromSquad` / `IsStanceFromSquad` / `ClearStanceFromSquad` (a stance-change serial: the stance the SQUAD gave him - Regroup's copy, Solo's guard crouch on entry and on a leader switch, a tutorial confinement's crouch copy in `FollowTo`; any later stance change, the stance keys and Alt + Z / C / V make it his own) and `SquadOrderRules::ShouldStandToSprintWithLeader` (leader sprinting + follower crouched + squad-given stance + able to sprint): `UpdateFollower` stands him up (the clip defers the move), then he copies the sprint. A crouch the player gave keeps him down (the 2026-10-01 rule). Console `Codex.Squad.SprintStandUp` 1 (0 = the old rule, A/B). Tests `CodexTactics.Squad.Orders.StandToSprintWithLeader`; `SoloRejoinSmoke` stages 117-122 (Solo, C stacked in cover at the wall, B selected and walked off crouched, Regroup with B leading, A selected and sprinting 15 m: with `SprintStandUp 0` the bug reproduces - B / C stay crouched at 47-51 % of his speed; with the fix both stand up and sprint at >= 90 % of his speed after 0.23 s; then the player's Alt + C crouch is kept when the leader sprints); `TutorialSmoke` stage 107 (after the A3 crouch confinement released, the leader sprints: both followers stand up and run, best 240-280 % while catching up).

## Group cover order: the others go along (game designer 2026-10-20)

- **Reverses the 2026-10-11 decision "Followers in cover: NO automatic stacking - cover is taught as manual orders per operative" for
  GROUP orders:** a cover order (the confirm click on the cover ghost) given while two or more operatives are box-selected, or in group
  mode (Solo off, exploration formation), takes the others into the same cover: the leader (the direct order) on the clicked slot, the
  others shoulder to shoulder on the nearest free slots of the same wall, side, who does not fit prone right behind it. Solo with one
  selected: only he (unchanged). Who joins: `SquadOrderRules::ShouldJoinGroupCover` (`USquadSubsystem::GetGroupCoverMembers`; group mode
  skips followers busy with an order of their own, guards, the ones aboard / held; a box selection includes every selected one). The
  orders are the usual own orders (`OrderTakeCover`, the prone walk = `OrderMoveTo`), so they survive selection changes; the group stays
  selected; formation following resumes when the leader walks away (the followers-in-cover rule, also for the prone ones) or on Regroup.
  Pause: planned; turn-based: only the active unit. Details: [cover-and-peek](cover-and-peek.md) "Group cover order".
- **In the fight too (user decision 2026-10-26, "group cover in combat: YES"):** group mode without a box selection was exploration-only
  (`USquadSubsystem::IsExplorationFormation`): in a fight a cover click was the selected operative's own order. Now
  `USquadSubsystem::IsGroupCoverFormation` (pure `SquadOrderRules::IsGroupCoverPhase`: the exploration formation, and in the fight wherever the
  formation follows - real time, the tactical pause, the preparation / rest; never turn-based, never under Commander Mode's autonomy) gates the
  whole group cover family: who joins (`GetGroupCoverMembers`), the formation wall layout (`ACodexTacticsPlayerController::OrderGroupCover`), the
  wall order no longer ending when a fight starts, the wall group (`ReallocateWallGroup` / its tick), the slide / corner-stack preview
  (`PreviewFormationMove`); the squad's corner peek already ran on `IsFormationActive`. **The tactical pause:** the wall layout is planned
  (`USquadSubsystem::PlanFormationWall`: the leader's walk to the clicked slot planned within the pause radius, the slot pending as the fallback,
  message "[PLAN] Along the wall (N operatives), facing out!") and ordered on the release (`ExecutePlannedFormationWall`, before the planned
  walks: `OrderFormationWall` with the followers then - members with a plan of their own in that pause keep it -, the wall group); a new plan for
  the leader in the same pause drops it; a slot beyond the radius falls back to the classic planned group cover. Solo with a single selection
  stays the individual cover click everywhere; a box selection works as before. Console `Codex.GroupCover.InCombat 0` = the old rule (A/B). Test
  `CodexTactics.Squad.Orders.CombatFormation.GroupCoverInTheFight`; smoke `GroupCoverSmoke` scenes WF (real time: the wall layout), WP (the
  pause: planned, ordered on the release), WS (Solo, single selection: he alone).

## Operative assets (old §5)

- Assets: `/Game/Characters/Operatives/BP_Operative`, `ABP_Operative`, `Explorer/…`, `Animations/…`, `/Game/Weapons/M16`.

## Per-member Blueprint and where to edit the squad (old §6)

- **Per-member Blueprint: how the user sets BP_Operative_FemaleSoldier (user request 2026-10-11).** Each roster entry
  (`ACodexTacticsGameMode::SquadRoster`, `FSquadMemberSpawn`) has `MemberBlueprint` (soft class). Set and loadable -> that
  member spawns from it and the roster's `BodyMesh` is NOT applied (the Blueprint owns mesh, materials, attachments such as
  the backpack); empty / missing -> `OperativeBlueprint` (BP_Operative) + `BodyMesh` as before (one log line «member Blueprint
  ... not found»). Role, fortitude, luck, accuracy, colour, `ExtraWeapons` (her sniper rifle), the balance config and the
  arsenal still come from the entry; save / load restores members by name / role onto the spawned squad, so a member keeps
  its class. Default: the Medic-Sapper -> `/Game/Characters/Operatives/BP_Operative_FemaleSoldier` (C++ default, no asset
  edit needed). Steps: (1) Content Browser `Content/Characters/Operatives` -> right-click **BP_Operative** -> *Create Child
  Blueprint Class* -> name it exactly **BP_Operative_FemaleSoldier**; (2) open it, select the inherited **Mesh** component ->
  Skeletal Mesh Asset = `SK_Female_soldier` (Anim Class stays ABP_Operative); (3) add the extra parts as Skeletal Mesh
  components attached to Mesh (set *Leader Pose Component* to Mesh in the Construction Script, or use the same skeleton) /
  Static Mesh components on a socket; (4) Compile + Save; PIE: the log shows «Medic-Sapper: member Blueprint
  BP_Operative_FemaleSoldier_C». Another name or another member: edit the roster in `BP_CodexTacticsGameMode` (next bullet, no code). Note 2026-10-11: the main folder holds an
  untracked `BP_FemleSoldier` (typo; child of BP_Operative with `SK_Female_Soldier_Backpack`) - it is NOT picked up until it is
  renamed to `BP_Operative_FemaleSoldier`. Checks: `CodexTactics.Core.SquadSpawn.*`, `SniperSmoke` stage 0 (class + body
  for both branches).

- **Where to edit the squad: `BP_CodexTacticsGameMode` (user decision 2026-10-14).** `/Game/Blueprints/BP_CodexTacticsGameMode`
  (child of `ACodexTacticsGameMode`, created by `Scripts/Editor/create_gamemode_blueprint.py`, idempotent) is the project
  default game mode (`Config/DefaultEngine.ini` `GlobalDefaultGameMode`). Open it -> **Class Defaults** -> category
  **CodexTactics|Squad** -> **Squad Roster** (rows are titled with the member name; order = selection keys 1..N). Per row: name,
  colour, spawn offset, fortitude, role, luck, accuracy, **Body Mesh**, **Member Blueprint**, **Extra Weapons**. All values are
  inherited from the C++ constructor until a row is changed (the script logs 3 rows on creation). **Give the Medic-Sapper (row 2)
  the regular operative back:** clear her *Member Blueprint* (BP_Operative_FemaleSoldier) AND her *Body Mesh*
  (SK_Female_soldier) -> she spawns from `OperativeBlueprint` (BP_Operative) with its mesh (`SquadSpawnRules::ChooseMemberClass`,
  test `Core.SquadSpawn.MemberBlueprintSelection`); clearing only one of them keeps the other. Keep *Extra Weapons* if she
  should keep the sniper rifle (it stays on slot 3 by user decision). Compile + Save, PIE. Trap: once the designer edits the
  roster array in the Blueprint, the whole array is stored there and later C++ default changes no longer reach it. Other game
  mode defaults (balance assets, `LevelJsonFile`, enemy classes) are editable in the same Class Defaults. No map overrides the
  game mode; code reads it through `GetAuthGameMode<ACodexTacticsGameMode>()` (works for the child).

## Formation look sectors (stage 6, 2026-10-25; design docs/design/FORMATION_SECTORS.md, contract FORMATION_CONTRACT.md)

Console `Codex.Formation.Sectors` (default 1; **0 = the old triangle formation, facing and speeds - every old path untouched**). `Codex.Formation.Debug 1`
logs every raw terrain class change with its probe hits, `2` also every follower repath (role, distance, slot speed, speed, backwards), `3` also each
member's formation look whenever it changes (with his facing) and every look ClearLook turns (2026-10-26).
- **What the player sees:** exploring in group mode the squad picks its formation by the ground round the leader - diamond (4) / wedge (3) / pair (2) in the
  open, echelon away from a wall on one side, column in any passage (narrow: P looks over the leader's shoulder, L back at a slant, the rear man back),
  the wall layout when the leader stops at a wall; the blizzard tight diamond in a blizzard. Each man looks into his sector: patrol (walk) = scans (leader
  11-1, P 12-3, L 9-12, the 4th walks BACKWARDS looking at 6), run (double click) = everyone his path, halt = the sector centres. The squad walks at
  `pace.patrol` (0.8) x walk speed. A group cover order in group mode = the wall layout: all along the wall 1 m apart, crouched, backs to it, facing OUT
  (nobody looks into the wall); the leader's next order ends it, the others stand up and follow. A box selection keeps the old orders (each to his point /
  the cover slots), Solo keeps everyone where he is.
- **Code:** `USquadSubsystem::UpdateFormationSectors` / `UpdateSectorFollower` / `OrderFormationWall` (`Private/Characters/SquadSubsystemFormation.cpp`);
  the pure glue `Formation/FormationRuntimeRules` (`CodexTactics.Formation.Runtime.*`): slot -> role (0 L, 1 P, 2 Z = the old 2nd-row-left becomes the rear
  man; 2 members: Z), the 9 probe rays at 120 cm (lower obstacles are looked over), hits -> thin walls read from the probe origin, a one-probe glitch
  debounced, the v2 tracker fed the leader's path and move destination (`StepFormationTerrain`: the ONE adapter - keep it thin), the wall layout only on
  arrival, contract 7 follower speed (slot velocity lead + FOLLOW_GAIN 2/s, capped by catchup / backwards 0.9, lead <= 120 cm, repath 0.1 s; crawling: no
  backward cap), walking into the wall layout = his path until 1 m from the slot. Facing: `AOperativeCharacter::SetFormationLookYaw` (set / cleared by the
  squad every tick; `UpdateCombatFacing`: combat target > facing order / quick fire > cover > an order's idle facing (group cover, warm spot) > formation
  sector > movement; not prone / sprinting / carrying / on the grid). A halted leader turns back to 12 for 1 s, then his facing is his own and the halted
  heading follows it (a teleport, a load or a facing order turns the squad as before). `SetFormationPaceMultiplier` (the leader's walk only),
  `SetFormationWallPose` (his next own order stands him up when the crouch was the squad's). The tutorial's `squad_not_in_cover_at` counts the wall pose as in cover.
- **Checks:** `FormationLabSmoke` (L_FormationLab, `-LevelJson=L_FormationLab.json`; `shot` = stills in `Saved/Screenshots/FormationLab/`); GroupCoverSmoke
  scene W. A/B: `smoke_parallel.ps1 -PreCmds "Codex.Formation.Sectors 0" -Commands ...`.
- **Open:** the hedgehog at a fire (`FormationShapeRules::HedgehogSlots`), the
  designer's back-to-the-wall clip; the 4th man of an echelon trails ~1.6 m after an echelon <-> diamond switch (walks backwards at 0.9 x walk; the
  simulator ~1 m) - a Training Ground tuning point.

### The squad hugging a wall: tight to the face everywhere (2026-10-27, branch formation/wall-hug-gap; the game designer's play-test of main 804d64c2)

Report (the Demo 01 lake yard, screenshots `hug_wall_ok` / `hug_wall_gap`): "at one wall it hugs, at the other it doesn't - and look at the distance between the squad and the wall". Two root causes, each with an A/B switch:
- **(a) the wall traces read a trigger box as the wall.** `ProbeFormationWalls`, `BuildFormationWall` (the face behind the clicked slot and its room each side) and `TraceLookWalls` traced by OBJECT TYPE (WorldStatic / WorldDynamic) - an object query also hits overlap-only boxes. The lake's `VOL_A7_YARD` (`ATutorialTriggerVolume`, OverlapAllDynamic) covers the whole compound yard: from inside it every trace hit it at distance 0 (log `probe ... raw corridor, [17 deg 0 cm] x 9`, `wall layout: 3 members, outward yaw 0, room left 0 / right 0 cm`). The layout's face was the clicked slot itself: every place 45 cm farther out, no room, the outward yaw from a zero vector (0) - at the building's west face (outward 180) the squad faced the wall and the followers' places lay in the face, so they never settled. Now `USquadSubsystem::TraceFormationWall`: a multi object trace, the nearest hit that BLOCKS pawns and is no pawn (pure `FormationRuntime::NearestWallHit`; `GatherFormationWalls` had that rule already). `Codex.Formation.WallTraceBlocking 0` = the old trace.
- **(b) the hand-span.** The layout's face offset `wall.face_offset` (45) is the solo cover stand-off (`FCoverTraceConfig::SlotOffsetCm`, test `FaceOffsetIsTheSoloCoverStandOff`), but the walk stopped short of the place - the leader's own move `AcceptanceRadius` 25 cm, a follower parks within the formation's stop radius 25 cm - while one operative in cover is put ON his slot (`AOperativeCharacter::EnterCover`). Now `USquadSubsystem::GlideToWallSlot`: a member settled in the wall pose and still glides (swept, 150 cm/s, pure `FormationRuntime::WallSlotGlide`) onto his exact place - followers within `WallSettleCm` 60, the leader within `WallLeaderSettleCm` 120. `Codex.Formation.WallSnap 0` = off.
- **Numbers** (`FormationWallHugSmoke`, the worst body centre off the face of 3; before -> (a) only -> (b) only -> both): the building's west face 69 (followers never in the pose) -> 70 -> 69 -> **45**; the 3.6 m passage to the compound wall 126 -> 81 -> 101 -> **46**; a 2 m passage (a spawned 1.2 m wall along the south face) 281 (a man past the low wall) -> 69 -> 270 -> **46**. Exploration and the real-time fight alike.
- **(c) the wall slide keeps its lead men clear of the leader** (found by `FormationCornerPeekSmoke` once the squad hugs exactly): the slide's places move with the leader's progress (`FormationWallRules::Slide`), so a lead man parked within the stop radius short of his place stood in the leader's way on the wall line; the crowd avoidance slowed the leader to a stop and the places stopped with him (`wall slide over after 6.3 s (491 cm off its end): the leader stopped`). `UpdateFormationSlide` asks the rule for the members AHEAD of the leader with him `FormationRuntime::SlideAheadClearCm` (40) further on (`SlideLeaderAhead` / `SlideTargetsClearOfLeader`, capped at his target - the final places unchanged; the pure rule and its TS parity untouched). Test `SlideLeadMenClearOfTheLeader`.
- **Checks:** tests `CodexTactics.Formation.Runtime.WallHug.*`; smoke `FormationWallHugSmoke` on `L_Demo01_A_FrozenLake` (+ `-LevelJson`; the tutorial skipped, checkpoints off, enemies parked; the yard checked by traces against the user's layout first): the group cover click (two `HandleWorldHit` clicks on the face) -> all three in the wall layout, crouched; each body centre <= 45 + 10 cm off the face; nobody behind it / at the passage's other wall; nobody bunched (>= 70 cm). `Shot` -> `Saved/Screenshots/Formation/wall_hug_fixed.png` (the compound passage from above).

### The squad's corner peek (2026-10-26, branch formation/v24-squad-peek; user bug report; behind `Codex.Formation.Sectors`)

**CONFIRMED by the user 2026-10-26:** at a high wall the peeker stands (his cover entry's stance) and the others stay crouched (the order's crouch).

User bug report 2026-10-26: one operative hugging a wall shows the peek silhouette when the cursor is on the wall's corner and peeks round it on
the click; the squad hugging it in formation ("Along the wall (3 operatives), facing out!") showed neither. **Root cause:** since stage 6
(b75e3ce, 2026-10-25) the group cover order in group mode is the formation wall layout (`OrderFormationWall`: the leader's `OrderMoveTo`, the
others the formation drive) - nobody is in a cover slot, and the corner peek, hover and click, was gated on the leader's own cover slot
(`IsInPeekableCover()` in `PlayerTick`, `UpdateCornerPeekHover`, `TryHandleCoverClick` / `FindCornerPeekForHit`). The v2.4 merges (6e85b95,
511c45f) did not take the hover / click: `UpdateSquadMoveHover` reads ground hits only.
- **Hover zones** (the squad in the wall layout, the leader in no cover): (1) the squad's wall within its peek window at a free end - the same
  window as the single peek (`CoverFacingRules::FindPeekEdge`: the face / the end face / the top within 75 cm inside the edge .. 60 cm past it) ->
  the peek ghost of the member nearest that end in the corner-aim pose, the crosshair; a click = the squad's corner peek; (2) the ground along the
  wall within `slide.max_off_wall` -> the dashed slide / corner-stack lines (unchanged); (3) the ground round the end -> the leader's path round it
  (unchanged); (4) the rest of the wall -> nothing on hover, the click = the cover ghost / group cover order (unchanged). The ghost lives on the
  wall body, the guide lines on the ground: they never compete (the guide lines are also off while the peek crosshair is up).
- **The order** (`USquadSubsystem::OrderFormationCornerPeek`, `SquadSubsystemFormationPeek.cpp`): who peeks = `FormationWallEnd::PlanSquadPeek`
  (pure, tests `CodexTactics.Formation.WallEnd.SquadPeek`): with the v2.4 corner stack at that end (`FindStackEnd`) the stack's point man - the
  place nearest the end, so with three K is second and the rear man looks back; without a stack (K already within `SlideMin` of the end, no
  world walls) the member nearest the end, the others hold. The peeker takes the corner stand point in cover (`OrderTakeCover`; the stance his
  cover entry gives him: high = standing, low = crouched - the ghost shows that) and, once in cover, gets the single operative's
  `OrderCornerPeek` (corner aim, leaning out, held with no target). The others: the leader's move to the end (`PeekDestinationOutCm` 60 off the
  face) = the existing wall slide into the corner stack (`TryStartFormationSlide` -> `HoldCornerStack`), the order's crouch kept; the peeker is
  left to his corner (no follow, no wall pose, no sector look; the covered approach counts him at his place). A leader who is the point man
  himself (a pair with K at the end) peeks and the stack forms round his corner. Over with the leader's next order (not the stack's own move),
  a leader switch, Solo or the end of the wall layout - the peeker then follows out of his cover as any follower in cover.
- **Checks:** `FormationSquadPeekSmoke` (both ends of the long wall; the hover through `UpdateCornerPeekHover`, the clicks through
  `HandleWorldHit`; `Shot` = `Saved/Screenshots/Formation/squad_peek_ghost.png` / `squad_peek_held.png`).

### Rules v2.6 in the runtime - short obstacles and the rear guard's turn round (2026-11-01, branch formation/rules-v26; behind `Codex.Formation.Sectors` + `Codex.Formation.ShortSideObstacles` / `Codex.Formation.RearTurn`)

The game designer's answers to the red team v2.5 questions (FORMATION_SECTORS.md "v2.6", contract 18). (1) A post or a short stub beside the path
is no side wall: `ProbeFormationWalls` drops a SIDE probe's hit (> 45 deg off the heading) on an obstacle shorter than `classify.side_min_length`
(150 cm) along the heading - `FormationTerrainRules::IsShortSideObstacleAt` over the level's box walls (the guard's world walls; touching boxes
within `classify.side_join_gap` 50 cm are one obstacle; a hit on complex collision stays) - so no echelon for it; the probe hits are marked
`FFormationWall::bProbeHit` (the pure `SideWalls` filter keeps them). (2) The walking rear guard more than `move.rear_turn_lag` (150 cm) behind his
slot along the heading turns round and catches up facing forwards (no formation look: he faces his movement; `FollowerMove`'s backward cap is
off once his body has turned) and walks backwards again within `move.rear_turn_back` (50 cm) - `FormationSectorRules::RearCatchUp` in
`UpdateSectorFollower` (`FFollowerState::bRearTurned`); a team-mate on his straight way to the slot delays the start (`RearWayBlocked`). Logs
`[Formation] <name> (rear guard) turns round to catch up facing forwards: N cm behind his slot` / `is back - walks backwards again`. Smoke
`FormationLabSmoke` runs `R_A_posts` (two 60 x 40 cm boxes spawned 3.2 m beside lane A: the diamond holds, the probes drop the hits) and `R_A_rear`
(the rear guard set 3 m back mid-walk: he turns within 1.5 s, faces his run, walks backwards again); the old known R_B_walls pace entry passes
(p95 103 cm, was 160-175). Tests `CodexTactics.Formation.V26.*`, `Parity.RearCatchUp`, `Parity.Classify` / `.PassageWidth` (the new scenes).

### The squad corner peek played twice (2026-11-01, branch fix/squad-corner-peek-anim; the game designer's play-test video `Saved/BugReport/1.11.26_bug_01.avi`)

Report: "when peeking round a corner the animation plays TWICE" - the peeker (the Medic-Sapper on L_FormationLab's W_B_2) made two blurred spins
at the corner, then raised the rifle. **Not two peek orders:** the designer's PIE log (`Saved/Logs/PIE_designer_2026-11-01_corner_peek.log`,
10:03:14-16) and `SquadCornerPeekSmoke` show ONE `OrderFormationCornerPeek`, ONE cover entry, ONE `OrderCornerPeek`, ONE step-out
(`cvr_std_idle_*_to_fire`), no facing flip. **Root cause:** the peeker walks ALONG the wall to his corner and his cover entry played the pack's
enter clip `std_idle_fwd_to_cvr_std_idle_*`, whose first frame FACES the wall (it is made for a head-on arrival): the actor snapped round to the
wall normal, the clip blended in over 0.15 s (spin 1: the body turned ~90 deg to face the wall), then the clip turned it 180 deg back out (spin
2), and only then (~1 s) the corner step-out - the peek. Measured on the body's shoulder line (the smoke, old entry): +310 deg one way, then
-119 back (west end); +145 / -139 / +183 (east end).
- **Fix:** `CoverFacingRules::DecideCoverEntryTurn` (pure, test `CodexTactics.Tactics.Cover.EntryTurnCornerPeekArrival`): a corner peek arrival
  (`AOperativeCharacter::MarkCornerPeekArrival`, set by `OrderFormationCornerPeek` right after the peeker's `OrderTakeCover`; cleared by the next
  cover order, consumed by `EnterCover` at that slot) more than `FCoverFacingConfig::EnterClipMaxOffWallDeg` (60, the run-in's own limit) off
  facing the wall gets no enter clip (`IsCoverEntryEasedTurn` -> `UOperativeAnimInstance::UpdateCoverLayer` skips it as for a quiet entry): the
  walk's yaw is held on the mesh and eased out over `CornerPeekEntryYawBlendSeconds` 0.9 s (~ the step-out clip; 0.3 s let the body swing out
  to the wall normal and back, a 50 deg wobble) while the step-out plays at once - one turn from his walk into the peek, the rifle on the peek
  side's fire idle. Every other cover entry keeps the enter clip (head-on peek arrivals too). `Codex.Cover.CornerPeekEntryTurn 0` = the old
  entry (A/B). Log `[Cover] X: arrives at the corner peek's slot 90 deg off facing the wall - no wall-facing enter clip, ...`.
- **Checks:** `SquadCornerPeekSmoke` (L_FormationLab, `-LevelJson=L_FormationLab.json`; the lab wall W_B_3, both ends; the group cover clicks, the
  hover + click on the end through the player's path): one cover entry, one peek order / reached, no enter clip, ONE step-out montage start, no
  exit / stance / second entry one-shot, no facing flip, the fire idle of the peek side, ONE body turn (`turns 1`). With the switch 0 the
  enter-clip and turn checks fail (the A/B). `Shot` (rendered) = the frame strip `Saved/Screenshots/CornerPeekFix/peek_<fixed|old>_end<n>_<i>.png`.
- **Open:** any OTHER cover entry walked in along the wall (not a peek) still plays the wall-facing enter clip (the same double turn); widening the
  rule to every entry is one switch in `DecideCoverEntryTurn` - for the designer to decide.

**The second peek at the other end never happened (same day, L_FormationLab W_A_1 by `Z_A_end`).** The squad peeked at the right end (it
stacked there), then the player ordered the peek at the LEFT end of the same wall: the squad slid over, the end man reached the corner and stood
there ("Hugging the wall, holding the corner!", barely leaning out) - no peek. Designer's PIE log 11:21:09 frames 665-666: `squad corner peek at
(4480, -5401)`, the slide to it one frame later, `squad corner peek over after 0.0 s (Medic-Sapper, the peek still due): the leader's next order`
(also 11:19:19 and 11:19:47 on the east face). **Root cause:** `UpdateFormationCornerPeek` waited for "a" corner stack (`bCornerStack` or a
stack slide) to adopt the leader's move into it - the FIRST peek's stack at the other end was still held, so the frame after the order (the
leader's move still waiting for his stand-up) took that old stack for the new one and stopped waiting; the slide to the new end then gave the
leader a new move order and the peek read it as "the leader's next order". Only when the click lands inside the world tick (the player's click
in PlayerTick: the squad updates in the same frame, before the slide is decided) - the smoke reproduces it that way (its end clicks run on
`OnWorldPreActorTick`; from the core ticker the squad saw the slide first and the bug hid). **Fix:** the peek stores its stack end
(`FSquadPeek::StackEnd` = the plan's) and adopts only a held / sliding stack AT that end (`FormationWallEnd::IsSameStackEnd`: within
`SameStackEndCm` 60, approached the same way; test `CodexTactics.Formation.WallEnd.SameStackEnd`). Logs `[Formation] squad corner peek: the
corner stack at its end (x, y) is on (sliding there / held) - ...` and, when a peek ends, the serials and stack flags. Smoke: `SquadCornerPeekSmoke`
orders 1a / 1b (west, then east straight from the west stack) and 2a / 2b (the reverse); each order: one step-out montage start, by the man at
the ORDERED end, round THAT corner. With the old rule orders 1b / 2b fail (`Saved/Logs/SquadCornerPeek_bug2_repro_old_stack_rule.log`).

### The squad's corner peek moves on to the next wall (2026-11-01, branch fix/peek-next-wall-camera; the game designer's play-test, bug_04 A)

Report (L_FormationLabCombat, `Saved/BugReport/2026-11-01_bug_04_peek_next_wall_camera/6-8.webp` + `PIE_designer_2026-11-01_bug04.log`): the squad
peeking round the end of one of two parallel walls (W_D_9), the double-click on the OTHER wall's end (W_D_10, the cyan cover ghost "corner to fire
from") - the squad only walked to that wall, the member who had peeked stayed in his corner, the other two crouched in a broken shape, no peek.
- **Root cause 1 (the old peeker stayed):** the double-click is a group cover order = the formation wall layout (`USquadSubsystem::OrderFormationWall`;
  log 15:03:37 `wall order: Engineer -> Z / Medic-Sapper -> P`, `squad corner peek over ... the leader's next order`). The peek's end handed its
  peeker back to the followers-in-cover rule (`SquadOrderRules::ShouldLeaveCoverToFollow`: out only while the leader walks AND his slot is > leave
  distance); on the wall route (`wall route: a wall between Engineer (Z) Medic-Sapper (P)`) his trail place lay beside his corner and the leader
  then stood at his slot - Medic-Sapper never logged "stands up" / "at the wall" (15:03:37-15:03:51, only the leader's next move freed him; the
  15:04:55 order the same for the Engineer). **Fix:** `OrderFormationWall` ends a peek in force itself and takes every member it names out of his
  cover / peek corner / group-cover prone spot / walk to a cover slot first (`LeaveCoverForSquad`, the leave half of `HoldsCoverOrLeavesIt`; log
  `[Formation] wall order: X leaves his peek corner for his wall slot (the squad's order)`, counter `GetFormationWallReleaseCount`).
- **Root cause 2 (no peek):** a wall order is only the wall layout; the peek at the new wall needed a second click on its end (15:05:03 - and
  with the leader's slot at that end, `PlanSquadPeek` gave "the leader takes the corner, the others hold": one of them in the wall END layout's
  place round the corner, in front of the peeker). **Fix:** the corner peek is carried over (`Codex.Formation.PeekCarry`, pure
  `FormationPeekCarry`, tests `CodexTactics.Formation.PeekCarry.*`): a group wall order given while the squad peeks, at a slot with a free corner
  (`FCoverSlot::HasExposedEdge`), remembers that corner (`StartSquadPeekCarry`: the nearer exposed edge, an aim point 25 cm in from it); once
  the squad stands at the new wall (the covered approach over AND every member settled at his wall slot - `AreFormationWallMembersSettled`; the
  leader still 0.4 s; a straggler: the leader still 8 s; dropped by the leader's next order / a leader switch / the order's end / 40 s) the
  controller orders the squad's corner peek there exactly as the player's click on that end (`TickSquadPeekCarry` -> `FindSquadCornerPeekForHit`
  -> `OrderFormationCornerPeek`). And `FormationWallEnd::PlanSquadPeek`: the leader already within `SlideMin` of the end = the stack where he
  stands - K the point man, the others in a line behind him along the face in the order they stood, the first 30 cm farther back (`AtEndGapCm`: the leader at his corner cover stands ~20 cm behind his stack place; at 84 cm a man walking in past him stuck on his body) (was: the others hold) - so the shape is
  the first peek's either way: the peeker at the corner, the stack behind.
- **Checks:** `SquadPeekNextWallSmoke` (L_FormationLab, W_D_9 / W_D_10; every click in the world tick): variant 1 the west ends (the click 80 cm
  from the end: P peeks, K second, Z back), variant 2 the east ends (40 cm: the leader at the end peeks, Z and P behind) - the old peeker out of
  his corner within ~0.5-0.7 s, one carried peek, the peek pose held, the others at W_D_10 behind him, nobody left at W_D_9. A/B
  `-PreCmds "Codex.Formation.PeekCarry 0"`. Stills `Saved/Screenshots/Bug04/` (`... SquadPeekNextWallSmoke Shot`, rendered).

### Wall leadership (2026-11-01, branch formation/wall-leadership; the game designer's decisions; behind `Codex.Formation.Sectors` + `Codex.Formation.WallLeadership`)

Design and the seven decisions: `docs/design/FORMATION_SECTORS.md` "Wall leadership". The squad's corner peek (the click on an end of the squad's wall,
the carried peek of the next wall) no longer leaves the leader where he is: the member nearest that corner along the wall peeks AND becomes the
formation leader; all the others stack behind him along the near face.
- **Code:** pure `Formation/FormationWallLeadershipRules` (`PickCornerLeader`, `StackOrder`, `SwapRoles`, `StackLine`, `Plan`, `HoldBehind`,
  `AheadInLine`; tests `CodexTactics.Formation.WallLeadership.*`); runtime `Private/Characters/SquadSubsystemWallLeadership.cpp`:
  `PlanWallLeadership` (the formation's members, able = `CanTakeWallLead`), `OrderWallLeadershipPeek` (ends the old peek, `SetLeader` under
  `bWallLeadershipSwitch` - selection, ring, portrait, camera follow as any leader change, but no `PassWallOrderToLeader` re-layout round him; the
  wall order's roles re-keyed (`SwapRoles`); every follower in cover / a peek corner / a prone spot / a cover walk released (`LeaveCoverForSquad` -
  the old leader at the other corner held it by the followers-in-cover rule while the new leader takes cover); the new leader's `OrderTakeCover` on
  the corner slot + `MarkCornerPeekArrival`; `HoldCornerStack` with the plan's stack; the peek state `FSquadPeek::bLeadership` / `Line` /
  `LineAble`), `HoldBehindInWallLine` (called from `UpdateSectorFollower`'s wall branch instead of the bug-D step round). `PlanFormationCornerPeek` /
  `OrderFormationCornerPeek` route to it while the CVar is on. The controller (`CodexTacticsPlayerControllerCover.cpp`, `PlayerTick`): the leader
  holding the squad peek's corner (`IsWallLeadershipPeeker`) keeps the squad's peek hover / click for its wall (the other corner), not his single peek; and his double-click on ANOTHER cover is the
  squad's wall order there (the formation wall layout + the carried peek of bug_04 A), not his own wall-edge cover switch with the others' legacy
  group cover (found by `SquadPeekNextWallSmoke`: with the peeker now the leader in his corner cover, the double-click dashed him alone).
- **Log:** `[Formation] wall leadership: the corner peek at (x, y) - B takes the corner and the lead from A (the corner of the world walls at ...);
  behind him: ...`, `[Formation] wall leadership: X leaves his peek corner for his place behind B`.
- **Leaving the wall (decision 6):** nothing extra - the switch is a real `SetLeader`, and the next formation's places are dealt by travel from the
  stack (contract 10.6): the man right behind the leader becomes P, the stack's last man the rear guard Z (the smoke checks it).
- **Checks:** `WallLeadershipSmoke` (L_FormationLab W_B_3; see build-test-smokes.md); `FormationSquadPeekSmoke` now expects the peeker to BE the
  leader (with the CVar 0 the old "leader second"); A/B `-PreCmds "Codex.Formation.WallLeadership 0"`.
- **Open:** interpretations (a)-(e) in FORMATION_SECTORS.md "Wall leadership".

### Rules v2.5 in the runtime - the covered approach to the wall (2026-10-26, branch formation/v24-wall-end; behind `Codex.Formation.Sectors` + `Codex.Formation.Approach`)

New user rule 2026-10-26: on the order to hug a wall (the group cover order in group mode, or the arrival layout) everyone used to drop his sector
and dash in, the rear wide open. Now `USquadSubsystem::ApplyFormationApproach` runs `FormationWallRules::Approach` (contract 15) every frame of the
wall layout until everyone is within `approach.arrive_dist` (1 m) of his slot: the leader runs in and, at his slot, looks along the wall; the follower
already looking back (the patrol's rear guard) covers the rear (away from the wall) walking backwards at the walk's backward cap; the other(s) look along
the wall line to their side and take their layout look at their slot; then the plain wall layout. The rear cover and the flank men do not copy the
leader's sprint (a sprinting operative faces his run) - the squad settles ~2 s later from 9 m, the price of the covered rear. Roles held (no reshuffle).
Log `[Formation] covered approach to the wall: K runs in, rear Z, 1 flank(s)` / `... over`. `Codex.Formation.Approach 0` = v2.4 (A/B). Smoke
`FormationWallGroupSmoke` scene 5 (a patrol 9 m from E's long face; with the switch 0 the rear / flank / along checks fail); tests
`CodexTactics.Formation.Approach.*`, `Parity.Approach`.

### Rules v2.4 in the runtime - the wall end (2026-10-26, branch formation/v24-wall-end; behind `Codex.Formation.Sectors` + `Codex.Formation.WallEnd`)

User bug report 2026-10-26 (the editor, `Codex.Formation.Debug`): three operatives crouched at the END FACE of a thin free-standing wall left large
uncovered zones on both sides of the end, along the wall's faces. The wall layout's looks (L 8 / K 6 / P 4) share a FACE's half-plane; at an end the
open arc reaches round it. Now every wall layout (`ComputeWallLayout`: the arrival and the group cover order; `HoldWallLayoutAt`: after a slide) goes
through `USquadSubsystem::ApplyFormationWallEnd` -> `FormationWallRules::EndLooks` (contract 14): with the looks holding < `wall_end.cover_pct` (95 %) of
the open ring 6 m round the layout (`OpenArcCoverage`; the world's box walls round it + the traced face measured out past the ring), K keeps 6 o'clock
and the others are spread over the open arc (at an end face: the two men beside the end ~118 deg off the axis, back along their faces). Once per layout:
no flicker, no reshuffle. Log `[Formation] wall END: the looks spread over the open arc (62 % -> 98 % held, ...)`. `Codex.Formation.WallEnd 0` = the face
looks (A/B). Smoke `FormationWallGroupSmoke` scene 4 (a thin wall's end face by the group cover order: 63 % held with the switch 0, 98 % on); tests
`CodexTactics.Formation.WallEnd.*`, `Parity.WallEnd`.
### Rules v2.4 at a wall's end (2026-10-26, branch formation/v24-corner-peek; the game designer's bug report, FORMATION_SECTORS.md v2.4)

Runtime fixes behind `Codex.Formation.Sectors` (the world-wall parts also behind `Codex.Formation.V21`); pure glue `Formation/FormationWallEndRules`
(`CodexTactics.Formation.WallEnd.*`), no TS / golden change.
- **A - the corner stack:** `TryStartFormationSlide` asks `FormationWallEnd::FindStackEnd` (the world walls: the squad's wall parallel to the layout,
  K's place over its span; the end in the travel direction is free - no other wall within the thicknesses + `CORNER_JOINT`; the destination from
  `StackReachCm` 125 before to `StackPastCm` 200 past it, on the squad's side, within `slide.max_off_wall`) -> the slide's destination becomes K's
  place of the stack (`StackShift`: the leading place `EdgeInsetCm` 40 short of the end; already there -> the stack in place). The slide's stop ->
  `HoldCornerStack` (a wall order: `StackSlots` - the point man `CornerPeekYaw`, the corner watch's look at the corner; the trailing end back along
  the wall 20 deg out; K the normal; a 4th the normal turned `wall.neighbour_deg` to the rear), crouched after the group cover order. Evidence
  before the fix (FormationCornerPeekSmoke log): `the leader's order from the wall layout is no slide (-720 cm along, room 650 / 500 cm)` -> the
  wall order ended, a column / echelon_left walked up, the leader stood at the edge looking along the wall, nobody peeked.
- **B - the fitted layout:** `ComputeWallLayout` (the group cover order, the automatic layout) and `HoldWallLayoutAt` (after a slide, a leader switch)
  run `FitWallLayoutToWorld`: `FormationWallEnd::FitLayout` with "usable" = no world wall body within 10 cm of the line from K's place, 30 cm off
  every wall body, on the nav mesh within 30 cm; a cut place moves to the next free step on K's other side (log `[Formation] wall layout fitted:`).
  Evidence (FormationWallSwitchSmoke, a T junction): before, K's slot next to the joint put a place inside / behind the long wall - 206 / 182 cm
  stand-ins round the leader's CURRENT position while he still ran round, a member cut off 1.6 s; after, all four variants settle on their places,
  0 cut off. The exact split of the user's screenshot (K alone on the far side) was not reproduced; the mechanism (layout places across the
  junction wall + stand-ins round the running leader) is what the fit removes.
- **C - the squad's guide lines:** `USquadSubsystem::PreviewFormationMove` (group mode, the wall layout standing, the leader not in a cover slot, the
  point within `slide.max_off_wall` of the squad's wall): a slide / the stack -> one line per member to his place; else the leader's nav-mesh path
  (it wraps the wall's end). `ACodexTacticsPlayerController::UpdateSquadMoveHover` (PlayerTick's cursor hit; cached while cursor and leader rest),
  `GetSquadMovePreviewPaths`; `ACodexTacticsHUD::DrawSquadMovePreview` (the wrap preview's dashes via `DrawDashedGroundPath`).
- **D - a leader switch at the wall:** `SetLeader` sets `Sectors.LastLeaderSerial` to the new leader's serial (the switch was read as his new order:
  a phantom slide / bypass to his last formation-follow destination - in the log `wall slide 2 ... 100 cm` right at a switch, the new leader
  "walking" at 3 cm/s into a team-mate), ends a bypass and calls `PassWallOrderToLeader`: a wall order / slide goes on with him (the layout round
  him, the crouch kept, `AssignWallRolesByTravel`). `UpdateSectorFollower`: walking into the wall layout, `FormationWallEnd::StepRound` steps round a
  team-mate standing in the line (out 110 cm, 55 cm past him). Smoke `FormationLeaderSwapSlideSmoke` uses the player's own clicks
  (`HandleWorldHit`: the wall twice, a member, the ground).

### Rules v2.3 in the runtime (2026-10-26, branch formation/runtime-v23; behind `Codex.Formation.Sectors` and `Codex.Formation.V21`)

The red-team fixes of FORMATION_CONTRACT.md 13 (pure rules ported in 61990b7) wired into the runtime; `Codex.Formation.V21 0` = the v2.2 runtime without them.
- **Fallback stand-ins keep their distance (13.5):** an unwalkable slot's stand-in (`ResolveReachableSlot` -> `SquadFormation::ChooseFallbackSlot`) keeps
  `slot.min_spacing` (80 cm) from the leader, from EVERY walkable formation slot of this frame (`SeedFrameGivenSlots`, before the follow loop - "and from
  the walkable slots", contract 13.5) and from the stand-ins given before it (`FrameGivenSlots`) - no two men are sent to one point, whatever the follower
  order. What a follower really walks to this frame (his slot or its stand-in) is `USquadSubsystem::GetFormationGivenSlot`. `Codex.Formation.Debug 2`
  logs every stand-in (`[Formation] fallback: slot ... unwalkable -> ..., candidate i of n usable; given ...`).
  *FormationLabSmoke R_v2_gaps (2026-10-26):* the echelon right walks 3.6 m from the lab floor's edge; Z's slot (5.3 m out on the right diagonal) is off
  the navmesh for most of the zone. v2.2 gave him the nearest candidate - his own bearing at 60 % - 50 cm from P's slot: Z walked on P's spot, and
  the smoke (measuring the RAW slot, ~2.2 m behind that stand-in) passed while Z, facing back as the rear guard, walked ~4 m forward to it
  BACKWARDS at the 0.9 x walk cap (only ~22 cm/s over the patrol pace; 389 -> 246 cm behind the slot he was given). The first v2.3 port counted only
  the slots resolved before him: with Z resolved after P the next candidate (the leader's back diagonal) was taken and the raw-slot measure failed
  (p95 212 cm) though Z kept ~10 cm off his stand-in; with Z resolved before P he still took P's spot and lagged (p95 285 cm against his given slot).
  Now: the walkable slots are given out first (Z always gets the back diagonal, no forward catch-up) and the pace check measures the GIVEN slot, as
  the bot's check 6 does (checks.ts `z.slot` = the frame's slot after the fallback); the gate stays 150 cm (R_v2_gaps p95 13 cm).
- **ClearLook's held look (13.1):** each member's last ClearLook result (`USquadSubsystem::ClearHeld`, `FormationGuardRuntime::RememberClearLook`) is the
  `Held` input of his next frame's ClearLook (hysteresis: a wall at the reach's edge no longer flips the rear man's look 145 deg). Reset when ClearLook does
  not apply to him (the wall layout, a run, the corner watcher, the leader looking where he walks) and dropped after any frame his look is not computed
  (`AgeClearHeld` each tick: a fight, an own order, the console off). `Codex.Formation.Debug 3` shows the held look on every ClearLook turn.
- **The trail column with walls (13.2):** `BuildTrailFrameSlots` = `FormationGuardRuntime::TrailFrameSlots` (pure): `TrailSlots` with the world's walls -
  the herringbone's 60 cm stagger shrinks in 5 cm steps until each slot keeps `column.stagger_clear` (45 cm) off every wall surface, so no column slot
  lands in a wall's clearance in a narrow passage / oblique gap (it would become an unwalkable slot and a stand-in round the leader).
- **The narrow column's decided looks (13.3, user decision 1A):** `AllocateFormationLooks` passes `FormationGuardRuntime::DecidedFixedLooks` to the
  coverage allocator as FIXED looks - in a narrow column (the classifier's held corridor width < `column.narrow_width`, also as the trail column) with
  3+ members K, P and L keep their LookYaw looks (P forward over the leader's shoulder), after the own-facing / combat / corner-watcher looks; Z stays the
  allocator's rear guard.
- **HUD: the turn fold says "(turn)"** (user decision 2026-10-26): `Echelon right -> Column (turn)` while the trail column is the sharp-turn fold,
  `(wall)` for a wall cut / the mirror (`FormationGuardRuntime::GuardHudText(..., bTurnFold)`, `IsTurnFold`: the turn ahead decides first, as in
  `FormationGuardRules::Resolve`; a held column keeps its reason). labels.ts follows in the simulator session.
- **Smokes with `-PreCmds` start on the neutral environment:** `UEnvironmentSubsystem` took only `-ExecCmds=CodexTactics.*` as a dev check, so
  `smoke_parallel -PreCmds "Codex.Formation.Sectors 0"` (ExecCmds starting with the switch) ran on a random procedural weather / hour (a blizzard
  changes the formation); now any `CodexTactics.` command in -ExecCmds counts (Clear, 15:00, seed 1).
- Tests `CodexTactics.Formation.V23Runtime.*` (ClearHeldMemory, ClearHeldKeepsTheRearMansLook, TrailFrameSlotsWithWalls, DecidedLooksAreFixed,
  FallbackStandInAtTheFloorEdge, GuardHudTurnFold).
- Not here (rules from the simulator session, v2.3 part 7+): a non-rear member whose share is the rear looking back over the shoulder (<= ~110 deg,
  walking forwards), ClearLook anticipating the motion; no tuning for the manual line's rear man (FormationLabSmoke reports p95 ~500 cm, not gated).

- **Rules v2.3 parts 6-9 (2026-10-26, formation/cpp-rules; contract 13.6-13.9):**
  - *A man cut off brings the trail column back (13.6):* `StepFormationGuard` passes `FormationGuardRuntime::AllInSight` (every formation follower -
    a sector role, not on his own order, not in cover - has a clear line to the leader through the world's walls, radius 0) as `Resolve`'s
    `bAllInSight`; false -> the trail column at once, released by the usual hold. The guard log adds ", a member cut off".
  - *Only the rear guard walks backwards (13.7, user Q3 = c):* the trail column's places carry their travel (`Sectors.TrailTravel`, the trail's
    direction at each slot, from `TrailFrameSlots`) into `FormationCoverRules::Allocate`; the allocator's `Shoulder` list (`Sectors.ShoulderLooks`)
    gives those members ClearLook's `Limit` (`FormationGuardRuntime::ShoulderLimit`: their velocity direction when moving over 15 cm/s and within
    90 deg of the entry, else the entry's travel; half `look.over_shoulder_max_deg` 110).
  - *ClearLook anticipates the motion (13.8, user Q4 = b):* `Ahead` = the member's position + his own velocity x `look.anticipate_s` (0.5 s;
    `FormationGuardRuntime::AnticipatedPos`, none below 1 cm/s).
  - *The HUD's "(turn)" (13.9):* `GetFormationHudText` / the guard log take the guard state's `Cause` (`GuardHudText(..., Cause)`); the runtime's own
    turn-fold flag (`bGuardTurnFold` / `IsTurnFold`) is gone - part 9 also makes a wall cut during a sharp turn "(wall)", where IsTurnFold said
    "(turn)". 13.5: an unresolvable slot (no stand-in at all) now counts the man's own position as given (sim `resolveFrameSlots`).
  All behind the existing switches (`Codex.Formation.V21` for the guard / ClearLook parts, the cover allocator's switch for the shoulder list):
  with them 0 nothing changes.

### Rules v2.2 in the runtime (2026-10-25, branch formation/runtime-v22; behind `Codex.Formation.Sectors`)

User decisions of FORMATION_SECTORS.md v2.2 (contract 11). With the console 0 every path below is off (the old formation / cover facing).
- **The triad K, P, Z:** `FormationRuntime::RoleForFollowerSlot(·, 3)` = slot 0 -> Z (the rear man straight behind, he walks backwards), slot 1 -> P (right).
  Slot 0 was the old LEFT slot, so the man who walked left goes to the centre-rear and the right man stays right (the least travel from the old
  triangle); the automatic 3-man wall layout puts Z at the left end - the same man. **Places, not fixed slots** (contract 10.6 / 11.6):
  `USquadSubsystem::ReassignFormationPlaces` re-assigns the follower slots (= roles) by minimal travel (`FormationRuntime::ReassignPlaces`,
  `AssignByTravel`) whenever that saves `ReassignGainCm` 100 cm (cooldown `FSquadFormationConfig::SlotSwapCooldown`; not in the wall layout / slide,
  nobody busy) - after a turn the man nearest the rear place becomes the rear man; it replaces the old left / right `UpdateSlotSwap` under the console.
- **Patrol / halt looks through the coverage allocator:** walking or halted with 3+ members (`FormationRuntime::UsesCoverAllocator`: every formation but
  the wall layout and the hedgehog; the pair and a run keep `FormationSectorRules::LookYaw`) `USquadSubsystem::AllocateFormationLooks` feeds
  `FormationCoverRules::Allocate` the places (K first, `PlaceBack` = how far back along the heading), the leader's probe walls + look probes round every
  follower slot (8 rays x `look.wall_dist` at 120 cm, `Sectors.LookWalls`) and the FIXED looks (a member's own facing order / quick fire / combat target):
  the rear guard (the place farthest back) holds 5-7 o'clock (shifted 30 deg to the open side by a wall on one side), K his search 11-1, the others the
  open-arc shares. The narrow column's P +15 / L -165 table is gone on the move (the allocator gives the corridor's open front to K and the rear to the
  rear guard; P / L double the nearer share). `Codex.Formation.CoverAllocator 0` = the v2.1 tables (A/B). A rear guard catching up sideways after a
  formation switch eases his look inside his rear band (`FormationRuntime::RearGuardCatchUpLook`: 180 +- (fov / 2 - 30), 15 deg under the backward
  angle) so he moves at the catch-up speed, not the backward cap (FormationLabSmoke R_E_corner p95 164 -> 101 cm; a straight-forward catch-up stays
  backwards).
- **The wall group (the user's playtest bug: after a group cover switch the followers looked the same way as the leader):** ONE squad call
  `USquadSubsystem::ReallocateWallGroup(Leader, Members, Why)` (`SquadSubsystemWallGroup.cpp`), fired from every group-cover path - the group cover order
  (`OrderGroupCover`: the formation wall layout, whose Layout looks already are the user's rule; the cover switch / the wrap / the slot path), the leader's
  switch / wrap arrival and any member's (re-)entry (`AOperativeCharacter::EnterCover` -> `NotifyWallGroupCoverEntered`; `ReleaseGroupCoverWait`, the prone
  fallbacks -> `ReallocateWallGroupForMember`). Group mode only (no Solo / box selection, the exploration formation). Once the leader is in his slot,
  `FormationWallGroup::Allocate` (pure, `Formation/FormationWallGroupRules`, `CodexTactics.Formation.WallGroup.*`) runs `FormationCoverRules::AllocateStatic`
  with the leader's look FIXED (`GetSightFacing`: his cover facing / corner aim stays his) over the walls probed round every member (16 rays at 50 cm), then
  holds each follower's look: in his cover slot the cover side along the wall (`AOperativeCharacter::SetCoverLookSide`, above the threat side and the
  corner hold, below a manual peek), or - a look within 45 deg of the outward normal ("away from the wall"), or along the wall into an inner corner - out
  of the slot into the back-to-wall pose facing that look (`LeaveCoverForWallPose`: quiet leave, the squad's crouch; held by `HoldsWallGroupPose`); prone
  members lie facing their look. A snapped cover look becomes fixed and the others are allocated again (no second rear man). Re-runs every 0.5 s and on
  every event; ends with the leader's next order, the leader out of cover > 1 s, Solo, a fight, the console 0 (cover sides cleared; the back-to-wall men
  stand up and follow as after the wall order).
- **The wall slide (user-approved):** from the wall layout (the group cover order's or the automatic one) the leader's next WALK order whose destination
  lies at the same wall (`FormationWallRules::Slide`: <= `slide.max_off_wall` 200 cm off it, on the squad's side, inside its measured length) side-steps
  the whole line: `TryStartFormationSlide` measures the wall along far enough (`BuildFormationWall` room up to `SlideRoomCm`), re-orders the leader to
  his slot moved along the wall (his offset kept) at the patrol pace, stands the crouched members up; per frame `UpdateFormationSlide` gives every place
  the slide's target (the start layout moved by the leader's progress - the order kept, nobody passes, nobody goes round) and look (the leading end
  ahead along the wall 20 deg out, K out with his search, the trailing end back = the rear guard). The leader's stop -> `HoldWallLayoutAt`: the wall
  layout again at the new place (the 4th / the pair's partner keeps his end when it has room, `KeepEndAfterSlide`), crouched again after an order's
  slide; the next order ends it or slides again. A run (double click) along the wall is the normal formation. A new order's decision waits for his
  stand-up clip (`LeaderOrderDecideMaxSeconds` 2 s) - its destination is not known before.
- **Checks:** `FormationWallGroupSmoke` (the corner switch / the short wall with one prone / the wrap: the leader keeps his look, no open gap over
  `coverage.max_gap_deg`, nobody faces a wall, not all the same way), `FormationSlideSmoke` (5 m right and 7 m left along a 17 m wall), FormationLabSmoke
  (the narrow column by the allocator: the rear guard 5-7, the leader 11-1).

### Rules v2.1 in the runtime (2026-10-26, branch formation/runtime-v21; behind `Codex.Formation.Sectors` and `Codex.Formation.V21`)

User decisions of FORMATION_SECTORS.md v2.1 / v2.1.1 (contract 10): the squad never splits round a wall, the bypass side "B", the corner check,
the sharp-turn fold "5Б", nobody looks into a wall "6А". `Codex.Formation.V21 0` = the v2.2 runtime without any of it (A/B); `Codex.Formation.Sectors 0`
= the old formation, untouched. Code: `Private/Characters/SquadSubsystemFormationGuard.cpp` (walls, trail, guard, trail column, corner watch,
ClearLook, HUD text), `SquadSubsystemFormationBypass.cpp` (bypass), the pure glue `Formation/FormationGuardRuntimeRules`
(`CodexTactics.Formation.GuardRuntime.*`); the pipeline stays in `USquadSubsystem::UpdateFormationSectors`.
- **What the player sees:** whatever formation is asked for (the terrain's, or a manual one), nobody is ever sent to the far side of a wall: an
  echelon whose men would stand behind a wall takes its mirror ("Echelon left -> Echelon right (wall)"), anything else folds into a column that
  walks the leader's own path - it bends round a corner and wraps a wall end ("Line -> Column (wall)"); the HUD's squad panel shows the line
  `Formation: ...` (amber while the guard falls back; English, `labels.ts guardHudText`). The request comes back by itself 5 m of path after it is
  whole again. Before a sharp turn (over 25 deg within 8 m of the leader's path) an echelon / line folds into that column too and opens after it.
  Clicking a point behind a free-standing wall: the squad passes it on the shortest side - or, with an enemy seen / heard (or a reported threat)
  within 20 s, on the side where the wall stands between it and the threat if that is at most 30 % longer. Passing a free wall end on patrol,
  the man closest to it looks round the corner (slicing the pie; also the leading man of a wall slide). Nobody looks into a wall closer than
  2.6 m, in manual formations too.
- **The level as walls:** the v2.1 rules read FFormationWall, not traces: `USquadSubsystem::GatherFormationWalls` overlaps a thin slab at the probe
  height (120 cm over the leader's feet) and turns every upright BOX of the blocking world collision into walls (`FormationGuardRuntime::WallsFromBox`:
  `UBoxComponent`s and the simple box collision of static meshes / instances, e.g. the lab's engine cubes; up to 3 m thick = one wall along the
  long axis in the layout convention, bulkier = its four sides as thin walls, so their corners are joints; a box must reach 15 cm above the probe
  plane - lower ones are looked over). Gathered 16 m round the leader every 0.25 s / 3 m (`Sectors.WorldWalls`). Complex-collision meshes are not read
  (the trace probes still feed the terrain classifier and the allocator).
- **Wall guard + trail column:** per frame `StepFormationGuard`: `FormationGuardRules::Resolve(WorldWalls, Requested, n, leader, heading, Ahead,
  TurnAhead, state, the leader's path this tick)` - Ahead = his pose `guard.lookahead` (500 cm) further along his path (`LeaderPathAhead`: his
  path following's points from the piece he walks on, `FormationGuardRuntime::PathAheadFrom`, then the bypass's waypoints and his destination).
  Level 2 -> `BuildTrailFrameSlots`: `FormationShapeRules::TrailSlots` on the leader's RECORDED path (`Sectors.Trail`, a point every 25 cm, 15 m
  kept, a teleport restarts it), the column's sectors per man by his room (`SideWidth`), the allocator's `back` = the column order. The requested
  formation is `GetFormationRequested()` (the menu's manual choice, else the terrain's), `GetFormationKind()` is the one in effect.
- **Places on a formation change (contract 10.6):** a new formation in effect (fold, mirror, trail column on / off) re-assigns the followers'
  places at once by minimal travel (`ReassignFormationPlaces`, no cooldown, any saving > 1 cm); the 100 cm gain rule stays for the way.
- **Fallback slots on the leader's side (contract 7):** `ResolveReachableSlot(..., bLeaderSide)` rejects a fallback candidate a trace from the leader
  cannot reach (a walkable point behind a wall).
- **Bypass "B":** a new own order of the leader in group mode (no box selection, no slide / wall order): `PlanFormationBypass` gathers the walls
  along the leg, the first FREE-STANDING one blocking it (`FirstBlockingWall`, agent radius 35 cm; walls of rooms / corridors stay the nav mesh's
  business) -> `FormationBypassRules::ChooseSide` with the known threats (`ReportFormationThreat` + `UTacticalSightSubsystem::GetSquadKnownEnemies`:
  the enemies shown now and the last-known silhouettes with their age; without the squad's sight an enemy an operative has a clear line to) ->
  the leader's order is steered through the chosen side's corners (`AOperativeCharacter::SteerOwnMoveVia` / `ResumeOwnMove`: his order, its
  destination and serial stay his; a waypoint reached -> the next, the last -> on to his destination; his next order drops it). Every decision is
  logged (`[Formation] bypass N: wall ... on the left (shielded, ...)`) and kept for smokes (`GetLastFormationBypass`). Not under a tutorial
  confinement (its own path judgement wins).
- **Corner watch:** `ApplyFormationCornerWatch` - `FormationCornerRules::Watch` on the places (actual positions) every frame of the move and of a
  wall slide; in walk mode the watcher's look replaces his slot's and is a FIXED look for the coverage allocator (the others cover what he leaves);
  a combat target / facing order still wins (the operative's facing priority). `GetFormationCornerLooks()`.
- **ClearLook:** every follower's look and the leader's go through `FormationSectorRules::ClearLook` (the world walls only: a wall lower than
  135 cm is looked over and never turns a look - the tutorial's 1.2 m sight wall) unless exempt (`FormationGuardRuntime::ShouldClearLook`: the wall
  layout / slide, run, the corner watcher, the leader looking within 35 deg of his travel).
- **The menu's choice:** `USquadSubsystem::SetFormationMenuChoice` (FormationMenuRules: a manual choice holds until auto; "all forward" = the
  terrain's formation, mode Forward); dev console `Codex.Formation.Menu <auto|line|diamond|echelon_right|blizzard|column|hedgehog|echelon_left|all_forward>`.
- **The hold-B radial menu (stage 7, 2026-10-26; user decision: G is the grenade, the menu moved to B):** `ACodexTacticsPlayerController`
  (`CodexTacticsPlayerControllerFormationMenu.cpp`): IA_SoloMode Started / Completed -> `SoloModePressed` / `SoloModeReleased`, the tap / hold on
  real time by `FFormationMenuKeyTracker` (`UI/FormationMenuInputRules`). TAP B (released before `FormationMenuHoldSeconds` = 0.25 s) = the solo /
  squad toggle (now on the RELEASE, was on the press); HOLD B = the menu, only while `CanOpenFormationMenu` (squad mode, >= 2 members, not
  turn-based, orders not locked, no dialogue, `Squad.Formation` not tutorial-locked - asked silently); otherwise the hold is a plain tap (the
  toggle on the release). The menu (`UFormationMenuWidget`, NativePaint, created lazily, z 30, never pauses) opens at the cursor pulled in to keep
  the ring + labels on screen (the OS cursor follows a pulled centre); the cursor offset from its centre -> `FormationMenuRules::Move`; the B
  release -> `Release` -> `SetFormationMenuChoice` + `PlayerCommands::FormationMenu` ("Squad.Formation"; F1 row "Hold B"); Esc (`DialogueSkip`
  first) / RMB (`CameraDragRotateStart` first) -> `Cancel` (the later release does nothing); LMB does nothing while open; the camera's edge pan
  stops while open; the menu closes by itself when it becomes unavailable (solo, a dialogue, members lost). Smokes:
  `SetFormationMenuCursorOffset` = a virtual cursor (the menu then opens at the view centre). Check: `FormationMenuSmoke`.
- **Checks:** `FormationLabSmoke` (check 4 on the requested formation; check 7 "never split" and check 9 "somebody looks round each passed wall end" on
  every run; R_E_corner: the turn fold; manual runs R_B_walls "echelon left" -> the mirror + HUD line, R_D_corridors "line" -> the trail column + HUD
  line; R_E_corner_pass: the corner watch at W_E_14's free end), `FormationBypassSmoke` (lane G: the four routes + the shielded side from the squad's
  own sight of a parked hound). Stills: `Saved/Screenshots/FormationV21/` (`FormationLabSmoke shot`, `FormationBypassSmoke shot`).
- **A new leader looks forward (bug 2026-10-26, found by TutorialSmoke; the coordinator: "the leader's sector is 12"):** a member made the leader
  (`SetLeader`, e.g. the tutorial's medkit lesson selects another operative and back) no longer keeps the follower look he turned to (the rear
  guard's 6 o'clock): the formation heading - his halt look for 1 s - starts from `FormationRuntime::NewLeaderLookYaw` (walking: his travel; an
  order of his own pending: its direction; standing: his own facing from when he last led, within 10 s; else the squad's heading). Under
  `Codex.Formation.Sectors` only. Smoke `SquadControlSmoke` (switch away and back: forward again within 0.5 s); test
  `CodexTactics.Formation.Runtime.NewLeaderLook`.
- **Known limits / open:** the world walls are boxes only (a building made of one complex mesh is not seen by the guard / corner / bypass); the
  corner watch flips between the two exits of the lab's wide corridor when the leader walks exactly `classify.probe_side` (400 cm) from both walls (the
  rule's `Lateral > probe_side` edge); the HUD says "(wall)" for the turn fold too (labels.ts wording); a manual line's rear guard walks backwards
  to catch up a slot that jumps forward (rules v2.2 rear guard) and trails up to 5 m (not gated for manual formations, as in checks.ts).

### After a retreat: face the enemy again (user request 2026-10-22)

`ThreatFacingRules` (pure, `CodexTactics.Squad.ThreatFacing.*`), applied in `AOperativeCharacter::UpdateCombatFacing`: in a real-time wave fight
an operative who stands (no move order, no velocity), has no combat target in his sights (it fell out of his sight while he ran), has no facing of
his own (cover / pending cover, an idle facing set by a group cover or a warm spot, `SetFacingPoint` - `bExplicitFacing` until his next move -,
a quick fire) and is not on the turn-based grid turns at his stance turn rate to the enemy he last fought (its last seen spot while it is hidden from
the squad, its live spot while seen), else to the nearest live enemy within 30 m; nobody left - no turn. The memory lasts for the wave fight
(cleared outside WaveCombat). Exploration is unchanged. `IsTurningToThreat()` for smokes / debugging. Smoke `RetreatFacingSmoke` (two operatives
firing at a pack of three, a 26 m sprint back: backs to the pack while running, then the turn drives the facing back within 30 deg and both fire
again; a facing order is kept).

### Post-fight hold

See [waves-horde-encounters](waves-horde-encounters.md) "Fight over: the squad stays put": after a fight the followers keep their spots until the
leader moves - or until Regroup (B) (2026-10-23, below).

### The squad stays in formation in the fight (user decision 2026-10-26)

Designer: "in the tactical pause I give orders to soldiers, then resume. Combat starts, and the soldiers do NOT return to the formation: they keep acting
SOLO; pressing B several times does nothing; it ends only when the combat ends. Until I give the order to switch to solo mode, they must stay in formation."
**Root cause:** not the orders and not the B handler - `USquadSubsystem::IsFormationActive` was `SquadOrderRules::IsFormationPhase` (Exploration /
PostCombat only), so from the preparation to the fight's end no follower ever walked (`TickFormationFollow` returned early), a follower in cover never
left it, and Solo on / off (B, never refused in the real-time fight - `Codex.RealTimeOrders` 1) only changed a flag nobody read. It was a recorded
Godot-era rule: Godot `main.gd _end_cutscene_and_start_pause` set `is_tactical_mode = true` (individual control) for every member at the preparation and
cleared it at the fight's end; ported as "formation only in exploration (Godot is_tactical_mode)" (TANDEM, Sprint 03, 2026-09-29). **The user decided
otherwise on 2026-10-26** - implemented:
- `SquadOrderRules::IsFollowPhase(Phase, Mode, bPostCombat, bCombatFormation)`: the following runs in Cutscene, Preparation (and the rest between waves),
  WaveCombat real time and tactical pause, WaveCleared, PostCombat; not on the turn-based grid, not after game over. `IsFormationActive` uses it and is off
  while Commander Mode fights on its own (`UGameFlowSubsystem::IsSquadAutonomyActive`: the autonomy owns the members in real time; the pause hands them
  back). Console `Codex.Squad.CombatFormation` 0 = the old rule (A/B).
- Combat start never switches to Solo; Solo only on B (the trap Solo stays an exploration rule: `TrapSpotRules::DecideSquadResponse(IsExplorationFormation())`).
- A member given an individual order (in the pause or in real time) carries it out - his own move / use / object task as before, and now a running
  SUPPRESS order or quick-fire burst (`FOwnOrderFacts::bFireOrder`) - then rejoins the formation on the next repath, as out of combat. A member fighting
  from cover holds it until the leader walks away (`ShouldLeaveCoverToFollow`), then follows.
- `UpdateFollower` leaves a panicking / raging / knocked-down follower alone (panic and rage move him themselves; `StopOperative` would cut the flight).
- In the real-time fight a parked follower gets no "face where the leader faces" idle facing, and the threat turn (`ThreatFacingRules`, a standing member
  who fought turns back to the threat) wins over the formation look sector (`AOperativeCharacter::UpdateCombatFacing`).
- ~~The exploration-only group features keep the old rule (`USquadSubsystem::IsExplorationFormation`): the group cover order without a box selection, the
  formation wall layout / wall group / slide preview (a wall order ends when the fight starts, as before). In the fight a cover click stays the single
  order of the selected operative (a box selection still takes cover together) - so individual cover orders in the pause stay individual.~~
  SUPERSEDED the same day (user decision 2026-10-26 "group cover in combat: YES"): the group cover family runs in the fight too
  (`USquadSubsystem::IsGroupCoverFormation`, "Group cover order" above); only a spotted trap's Solo keeps `IsExplorationFormation`.
- The B handler (`ACodexTacticsPlayerController::ToggleSoloMode`, `IA_SoloMode`) is unchanged.
- A box-selected group move in the preparation / fight is each member's own order: they walk to their group slots, then (Solo off) the followers rejoin the leader's formation while the box selection stays (as in exploration, rev 5g). `GroupSelectSmoke` stages 1 / 13 check it (2026-11-01: the smoke had kept the pre-2026-10-26 "stand on the group slot" expectation and failed since `3ceb5629`).
- **A follower yields to the leader's own walk (bug 2026-10-28, CoverSmoke):** with the formation on in the fight a follower walking into his slot beside the leader at a wall stood on the leader's cover walk along it - the walk ended `Blocked` at velocity 0. While the leader walks an own order (cover / move; not the wall order / slide) a follower on his straight way ahead, or whose slot lies on it, steps aside (`FormationRuntime::YieldFromLeaderWay`: 80 cm corridor, 120 cm aside on his own side first, on the nav mesh) and takes his slot again once the way is clear. `Codex.Formation.YieldToLeader 0` = the old follow (A/B). Tests `CodexTactics.Formation.Runtime.Yield.*`.
Tests `CodexTactics.Squad.Orders.CombatFormation.SquadStaysInFormationThroughTheFight`; smoke `CombatFormationSmoke` (the wave fight starts: Solo off,
formation on - `CombatFormation 0` reproduces the bug; pause, B and C ordered 6 m to the sides, A selected again, resume: both carry their orders out, then
rejoin round A; A walks 11 m - both follow; B (the key) in the fight: Solo on, A walks 8 m, they hold; B again: Regroup, both back at A).

### After the final fight: Regroup brings them back (bug 2026-10-23, lake after the horde)

Designer: "after the fight one operative died, one froze, and even pressing B he does not return to the formation; Solo does not switch off".
Three causes, none of them the dead / frozen operative as such (the leadership passes on a death, `UnregisterOperative`; B is not refused):
1. **The formation was off for good after the final fight.** The last wave / ambush fight ends in **PostCombat**, which lasts until the level ends
   (nothing calls `FinishPostCombat`), and `USquadSubsystem::IsFormationActive` was "Exploration only" - after the horde no follower ever walked
   again, B toggled Solo on / off without a visible effect. Godot `_return_squad_to_prep_stations` cleared `is_tactical_mode` at the fight's end.
   Fix: `SquadOrderRules::IsFormationPhase` (Exploration and PostCombat).
2. **Regroup did not end the post-fight hold** (2026-10-22, it ended only when the leader moved). Fix: `ExitSoloMode` ends it
   (`KeepsPostFightHold(..., bRegroup)`).
3. **A formation slot off the navmesh was unreachable for good** (inside a wall, the generator, the fallen mate's remains crate - a navmesh hole):
   while the leader stood still the follower stood too (`FollowTo` -> Unreachable every repath). Fix: `USquadSubsystem::ResolveReachableSlot` -
   a slot that does not project onto the navmesh is replaced by the nearest candidate round the leader that does
   (`SquadFormation::ComputeFallbackSlots`: the slot's offset turned in 30 deg steps, at its distance and at 60 %).
A frostbitten follower (prone, cannot get up) crawls back slowly (x0.25 cold speed); warmed he walks. Consoles (A/B, 0 = the old rule):
`Codex.Squad.PostCombatFormation`, `Codex.Squad.SlotFallback`. Tests `CodexTactics.Squad.Orders.RegroupAfterFight`,
`CodexTactics.Formation.FallbackSlotsRoundTheLeader`; smoke `RegroupAfterLossesSmoke` (Solo with the Engineer leading, he is killed -> the
Commander leads, Solo stays on; the Medic-Sapper frostbitten; the fight's end -> PostCombat; with each old rule the bug reproduces (B: she stays),
with the fixes she crawls back, warmed she walks into the formation, the leader walks 10 m and she follows).

## The far side of a wall (the game designer's play-test 2026-11-01 on L_FormationLab; branch formation/wall-far-side)

Report (`Saved/BugReport/1.11.2026_bug_02.avi`, `3-5.webp`): (bug_02) the squad crouched at a wall, the player wants it at the FAR side of the same
wall - his clicks land on the wall's top, its near face or the operatives (the feed spams "Operative selected: ..."), the cover ghost snaps to the near
face, the squad jumbles / stalls going round; (split) the squad crouched at one wall, ordered to the far side of the OPPOSITE wall - on the run the
Commander went round one end, the Engineer + the Medic-Sapper round the other. Five approved fixes, each with an A/B console switch:

- **a) The wall side by the cursor** (`Codex.Cursor.WallSide`, input UX, any formation / CVar): `ACodexTacticsPlayerController::ResolveWallSideSlot`
  (`CodexTacticsPlayerControllerWallSide.cpp`; pure `Tactics/WallSideRules`, tests `CodexTactics.Tactics.WallSide.*`). A hit on the TOP of a HIGH wall:
  its two faces are traced along the cursor ray (planar) just under the hit (`LineTraceComponent`, the same object), the top is split at its middle line
  (`SideOfTopHit`): beyond it = the FAR face, before it = the near face; the slot = `CoverTraceRules::FindCoverSlotAt` on that face (only high cover - a
  low wall's top keeps its old meaning, a move / vault). A hit on the GROUND: traced back along the cursor ray (planar) at chest height (110 cm) - a high wall's face
  pointing away from the camera, the ground within `FarGroundReachCm` of it (the band the wall hides along THIS ray: height / tan(the ray's depression) +
  1 m, 1.5 .. 10 m) and the leader on the wall's other side (`IsGroundBehindWall`) = the far face (not the leader's own cover object while he is in cover:
  his wrap along it decides, as before). A face hit keeps the face that was hit (`FindCoverSlotForHit`). Used by `TryHandleCoverClick` (not on the
  turn-based grid): the first click = the ghost on that face, the second = the order (the group cover order -> the wall layout there); a far-face click on
  the leader's own cover object = the wrap round it in cover. Log `[Cover] click at (...) on <wall>: the FAR face (slot ...)`.
- **b) At a wall a click picks an operative only on his body** (`Codex.Cursor.TightPick`): `FindClickedMember` while `USquadSubsystem::IsSquadAtWall`
  (the wall layout / its order stands, or a member is in cover / the wall pose) uses `ClickPickRules::PickRadius`: the ground within 45 cm of a body still
  picks him, a wall's top / face never (it was the 120 cm `SelectRadius` - a click on the top right above a crouched man, 57 cm off his body, selected him).
  A click on the body itself, the portraits and the number keys select as before.
- **c) The occluder fade - REMOVED 2026-11-01** (the game designer, bug_05: "a failed solution" - walls never fade): `UOccluderFadeSubsystem`,
  `M_OccluderFade`, `Codex.OccluderFade`, `UpdateWallSideHover` and the cursor's `ThroughFadedOccluders` step are gone (branch `fix/custom-ghosts-solo`). The
  far side is reached by the top's far half (a); the ground right behind a high wall counts only where the camera still sees it (the wall may hide it all -
  accepted).
- **d) The far side of the same wall: one column round the NEAR end** and **e) no split** (`Codex.Formation.WallRoute`, behind `Codex.Formation.Sectors` +
  `Codex.Formation.V21`). Root cause (code + `FormationFarSideSmoke` A/B): the group cover order in group mode is the formation wall layout
  (`OrderFormationWall`): the leader runs to his slot (his own navmesh path; the polygons can make it the FAR end - at x 3400 on the lab's W_C_8 every path
  went east though the west end is 7 m shorter), every follower walks to his OWN wall slot with his own navmesh path (`UpdateSectorFollower -> FollowTo`), so
  men whose slots lie either side of the decision line take different ends - with `WallRoute 0` the smoke reproduces it: `Commander east, Engineer west,
  Medic-Sapper east`, the squad 16 m apart. Now (`SquadSubsystemFormationRoute.cpp`, pure `Formation/FormationWallRouteRules`, tests
  `CodexTactics.Formation.WallRoute.*`): (1) the wall order's own walk runs the rules v2.1 bypass planner (`PlanFormationBypass`, the shortest side or the
  shielded one with a known threat) - the leader goes round the NEAR end; (2) every frame of the wall order a follower whose straight way to his wall slot is
  cut by a wall body (`NeedsRoute`, 15 cm clearance) walks a column ON the leader's path since the order (`PlacesAlongTrail`: column.base_back + i x
  column.spacing back along it, never past where he started; the column order fixed when the route begins, `AssignColumnPlaces`) - no approach duty, no
  wall pose, he runs with a sprinting leader; once his way is clear (round the obstacle) he is released into his slot for good; once all are clear the wall
  places are re-dealt by travel (nobody crosses another at the far face). Log `[Formation] wall route: a wall between Engineer (Z) Medic-Sapper (P) and their
  wall slots - ...`, `... is round the obstacle`, `wall route over: the wall places by travel ...`. Getters `IsFormationWallRouted`,
  `GetFormationWallRouteCount`. With the formation console off the group cover order is the old per-slot order (unchanged).
- **Checks:** smoke `FormationFarSideSmoke` (L_FormationLab, the REAL click path: the player camera's ray through the aimed point -> `HandleWorldHit`; the
  camera yaw locked looking north): scene 1 the near-face layout at W_C_8; scene 2 (b) the top above each crouched follower and the
  face beside him select nobody (a cover click on the near face), his body selects him; scene 3 (a, d) the top's near half -> the near-face ghost, the
  far half -> the far-face ghost with the wall solid (no fade since bug_05), the first visible ground behind the wall (when the camera sees any) -> the far face, the confirm at the spot where the old rule splits (`FindSplitX`: each member's own navmesh path checked) -> the leader round the
  near end, everyone round the SAME end, no stall, the layout on the far face; scene 4 (e) the same from the opposite wall W_B_3. A/B: `-PreCmds
  "Codex.Formation.WallRoute 0"` (split + far end), `"Codex.Cursor.TightPick 0"` (selections), `"Codex.Cursor.WallSide 0"` (no far
  side). `Shot` (rendered) -> `Saved/Screenshots/FarSide/01_far_side_ghost.png`, `02_opposite_wall_ghost.png`, `03_same_wall_column.png`,
  `04_opposite_wall_column.png`.
- **Limits:** the route reads the world's BOX walls (`GatherFormationWalls`) - a complex-collision building is not routed round (the nav mesh decides, as
  before); the far side of a LOW wall is the old click (a vault / a move); the side split uses the wall's two faces along the camera's view (a wall seen
  end-on has no far half - its faces are the ends).

## The custom formation (game designer decisions 2026-11-01; branch formation/custom)

Design and UX: `docs/design/FORMATION_SECTORS.md` "Custom formation"; data contract `docs/design/FORMATION_CONTRACT.md` 16; TS reference
`Tools/TrainingGround/src/movement/custom.ts`; C++ `Formation/FormationCustomRules` (parity `CodexTactics.Formation.Parity.Custom*`, 0 diffs).

- **Menu**: the hold-B radial menu draws a row of three discs below the ring - Custom 1 / 2 / 3 (`EFormationMenuItem::Custom1..3`, `FormationMenuRules::CustomItemAt /
  HitAt`, widget `UFormationMenuWidget::SetCustomView`). Release on an empty disc = the placement mode for that slot (effect `Edit`); on a filled one = the squad
  takes the shape (a manual choice, holds until Auto); Ctrl held on the release = edit it. With `Codex.Formation.Sectors 0` the row is not shown (the cursor
  there is the ring's) and a held custom choice is inert (the old formation runs).
- **Placement mode** (`CodexTacticsPlayerControllerFormationCustom.cpp`, pure state `UI/FormationPlacementRules` `FFormationPlacement`): the world stops (global
  time dilation `FormationPlacementTimeDilation` 0.0001, re-asserted every tick; restored through `UGameFlowSubsystem::ApplyTimeDilation`; not the tactical
  pause, no charges; the camera runs on real time). Every member in the formation (the leader + his followers, not a man on guard) gets a hologram ghost
  (`AFormationGhostActor`: a copy of his mesh STANDING POSED - bug_05 2026-11-01, they stood in the T-pose: `ABP_Operative` leaves the baseline
  `Animations.Idle` empty, so the ghost holds `ReadyPoseClip` (the Rifle_2 idle loop) at 35 % on a paused single node, evaluated at once
  (`TickAnimation` + `RefreshBoneTransforms` - the placement pause leaves no tick time), checked against the reference pose
  (`FormationPlacementRules::PoseOffRefDeg` >= 10 deg, else it follows the operative's own pose; `GetPoseSource`); the dedicated hologram
  `/Game/UI/Materials/M_FormationGhost` (`Scripts/Editor/create_formation_ghost_material.py`: unlit translucent, skeletal-mesh usage, exposure-invariant,
  core fill + a bright fresnel rim + rising scanlines; "Color" / "EmissiveIntensity") on every slot, the see-through silhouette as the overlay (seen behind
  walls); orange, selected = brighter yellow, refused = a red flash) at his stored place (editing) or where he stands (a new slot). Pick an operative by
  his body (the cursor's hit), within `GhostPickRadius` 60 cm of his ghost, his number key 1-4 or his portrait (`SelectFormationPlacementMember`; bug_05:
  the keys / portraits changed the LEADER and the placement kept placing the old pick - now they only pick for the placement, the leader stays the anchor);
  LMB down on the ground = his spot (must project onto the nav mesh), the drag to the release = his look (shorter than 50 cm = the squad's heading = the
  leader ghost's look); each placed ghost stays while the others are placed; refused spots (closer than slot.min_spacing to another ghost, or past the 40 m
  sanity reach of the leader's ghost - the 10 m reach was lifted 2026-11-01, bug_05) flash red with an HQ line. The tactical pause's order radius never
  applies (its 12 m ring is hidden while placing: `URadiusRingSubsystem`). The leader's ghost is the anchor: moving / turning it moves the frame. The HUD draws each ghost's look
  arrow on the ground, the drag in progress and a banner above the action bar (`ACodexTacticsHUD::DrawFormationPlacement`). Space, a B TAP (bug_05: a B tap toggled
  SOLO and the placement then cancelled itself for the split squad - `FormationPlacementRules::ResolveBTap`) or the same Custom in the menu confirms: the shape stored (`USquadSubsystem::SetFormationCustomShape`), picked, `ResetFormationState` with the anchor's heading, the leader walks to
  his ghost when it moved, the followers fall into their places. Esc / RMB cancel (the slot and the choice unchanged). The mode ends itself on Solo, < 2
  members, turn-based or a dialogue.
- **Per tick** (`SquadSubsystemFormationCustom.cpp`): `Requested = Custom` (an empty slot -> the terrain's formation); the guard
  `FormationCustomRules::Resolve` (the shape cut here / 5 m ahead or a man cut off -> the trail column, HUD "Custom 1 -> Column (wall)"; back to the shape after
  classify.hold_distance of path); unfolded, `CustomSlots` in the leader's frame (`Sectors.CustomRoles`: each operative his own place; a new leader takes the
  anchor, the old anchor man the new leader's place; a dead / missing member skipped; a member the shape lacks walks behind the rearmost place). Looks: the
  player's fixed looks relative to the heading through `FormationCustomRules::Look` (ClearLook only when the look points into a wall within look.wall_dist);
  no place reassignment, no corner watch, no coverage allocator; the facing priority (combat target > explicit facing order > cover > formation) is unchanged;
  a run faces the run as in every formation.
- **Kept**: the three slots in the save game (`game_state.formation_custom`, `formation_menu_choice`) and across levels (the campaign carry) - `save-load.md`.
- **Smoke** `CodexTactics.FormationCustomSmoke` (L_FormationLab): the real paths (hold B with the virtual cursor, the placement LMB down / up handlers on the
  cursor's real ray to each body, Space, Esc), refusals, a ghost 16 m out accepted, every placed ghost kept while the next is placed, the ghosts posed in
  `M_FormationGhost`, Space / B tap / Esc / RMB with NO solo mode, Custom 3 picked by number key / portrait / body (the leader unchanged), the anchor move, the shape kept standing / walking (p90 12 cm) / after lane D's corridor (fold + unfold), the looks, Ctrl edit + Esc, a
  three-man Custom 2 confirmed in the menu with the 4th as the extra, the carry and save -> load; `shot` (rendered) writes
  `Saved/Screenshots/CustomGhosts/01_placement_ghosts.png`, `02_ghosts_three_placed.png` (close-up), `02_custom_shape_walking.png`.

