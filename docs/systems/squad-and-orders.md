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
logs every raw terrain class change with its probe hits, `2` also every follower repath (role, distance, slot speed, speed, backwards).
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
- **Open:** the hold-G radial menu (manual formations, `EFormationMode::Forward`), the hedgehog at a fire (`FormationShapeRules::HedgehogSlots`), the
  designer's back-to-the-wall clip; the 4th man of an echelon trails ~1.6 m after an echelon <-> diamond switch (walks backwards at 0.9 x walk; the
  simulator ~1 m) - a Training Ground tuning point.

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
