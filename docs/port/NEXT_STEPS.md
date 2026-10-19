# Next steps (moved from the old HANDOFF.md)

> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19). Index: [HANDOFF](HANDOFF.md) | [system docs](../systems/README.md) | [CHANGELOG](CHANGELOG.md)

## 8. Next steps (in order)

- **Demo 01 rev 5 (2026-10-13):** user: place / re-id the volumes and the two new actors (§6 «Tutorial» rev 5 block; beat sheet §10.1),
  answer §7 «Demo 01 rev 5». Phase 2 (beat sheet §11): encounter groups (§1.3: `EncounterGroup` on `AEnemyCharacter` /
  `APatrolRouteActor`, `ULevelEncounterSubsystem` skirmish state, `UWaveSubsystem::AdoptLevelEnemies` filter, level JSON `encounters`,
  `EncounterStarted / Cleared` events, `EncounterSkirmishSmoke`), A4 fight steps, A5 trapped crate steps (+ `TrapDefused` event), A6
  defence prep steps (+ `ObjectTrapped`, relocation kind, `HighGroundTaken`). Phase 3: A7 canister, A8 pause, A9 AUTO, A10 generator
  repair + guard (subject on `Squad.Guard`), debrief. Then TANDEM #17 (all rev 5 text ids) and #18.

- **APC (2026-10-12, branch `agents/apc`):** user: run `Scripts/Editor/setup_apc_vehicle.py` with the editor closed (or make
  `BP_APC_BTR82` by hand, §6 «APC»), build `L_VehicleTest` (§6), answer §7 «APC P1». Next phases: P2 turret (gunner seat, posture
  auto-fire, Ctrl+click target, ammo, ABP aim), P3 turn-based (vehicle as a multi-cell occupant, parked turret 3 AP, get out 2 AP),
  P4 damage **done 2026-10-12** (hull HP / armour, disabled + Engineer repair, ejection, mines, object shots; §7 «APC P4»), P5
  tutorial beat V (steps JSON + texts, `VehicleDamaged` / `VehicleDisabled` / `VehicleRepaired` ready) and the remaining design
  items (engine auto-off, fuel option, path ribbon, wheel damage, turn-based blasts on the hull).

- **Sprint 15 heat (2026-10-11), waiting for the user:** confirm the design decisions (§7 «Sprint 15 heat»), look at
  `Saved/Screenshots/Phone/heat_sources.png` (main folder; placeholder shapes); Gemini: VFX / materials (TANDEM request #15). Open:
  gas canister refill item, flamethrower hook (`AFirePuddleActor::SpawnAt`), night accuracy (no night system), flares in turn-based combat.
- **Knockdown tuning (2026-10-11), waiting for the user:** branch `agents/knockdown` (worktree `../CodexTactics-agents3`) merged to main (9da272b); clips `Saved/Screenshots/Phone/knockdown_faster_getup.mp4` / `knockdown_tb_getup.png`. Answered 2026-10-11: the Brute blow is guaranteed again (done), no shooting while lying in turn-based for now.
- **Sudden horde start (2026-10-11, reworked), waiting for the user:** watch `Saved/Screenshots/Phone/horde_intro_flyover.mp4`
  (main folder); tune with `Codex.HordeIntro.*`, then move the numbers into DA_GameBalanceConfig. Open: a bottom-HUD effect on
  `OnHordeIntroStarted`; the 4-min mid-fight horde (`UHordeSubsystem`) keeps its old behaviour; on L_MovementTest the spawn points
  surround the test start (smokes / PhoneShots put the squad 30 m away: `SmokeUtils::PlaceSquadAwayFromWaveSpawns`).

- **Sniper / Female Soldier (2026-10-09), waiting for the user:** assign a real sniper model on `DA_Weapon_sniper_rifle` (`HandMesh`
  + grip offset); tune `weapons_tuning.json` "sniper_rifle"; optionally wire the sniper aim offsets (§6 «Female Soldier + sniper»);
  Eyeball in PIE: the
  M16 stand-in pivots with hand_r during the bolt clip (the pack animates its own rifle).

0. **Bot training (user decision 2026-10-06) — done 2026-10-07 (see §10 «Stealth playtest bot»), waiting for the user:**
   approve or reject `Saved/Coach/stealth_proposal.md` (proposed `Codex.Perception.SightRangeScale` 0.9; apply with
   `python Scripts/Tools/jev_ai_coach.py --apply-proposal Saved/Coach/stealth_proposal.json`). Open: the squad has no
   mines in exploration, so the bot's trap opener never fires and trap searches are untested by the coach (give the
   squad a mine on patrol levels or place tripwires on L_PatrolTest?). Since the frontend (2026-10-08) there is no paused
   in-level start menu: on wave maps the bot explores in running time before it calls `StartMission(Combat)` (wave
   telemetry from before 2026-10-08 is not directly comparable).
1. ~~Loot crates~~ — done (see §10).
2. ~~Turrets~~, 3. ~~generator damage / repair~~ — done (see §10). Enemies attack turrets / barricades / the generator
   since the enemy AI pass (see §9 Enemy AI).
4. ~~Ctrl + click targeted shots~~ — done (see §10).
5. UI shell from REFERENCE_PLAYTHROUGH: ~~objective banner, mission failed + Ctrl + X, start menu~~ (done, §10);
   ~~dialogue window, bottom action bar, banners, cutscene card~~ (done); remaining UI: ~~inventory drawer~~ (done: `UInventoryDrawerWidget`, H / J / K / L, `UsePersonalItem`), ~~transfer~~ (done: `UTransferDialogWidget`, `USquadTransferSubsystem`, `TransferRules`),
   ~~guard~~ (done: T / «ОБОР», `USquadSubsystem::ToggleGuard`, `AOperativeCharacter::bGuarding`), ~~weapon selector~~ (done), ~~pause menu / save-load~~ (done: `UPauseMenuWidget`, `USaveLoadDialogWidget`, `USaveGameSubsystem`, F5), ~~radius rings~~ (done: `URadiusRingSubsystem` + `ARadiusRingActor`, M_CombatFeedback segments). §8.5 is complete; next: §9 gaps.
6. Turn-based combat manager on the Gorky grid (Godot `Scripts/tactics/turn_based_combat_manager.gd`).
   Done: `GorkyLineOfSight`, `TurnBasedRules`, `UTurnBasedCombatSubsystem` + overlay + controller input (see §4).
   Exposed zones done (`FExposedZones` pure rules + `UpdateExposedZones` at the end of the squad phase, overlay
   outlines `ATurnGridOverlayActor::SetExposedZones`, reinforcements via `UWaveSubsystem::SpawnEnemy`, armor 2, facing
   north, frozen outside their turns). Barrel / turret relocation done (`StartRelocate` / `RelocateObject` /
   `TryPushAdjacentBarrel`; click on an orthogonally adjacent barrel or a Chebyshev-adjacent turret picks it up, Ctrl +
   barrel shoots (Shift until 2026-10-06), Esc / RMB / click on its own cell cancels, panel «📦 Бочка»). Barricade relocation done
   (`GetBarricadeCellsAt` / `CanPlaceBarricadeAt` / `RelocateBarricade`; footprints on the grid now use Godot's five
   samples along the long axis, `RegisterBarricadeCells`; Q -45°, E / R / wheel ±45° while relocating; targets within
   2 cells of the operative). Deployables on the grid done (`CanPlaceDeployable` / `DeployObject` /
   `HandleDeployPlacement`: the action-bar / F placement ghost works in turn-based, a click sets the item up on the cell,
   walking up first if needed; Q / E rotate the ghost). Weapon switching done (arsenal below). Grenades done (`UGrenadeSubsystem` aim /
   throw, `AGrenadeActor` flight / fuse / blast, `GrenadeRules`; G, the selector's grenade line, LMB throws, RMB / Esc
   cancel; in turn-based combat the grenade is a grid weapon like in Godot, entering it refunds grenades in flight).
   Stasis look done (M_TacticalStasis from Scripts/Editor/create_stasis_material.py on every mesh of the enemies left
   outside the fight, restored at the end). Companion drone: Godot _execute_companion_drone_phase only emits a request
   that main.gd completes at once (no drone exists) — nothing to port. §8.6 is complete; next: §8.5 UI (inventory
   drawer, transfer, guard).
7. Phase 2 data importer (JSON / .tres → DataAssets) replacing hand-typed values (§9).
   Done: weapons — `Scripts/Editor/import_weapons.py` → `/Game/Data/Weapons/DA_Weapon_<id>` (all 9; defaults parsed
   from weapon_data.gd, enums mapped by name, `EStatusEffect::Shocked` appended); the game mode equips
   `StartingWeapon` (DA_Weapon_m16) with `StartingReserveAmmo` 60 (Godot _init_weapons) → HUD «[30/60]» like the video.
   Balance — `Scripts/Editor/import_balance.py` → `/Game/Data/Balance/DA_Balance` (balance.tres) and
   `DA_GameBalanceConfig` (game_balance_config.tres). **Since 2026-10-01 (user decision) Unreal is the master copy:** the
   assets are `UGameBalanceConfig` — one typed field per Godot export (364, Godot names, Categories = the Godot
   @export_group / @export_subgroup sections, ## comments as tooltips, @export_range as sliders / clamps), generated
   into `Public/Data/GameBalanceConfig.h` by `Scripts/generate_balance_config.py` (plain Python; re-run when Godot adds
   parameters). `UGodotBalanceAsset::GetNumber` / `SetNumber` reach the fields by name via reflection (then `Numbers`
   for keys without a field), so every system reads the tuned values. `import_balance.py` creates missing assets from
   Godot once; afterwards it only writes a Godot-vs-Unreal difference report (`CODEX_BALANCE_REIMPORT=1` overwrites).
   Rule tests use `CodexTacticsTests/Private/GodotBalanceFixture.h` (Godot values), not the tunable assets. Wired so far: turn-based combat
   (`TurnBasedRules::BalanceFromGodot(DA_Balance)` + step durations, loaded at combat start via the game mode's
   `TurnBasedBalance`), operatives (`Characters/OperativeBalance::Apply` = Godot apply_balance_config: max health
   commander 140 / engineer 150 / medic 120, matches, walk / run / crouch speeds, acceleration, sprint / lift / carry /
   wound values, commander prep radius, cold rules — rates, stance multipliers, misfire / freeze / aim; applied on
   the deferred spawn), enemies (`AEnemyCharacter::ApplyBalance` = Godot enemy apply_balance_config: crit chance /
   multiplier per type — hound 0.15, spitter 0.25, brute 0.30, frostbitten 0.10 instead of a flat 0.20 — and
   hound / spitter / brute health, speed, damage, range, cooldown). Deployables need nothing: Godot reads the config
   only when the inspector value is 0, and the UE values equal the Godot export defaults (mine 120 / 3.5 m, turret
   120 HP 16 dmg 0.45 s 12 m, barricade 200 HP). Camera NOT wired on purpose (UE distances were tuned with the user;
   Godot distance semantics differ) — ask before changing.
   Levels — `Scripts/Editor/import_levels.py` → `/Game/Data/Levels/DA_Level_<file>` (level_01_outpost + stage_01..12;
   waves with `is_active: false` dropped like main.gd). The game mode's `LevelConfig` (DA_Level_level_01_outpost = Godot
   main.gd active_level_json_path) goes to `UWaveSubsystem::SetLevelConfig` and, through `LevelFlowRules::ApplyLevel`, to
   the flow config: preparation 60 s, rest 20 s, TotalWaves = 3 (Godot max_waves = active waves; without a level Godot
   uses preparation_phase_duration / max_campaign_waves from game_balance_config.tres). L_MovementTest has four
   `AEnemySpawnPoint`s beyond the gate (NORTH_GATE, WEST_FLANK, EAST_FLANK, FAR_PERIMETER; added by
   add_level_objects_to_movement_test.py, following the Floor transform). Lane matching = Godot _get_enemy_spawn_pos
   (substring either way, else any point). Spawning = Godot _spawn_custom_json_wave (see §7).
   Source trap: Godot has TWO GameBalanceConfig files with different values. The turn-based manager loads
   `resources/balance.tres` first (squad 8 AP, enemy 6 AP — the UE `FTurnBasedBalance` defaults), while camera, enemies,
   turrets, mines, barricades load `resources/game_balance_config.tres` (e.g. tactical_squad_max_ap = 3, enemy 4,
   turret_shot_delay 1.0). Import both and wire each consumer to the file its Godot counterpart loads. Weapons:
   `resources/weapons/*.tres` → `UWeaponDataAsset` (same field names). Levels: `data/configs/levels/*.json`.
8. Content: level, VFX, cutscene; character "twisted" look issue (§6).

