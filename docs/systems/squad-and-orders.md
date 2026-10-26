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

### The squad's corner peek (2026-10-26, branch formation/v24-squad-peek; user bug report; behind `Codex.Formation.Sectors`)

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
- The exploration-only group features keep the old rule (`USquadSubsystem::IsExplorationFormation`): the group cover order without a box selection, the
  formation wall layout / wall group / slide preview (a wall order ends when the fight starts, as before). In the fight a cover click stays the single
  order of the selected operative (a box selection still takes cover together) - so individual cover orders in the pause stay individual.
- The B handler (`ACodexTacticsPlayerController::ToggleSoloMode`, `IA_SoloMode`) is unchanged.
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
