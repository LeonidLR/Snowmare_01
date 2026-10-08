# Save system audit (2026-10-08)

Scope: `USaveGameSubsystem` (`Source/CodexTactics/*/Core/SaveGameSubsystem.*`), `SaveGameRules`, the JSON slot format.
Save policy (user decision 2026-10-08, option b): saving only **outside combat** (exploration, preparation, after a wave /
victory); **autosave** right before a fight starts (wave start / ambush start) and right after it ends (wave cleared /
victory); during the fight (real time, tactical pause, turn-based) Save is refused with *"Saving is disabled during
combat"*; loading is always allowed.

Format version **2** (`SaveGameRules::CurrentSaveVersion`). Version 1 saves (Godot keys) still load: every new section is
optional and a missing one leaves that part of the world as it is (old behaviour); the flow resumes as before
(`ResolveLoadFlow` version 1 branch). A newer version than the build knows loads what it understands (warning in the log).

"Before" = state of `main` at 1b7d057. "Fix" = this change. Verified by `CodexTactics.SaveRoundTripSmoke` (snapshot /
save / mutate / load / snapshot diff, in place and after reopening the map) and `CodexTactics.Save.Policy.*`.

## Squad / operatives

| System | Saved before? | Restored before? | Gap | Fix |
|---|---|---|---|---|
| Transform (position, yaw) | yes | yes | — | — |
| Health / max health | yes | yes (via a damage call) | restored as "damage" (OnHealthChanged with a negative delta) | `UHealthComponent::RestoreHealth` (no damage event) |
| Cold level, fortitude, level / EXP / stat points, accuracy, luck | yes | yes | — | — |
| Stance | yes | yes | — | — |
| Weapon in hands + clip / reserve per weapon (`AmmoInventory`), ammo of non-arsenal weapons (`ExtraAmmo`) | yes | yes | — | — |
| Matches, turrets / barricades / mines, medkits, canned food, bread, chocolate, grenades | yes | yes | — | — |
| Bonus items found in crates (`BonusItems`) | no | no | lost | `bonus_items` |
| Guard (T) | yes | yes | — | — |
| Fire posture: squad-wide + per-operative override | no | no | reset to Aggressive on load | `game_state.squad_posture`, `squad[].posture_override` |
| Commander Mode (Ctrl + T) | no | no | reset | `game_state.autonomous_combat` |
| Leader, solo mode | yes | yes | — | — |
| Rage / panic / stress | no | — | intentionally not saved: rage starts only from crits in a fight, panic only in a fight, stress decays outside combat — a save is always outside combat | — |
| Knockdown | no | — | n/a out of combat (intentionally not saved) | — |
| Status effects (burning, frozen, bleeding timers), cover state, aim, planned pause orders | no | — | combat-only / seconds-long; intentionally not saved | — |
| A carry / push / deploy order in progress | no | no | the carried object stayed attached / hidden after a load | the load cancels every relocation / deploy task first (`CancelActiveTask`, `CancelPlacement`), the objects then take their saved place |

## Squad composition / recruit (Ivan Susanin)

| System | Saved before? | Restored before? | Gap | Fix |
|---|---|---|---|---|
| Susanin recruited | yes | yes | — | — |
| Rescue already happened but not recruited (he waits in cold distress) | no | no | after a load in a fresh world he was gone and the rescue could not repeat (it fires on wave 1 only) | `susanin.rescue_triggered` + his own operative record (`susanin.operative`) → `URecruitSubsystem::RestoreRescue` |
| No Susanin at the save, one in the session | — | no | a Susanin of the session stayed | `RestoreRescue(false, false)` removes him (version 2 saves) |

## Game flow / mission

| System | Saved before? | Restored before? | Gap | Fix |
|---|---|---|---|---|
| Combat unlocked, wave index, solo | yes | yes | — | — |
| Phase | only preparation / wave flags | Exploration or Preparation | a save after a wave (WaveCleared) or after the victory loaded into Exploration with the combat zone unlocked | `game_state.phase` + `SaveGameRules::ResolveLoadFlow`: WaveCleared → next wave's preparation (or the finished battle after the last wave / a single ambush fight), PostCombat → exploration with the combat zone locked |
| Preparation time left | no | no (full time) | — | `preparation_time_left` (`RestoreForLoad(..., PreparationSeconds)`) |
| Ambush fight / level encounter | no | — | — | `is_ambush_fight`; the BeforeCombat autosave of an ambush is written as *exploration before the ambush* with the patrols back on duty |
| Map | no | — | a menu could not know which level to open | `map` (`FSaveSlotInfo::MapName`), `LoadGameWithTravel` |
| Pause charges, turn-based uses, tactical pause | no | — | combat-only; intentionally not saved (reset per wave) | — |

## Enemies

| System | Saved before? | Restored before? | Gap | Fix |
|---|---|---|---|---|
| Living enemies out of combat (position, yaw, health, class, archetype) | no | no | a fresh world respawned every placed enemy at full health | `world_state.enemies[]` |
| Dead enemies | no | no | **dead enemies came back** after Continue | load removes every enemy not in the save |
| Patrol route + waypoint index + direction + phase (move / wait / turn) + wait left | no | no | patrols restarted at waypoint 0 | `patrol.*` (`AEnemyCharacter::CapturePatrolSnapshot / RestorePatrolSnapshot`) |
| Escort leader | no | no | lost for runtime escorts | `escort_leader` (by actor name; recreated leaders are matched) |
| Search (origin, elapsed), suspicion | no | no | lost | `patrol.searching / search_origin / search_elapsed / suspicion` |
| Runtime patrol routes (bot / smokes) | no | no | lost in a fresh world | `world_state.patrol_routes` (points, loop, ping-pong, waits); a missing one is rebuilt |
| Perception override (`bOverridePerception`, smokes only), wave modifiers | no | — | intentionally not saved (debug / per-wave tuning; health is saved) | — |
| Enemies in a fight | — | — | saves are refused in a fight; the after-combat autosave holds none (wave cleared) | — |

## World objects

| System | Saved before? | Restored before? | Gap | Fix |
|---|---|---|---|---|
| Supply crates (looted, defused, destroyed, exact two-way stash) | yes | yes | — | — |
| Piles on the ground | yes | yes | — | sorted by place in the file (stable order) |
| Barrels: burning + time left / burnt out | no | no | a lit barrel was fresh again after a load | `objects.<name>.burning / burnt / burn_left` (`ABarrelActor::RestoreBurnState`) |
| Barrels / barricades / turrets moved (relocation) | no | no | back at the authored place | `objects.<name>.pos / rot` for every interactable |
| Barricades, turrets (placed by the squad or in the level): presence, health, turret power / broken | no | no | placed ones vanished, picked-up ones came back, health full | `objects.*` + class path: placed ones are recreated, ones placed after the save removed; `health / max_health`, `powered / broken` (`ATurretActor::RestoreSaved`) |
| Mines: presence, squad / hostile, live charge, revealed, arming countdown | no | no | same; a revealed hostile mine was hidden again | `placed_by_squad / trapped / revealed / arming_left` (`AProximityMineActor::RestoreSaved`) |
| Tripwires: anchors, peg / bracket, arming | no | no | lost | `anchor_a / anchor_b / anchor_*_on_object / arming_left` (`ATripwireActor` getters + `RestoreArmingLeft`) |
| Object traps (grenade tripwire charge), defused, failed attempts, warning | crates only | crates only | barrels / barricades / quest objects lost their trap | `trapped / defused / failed_defusals / defusal_warned` |
| Generator health / breakdown | no | no | a broken generator was whole again | `generator_health / generator_broken` (`AInteractableActor::RestoreGeneratorState`, heat follows) |
| Quest objects: canister picked up (hidden) | no (flag only) | no | the canister lay there again in a fresh world | `hidden / collision` per object |
| Gate open | flag | only via the quest (opening animation) | a gate never closed again when loading an earlier save | `world_state.gates` (`AGateActor::RestoreOpen` snaps the leaves, collision / nav follow) |
| Generator running / gate powered (quest chain) | yes | yes (forward only) | going back left the heat on | quest restore + `RestoreGeneratorState` re-evaluates the heat; turret power is per turret |

## Quests / narrative

| System | Saved before? | Restored before? | Gap | Fix |
|---|---|---|---|---|
| Quest chain (canister, diesel, generator, gate powered) | yes | yes | — | — |
| Dialogue triggers already played | no | no | once-only dialogues played again | `world_state.dialogue_triggers` (`ADialogueTriggerVolume::RestoreTriggered`) |
| Narrative elements read | no | no | — | `objects.<name>.read` |
| Mission intro / cutscene seen | — | — | intentionally not saved: a loaded save starts without the intro (`UMissionSessionSubsystem::HasPendingLoad`) | — |
| Feed / radio history, objective text | no | — | intentionally not saved (the objective is recomputed from the quest chain / phase) | — |

## Policy / API

| Item | Before | Fix |
|---|---|---|
| Save in combat | allowed (F5, dialog) | `CanSaveNow(&Reason)`; `QuickSave` / `SaveToSlotWithMessage` refuse with the feed line *"⛔ Saving is disabled during combat."*; the frontend pause screen asks `CanSaveNow` (via `CodexSaveBridge`) |
| Autosave | none | `Autosave(BeforeCombat / AfterCombat)` on `UGameFlowSubsystem::OnBeforeGameFlowChanged` (fires before any other system reacts, so the pre-combat save sees the world before the wave spawns), slot `autosave`; off for headless `-ExecCmds` checks unless a check enables it; CVar `Codex.Save.Autosave 0` |
| Continue / Load from a menu | — | `LoadGameWithTravel(Slot)` (pending slot in `UMissionSessionSubsystem`, applied 0.5 s after the level began play), `GetContinueSlot()`, `FSaveSlotInfo::Version / MapName`, `OnSaveApplied()` |
