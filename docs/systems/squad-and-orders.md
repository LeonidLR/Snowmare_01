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

