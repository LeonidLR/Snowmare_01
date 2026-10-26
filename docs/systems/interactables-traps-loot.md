# Interactables, traps, loot, quests and the blast gate

> World objects derive from `AInteractableActor` (menus per state, traps on any object). `UInteractionSubsystem` runs click -> approach -> menu;
> `URelocationSubsystem` moves objects and sets up deployables. The blast gate is paced by the generator during the wave fight.
>
> Key files:
> - `Interactables/*` (InteractionSubsystem, BarrelActor, DeployableActor, LootCrateActor, TurretActor, TripwireActor, ProximityMineActor), `Quests/*`, `Interactables/GateActor`
> - Smokes `DeployableSmoke`, `LootSmoke`, `PMCTripwireSmoke`, `TrapSoloSmoke`, `QuestChainSmoke`, `GateGeneratorSmoke`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Spotted trap -> the squad holds in Solo (old §5)

- **Spotted trap -> the squad holds in Solo (rev 5h, game designer play-test 2026-10-15, every level):** a hidden level mine (`AProximityMineActor::HandleSpotted`) or PMC tripwire (`ATripwireActor::RevealBy`) spotted with a spotter calls `AInteractableActor::NotifyTrapSpotted` -> `USquadSubsystem::HoldForSpottedTrap(Spotter, TrapName)`: every member `StopOperative` (the Godot rule) and, by pure `Interactables/TrapSpotRules::DecideSquadResponse(bFormationActive, bAutonomy, LivingMembers)` (`HoldAndSolo` only exploring with the formation, AUTO off, 2+ alive; else `HoldOnly`), a box-selected group drops to the leader (`SetSelectedGroup`), `EnterSoloModeInternal(bForTrap)` (followers crouch except one in cover; no "[SOLO MODE: ON]" line) and the feed line "{Mine|Tripwire} spotted - squad holding. Move them one by one (B regroups)." (speaker SQUAD). `bTrapSolo` (`IsTrapSolo`): no 25 m leash in `Tick` until `ExitSoloMode`; saved as `is_trap_solo` (`RestoreTrapSolo` after the Solo state). Events: `TrapSpotted` (Subject = the spotter) for both kinds, besides `MineSpotted` / `TripwireSpotted`. AUTO keeps the old stop (its paths avoid known traps). Tests `CodexTactics.Interactables.TrapSpot.SquadResponse`; smoke `TrapSoloSmoke`.

## Interaction / world objects, quest chain, blast gate (old §5)

- **Art meshes and quest events (2026-10-21, Demo 01 lake quest):** `AInteractableActor::VisualMesh` + `VisualMeshTransform` - an art mesh shown at its own transform relative to the collision box (never stretched; empty = the class placeholder filling the box, as before; clearing it brings the placeholder back), `AGateActor::LeafMesh` + `LeafMeshTransform` for the left leaf, the right one mirrored (`VisualMeshRules`, tests `CodexTactics.Interactables.VisualMesh.*`); the box stays the collision / navmesh / click shape. The vehicle menu title is the object's `DisplayName` ("Abandoned APC" without one; the BMP-2 wording is gone). `UQuestSubsystem::InteractWith(Type, Object, Instigator)` raises `QuestStep.<Kind>` + `QuestStep` for a step done; the generator raises `GeneratorDamaged` / `GeneratorDown` / `GeneratorRepaired`, the gate `GateOpened` (when the leaves start to slide). Smoke `LakeQuestSmoke` on the lake.

**Interaction / world objects**
- `Interactables/InteractionSubsystem` — click → approach → `BuildActionMenu` → menu or feed line; confirm /
  relocate / trap / cancel. Mine "approaching defuser" mark. Godot `main.gd _trigger_menu_for_object`,
  `_open_action_menu`, `_on_action_confirmed`, `_on_relocate_confirmed`, `_on_trap_confirmed`.
- `Interactables/ActionMenuTypes` (`FActionMenuSpec`, `FActionMenuRequest`), `UI/ActionMenuWidget` (UMG built in C++,
  restylable via a WBP with the same widget names), owned by `UI/CodexTacticsHUD`.
- `Interactables/InteractableActor` — base: quest objects (menus per quest state), **traps on any object**
  (`bTrapped`, defusal attempt on confirm, `TrapWithGrenade`, `DetonateTrap`, `ApplyBlast`). Godot `interactable.gd`.
- `Interactables/BarrelActor` + `BarrelRules` — match, 35 s fire, fade 7 s, heat 5.5 m, charred, `Explode` (trap / shot).
- **Objects / barricades move in the real-time fight too (user decision 2026-10-26: "barricades can be moved only in the tactical pause; cancel that.
  The tactical pause is an auxiliary pause that gives the player a breather; it must not cancel everything else. Maybe these were rules from the Godot
  days").** It was the Godot rule (`main.gd can_relocate_objects_now`: outside a wave / in the pause). `RelocationRules::CanRelocateNow` now refuses only
  the turn-based grid (its own push; the camera-zone solo still allowed there); `ShouldDropActiveTask` drops a carry only on the grid (the real-time fight
  drops nothing). In the real-time fight a click on a movable object picks it up, a click on a barricade / set-up item opens its menu ("Relocate", pick
  up), the barrel menu offers "Push", the action bar's MOVE OBJ works; the order runs at once with the worker's placement radius (`GetPlacementRadius`:
  `WorkerPlacementRadius` in real time, the pause radius in the pause); the pause still plans (executed on the release). Deploying, tripwires and
  "Ignite" already ran in real time (plan in the pause, execute in real time) - unchanged. Console `Codex.Relocation.RealTimeCombat 0` = the old rule.
  Tests `CodexTactics.Relocation.RealTimeCombat.AllowedExceptTurnBased` (+ the old-rule cases); smokes `PauseBarrelOrderSmoke` (real time: the barrel
  menu offers "Push"; B relocates the barricade in real time - it starts at once, is not dropped, stands on the spot), `ClickRulesSmoke` (real time: barrel
  "Push", barricade menu, no HQ refusal; the old rule refuses). Other combat gates that depend on the pause (listed for the user, unchanged): the pause
  only PLANS orders that real time executes (moves, cover, use orders, targeted shots, quick fire, SUPPRESS, deploy, tripwires); `Codex.RealTimeOrders 0`
  (off by default) would lock every order to the pause; vehicles refuse to board / drive / leave IN the pause (`VehicleRules`); a disarm from the wall steps
  off the wall only in real time.
- `Interactables/RelocationSubsystem` + `RelocationRules` + `RelocationGhostActor` — move objects (ghost, radius,
  push task, pause plans, combat / mine drop) **and set-up from supply** (F, two-click placement, deploy tasks,
  mine mishap). Godot `main.gd` relocation + placement blocks.
- `Interactables/DeployableActor` (+ `DeployableRules`) — pick-up / defuse / relocate menus, 1.1 s dismantle, role
  routing. `BarricadeActor` (200 HP, tripwire on enemy contact 1.8 m). `ProximityMineActor` (trigger 1.6 m, blast
  120 / 3.5 m, arming 3 s for squad mines, hidden level mines spotted at 4.5 m / sapper 6 m).
  Godot `deployables/barricade.gd`, `mine.gd`.
  2026-10-25: the barricade's vault nav area (`VaultNavigation::MakeVaultable`, closed to enemies) covers its whole box + 40 cm
  (`VaultRules::NavAreaExtent`; it was a 2 x 2 m square, the ends open to enemy paths), and enemies smash a barricade in the way of an
  operative only when he is unreachable on foot or the squad is saturated - [enemy-ai-and-perception](enemy-ai-and-perception.md) "Operatives first".
- `Interactables/LootCrateActor` + `LootRules` (`FLootContents`, `ELootItem`, `FLootEntry`) — supply crate: intact →
  `HandleDirectInteraction` opens the lid (1.8 s) and the loot dialog, trapped → defusal menu (2 s crouched work),
  detonation burns the contents, enemy contact 1.8 m, tiers Standard / Maximal, Godot colours. Items go to the
  leader: provisions / matches / M16 reserve / deployables (no limit, as Godot) / `ExtraAmmo` for weapons not ported /
  `BonusItems`. `UInteractionSubsystem::OpenLootDialog / LootItem / LootAll / CloseLootDialog`,
  `UI/LootDialogWidget` (+ `ULootEntryButton`). Godot `loot_crate.gd`, `loot_dialog.gd`, `main.gd` loot handlers.
- `Interactables/TurretActor` — powered by default; fires at the nearest visible enemy (≤ 12 m, 16 dmg / 0.45 s,
  barricade in the line of fire = 60 %, walls block); 120 HP, breaks at 0 (repair engineer 2 s / others 4 s);
  menus broken / unpowered («Закрыть») / trapped / pick-up; powered turret is a heat source (4.5 m, Godot
  heat_sources); `SetAllPowered`. Deploy / pick-up routed to the commander first. Godot `deployables/turret.gd`.
- Generator damage on `AInteractableActor` (Generator type): `TakeGeneratorDamage` (200 HP) → breakdown (heat off,
  turrets unpowered, «[АВАРИЯ]» menu), repair on confirm (engineer 2.5 s / others 5 s) → `RepairGenerator`.
  Starting the generator also powers all turrets. Godot `interactable.gd breakdown_generator / repair_generator`.
- `Quests/QuestChain`, `QuestSubsystem`, `Interactables/GateActor`. Godot `quest_manager.gd`, `gate.gd`.
  **Blast gate tied to the generator (user request 2026-10-11, default):** `Quests/GateProgressRules` (pure) + `AGateActor`. Level JSON
  `gate_open_mode` = `generator_waves` (default) | `instant` (the old Godot gate: the terminal opens it). Paced: the terminal only
  unlocks the servos (`UnlockProgress`; quest line «the leaves barely move», objective «Hold the line until the blast door opens»);
  during the wave fight (WaveCombat, real time / tactical pause in game time; not the turn-based grid) the door creeps by
  `gate_progress_per_second` (0.004 = 0.4 %/s) ONLY while the generator works (quest running and no generator broken); a breakdown
  pauses it (never reverses); each wave may push it to `gate_max_progress_before_final` (0.9) x wave / final wave (3 waves: 30 / 60
  / 90 %); it opens fully only when `gate_full_open_after_wave` (0 = the level's last wave) is cleared (latched `IsFinalWaveCleared`)
  AND the generator works (down at that moment -> it waits for the repair). Before that the door stays VISUALLY CLOSED
  (user decision 2026-10-11: no leaf creep; `MaxCrackDistance` removed) — the collision boxes and the navmesh block stay closed. A
  rotating amber warning beacon (`BeaconPivot` / `BeaconMesh` / `BeaconLight`, editable `BeaconOffset`, `BeaconSpinSpeed`,
  `BeaconMaterial`, colour / intensity / radius) spins with its light on while the servos are unlocked and the generator works
  (`GateProgressRules::ShouldBeaconSpin`), stops (light off) when the generator is down, spins during the final slide and is off
  once open.
  HUD: a line under the fire-posture badge + a world label above the gate («BLAST DOOR 37% — GENERATOR OFFLINE», «… — OPENING
  SLOWLY», «… — GRID OVERLOADED, HOLD THE WAVE», «BLAST DOOR OPENING — GET INSIDE»). HQ feed lines on: unlock («power grid is
  overloaded…»), wave cap reached, generator down, power back, last wave cleared without power, opening. Console:
  `Codex.Gate.OpenMode -1|0|1`, `Codex.Gate.ProgressPerSecond`. «Start Battle» (`CompleteChainForCombat`) still opens the gate at
  once (the squad is placed behind it at `CombatStart`, the waves spawn on the near side). Saved: `world_state.gate_progress`
  {progress, final_wave_cleared} per gate (generator health / broken were already saved with the interactables).

## Disarm orders from the cover, and the action window's lifetime (game designer 2026-10-21)

- **From the cover:** a disarm order (click on a spotted wire / live mine / rigged object, `InteractionCommands::IsDisarm`) takes the operative off the wall:
  in reach of the disarm spot the menu would open at once with no move order, so `UInteractionSubsystem::RequestInteraction` calls
  `StepOffCoverForDisarm` (real time) first - `LeaveCoverToMove`, never for what `OrderMoveTo` refuses (raging, panicking, knocked down, aboard);
  farther away `OrderMoveTo` leaves the cover as for any move. A menu opened in the cover by another path (the tactical pause, `OpenMenuNow`) is
  handled at `ConfirmActionMenu` (same helper). Check: `DisarmCoverSmoke`.
- **The window follows its object:** `UInteractionSubsystem::bMenuOpen` (not the weak `MenuTarget`) says the menu is on screen; `Tick` ->
  `CloseStaleWindows` closes it when the object is destroyed, `AInteractableActor::ShouldCloseActionMenu()` (awaiting the pickup grab; a tripped
  wire; a detonated mine) or its `bTrapped` changed since the window opened; Cancel always closes (it used to do nothing once the wire was gone).
  `OnActionMenuChanged(false)` is broadcast exactly once per window (the HUD hides `UActionMenuWidget`). A loot dialog whose crate was destroyed closes the same way.

## A fallen operative's gear: the remains on his body (game designer play-test 2026-10-26)

- **Bug:** "the fight is over, one of my fighters is down, but I can't walk up and take his equipment" (the Medic-Sapper killed in action: her
  portrait slot dimmed = out of the squad). The remains crate (`AOperativeCharacter::SpawnRemainsLoot`) was a hidden 120 x 80 x 80 cm crate box
  at the capsule's feet; the death clip lays the body up to ~1.5 m away from the capsule, so the cursor on the body met the snow (a move order)
  - `FallenGearSmoke`'s evidence lines: head 142 cm / right hand 146 cm from the capsule, the old box held 2 of her 10 main bones, the ray at
  her chest hit the floor and the click was a move order (no interaction). The box also blocked pawns / cut the navmesh, the crate was relocatable (in a wave fight the click
  answered "objects cannot be relocated during combat"), the M16 rounds were read from `AmmoInventory` (only weapons NOT in hand: 0) and the
  grenades / the weapon were missing (Godot `player.gd get_items_list` has them).
- **Now:** `ALootCrateActor::BindRemainsBody(BodyMesh, Name)` - the box is fitted to the lying body's bones (pure `Interactables/CorpseLootRules`:
  `FitBoxToBody` = 2D principal axis + padding 20 cm, min 40 x 30 x 25 cm; `IsBodyBone` drops root / ik_* / VB / attach helpers), refitted every
  0.25 s for 10 s while the death clip settles; QueryOnly, blocks only `ECC_Visibility` (the cursor), never pawns, the camera or the navmesh; not
  relocatable; no crate mesh but a label "[Click] Search the body: <name>" / "[Searched] Body: <name> (empty)" (Godot CorpseLootPrompt, 60 cm up);
  the hand cursor over it (`PlayerTick`); a search of 0.6 s then the loot window. Contents (`CorpseLootRules::BuildRemainsContents`): everything he
  carried - every hand-over item (ammo = clip + reserve), the new `ELootItem::Grenade` (`FLootContents::Grenades`, no carry limit) and his firearm
  as the bonus weapon (Godot: added to the looter only if he lacks it). Save: runtime corpse crates keep `grenades`, `bonus_weapon`, `remains_of`
  and re-bind to the nearest killed-in-action body on load. Tests `CodexTactics.Loot.Remains.*`; smoke `FallenGearSmoke`.
- **Knocked down (not dead):** the knockdown is temporary (0.6 s down in real time; on the grid until his turn's get-up). A fight that ends while he
  lies on the grid releases him: he gets up by himself in PostCombat (`FallenGearSmoke` stage 7). Taking a downed (living) operative's gear is not
  defined by Godot or the GDDs - not implemented (open question).

## Retry after a failed disarm; a visible PMC wire is noticed (game designer 2026-10-22)

- **Retry at once:** after a warning / slip the operative still crouches ~60-70 cm from the charge / the wire, so the cursor on the trap landed on
  his own body or picked him by the 1.2 m click-select radius (`ACodexTacticsPlayerController::FindClickedMember`): he was re-selected and the
  player had to walk away and come back. Now (1) a click ON a world object never picks an operative by proximity (only a direct body hit), and
  (2) `HandleWorldHit` takes a click at a live, known trap (`UsesDisarmApproach`, not awaiting its pickup) that the selected operative stands
  at (`IsInInteractionReach`) as a click on that trap - his own body or the ground within `TrapApproachRules::RetryClickRadiusCm` (1.2 m) of the
  charge / wire line, not another operative, not Ctrl (`TrapApproachRules::IsRetryClick`, `UInteractionSubsystem::FindRetryTrapAt`). Mines and
  PMC wires alike; nothing else changed (the mine's defuser lease keeps it quiet, the warned / failed state stays on the object). Tests
  `CodexTactics.Interactables.TrapApproach.RetryClick`; smokes `DeployableSmoke` (mine: warning -> body click and mine click reopen the menu, she
  does not move, the second attempt disarms) and `PMCTripwireSmoke` stage 7c (the same for a wire).
- **A PMC wire placed visible** (`bHiddenUntilSpotted` off) used to raise nothing - no hold, no `TripwireSpotted`, no tutorial card. Now it is
  noticed like a hidden one the first time an operative comes within its spot range (`ATripwireActor::RevealBy` / `Tick`: hostile and not yet
  `bSpotted`): the squad holds, the feed line, `TripwireSpotted` + `TrapSpotted` once; `bSpotted` is saved as before. `PMCTripwireSmoke` stage 7d.

## How to place a PMC tripwire (old §6)

### How to place a PMC tripwire (Demo 01 G3, 2026-10-12)

A hostile (PMC) tripwire is a level trap: hidden until an operative spots it, set off only by operatives, disarmable by anyone.
1. **Place it:** Place Actors -> search **PMC Tripwire** (class `APMCTripwireActor`, a child of `ATripwireActor` with Faction PMC,
   Armed At Level Start and Hidden Until Spotted on). Drop it at the doorway / gap; rotate it so the wire runs across the passage
   (the wire is drawn in the editor between the two handles).
2. **Anchors:** select the actor -> two diamond handles **AnchorA** / **AnchorB** in the viewport (actor space, `MakeEditWidget`);
   drag them to the two posts / door jambs (span 1-5 m, a longer / shorter one is laid anyway with a log warning). At BeginPlay each
   anchor is projected down onto the WorldStatic ground below it and the wire hangs 30 cm above (prone crawls under).
   `Anchor A/B Bracket` = a bracket on an object instead of a peg (look only).
3. **Details -> CodexTactics | Tripwire** (all per instance, defaults from `TripwireRules`): `Faction` (Squad / PMC), `Armed At Level
   Start`, `Hidden Until Spotted`; **Charge**: `Blast Damage` 140, `Blast Radius Cm` 450, `Squad Damage Share` 0.75 (operatives take
   105 at the centre, falloff 0.5), `Grenades Recovered` 2 (given to the disarmer); **PMC**: `Spot Range Cm` 450 + `Sapper Spot Bonus
   Cm` 150 (the mine rule), `Disarm Seconds Sapper / Engineer / Other` 3 / 4 / 5, `Turn Based Disarm AP` 2.
4. **In game:** hidden = not drawn, not clickable, no path cost; an operative within the spot range of the wire (any stance; outside the
   wave fight, as mines) reveals it: the squad stops (rev 5h: exploring, AUTO off -> the squad switches to Solo, "Tripwire spotted - squad
   holding. Move them one by one"; §5 «Spotted trap»), «TRIPWIRE SPOTTED!», feed line, `TripwireSpotted` + `TrapSpotted`; then it is red with a «PMC
   Tripwire» label; the squad's paths go round it ONLY in the AUTO mode (Ctrl+T, rev 5e) - a player's move order is literal and walks into it. Click it (or the ground within 40 cm of it - hand cursor, rev 5e) -> the operative walks up -> menu «Disarm (N s)»: ANY operative crouches over it for 3 / 4 / 5 s
   (sapper / engineer / others) and rolls the mine formula `DeployableRules::CalculateDefusal` crouched (+10): role base 60 / 45 / 35
   + luck / 2 - cold (-15 / -30 / -50 above 25 / 50 / 75 %) - 25 per failed attempt; warm defaults: Medic-Sapper 87.5 %, Engineer 70 %,
   Commander 57.5 % (frozen 80 %: 37.5 / 20 / 7.5 %). The mine failure rule: a dangerous attempt (< 45 %) first only warns, a miss
   slips (try again) or detonates (40 % / 80 % risk); a detonation pulls the pin (0.25 s, blast). Success: removed, +2 grenades,
   `TripwireDisarmed`. Enemies walk through it; a standing / crouched operative stepping on it trips it (`TripwireTriggered`).
   **Turn-based:** a click on the spotted wire within 2.2 m of the active operative = one attempt for 2 AP (resolved at once,
   `HandleTripwireClick`); the wire keeps ticking on the grid, so a grid walk across it trips it.
5. **Saved:** faction, spotted, anchors, arming, failed attempts / warning (a disarmed / blown wire stays gone; a hidden wire loads hidden).
6. The squad's own wire (drawer, two clicks, Medic-Sapper only, 1.5 s arming, friendly fire) is unchanged.

