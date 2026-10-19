# Combat, fire and turn-based combat

> Real-time squad fire follows Godot `player.gd`; Ctrl + click is either a targeted shot (`IssueTargetedShot`) or area quick fire (`IssueQuickFire`).
> Fire postures (Passive / Defensive / Aggressive) decide automatic fire. Turn-based combat runs on the Gorky grid (`TurnBasedCombatSubsystem`).
> Knockdown values and weapon tuning live in data (`Content/Data/Weapons/weapons_tuning.json`, `Codex.Knockdown.*` CVars).
>
> Key files:
> - `Combat/TargetedShotRules`, `QuickFireRules`, `Characters/FirePostureRules`, `Combat/CombatFeedbackSubsystem`, `HealthComponent`, `Tactics/TurnBasedCombatSubsystem`, `GorkyGridManager`
> - `Characters/OperativeCharacter{Combat,Weapons,TargetedShots,Aim}.cpp`, `KnockdownComponent`; design `docs/design/QUICK_FIRE.md`
> - Smokes: `TargetedShotSmoke`, `QuickFireSmoke`, `PostureSmoke`, `SquadFireSmoke`, `KnockdownSmoke`, `TurnBasedSmoke`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Combat modules (old §5)

- `Combat/SpaceInput`, `EncounterQueries`, `HealthComponent` (armour, elements, `ApplyDirectHealthLoss`),
  `WaveSubsystem`, `EnemySpawnPoint` (Gemini). `Characters/EnemyCharacter` + `EnemyAIController` (Gemini, tag `Enemy`).
- `Tactics/GorkyGridManager`, `Gorky17Types` — grid, arc zones, AP BFS, A* (Gemini). Turn-based manager NOT yet.
- `Data/CombatTypes`, `WeaponDataAsset`, `EnemyArchetypeAsset`, `WaveConfigTypes` (Gemini; values hand-typed — see §9).

## Targeted shots, quick fire, combat feedback (old §5)

- Ctrl + click targeted shots: `ACodexTacticsPlayerController::IssueTargetedShot` (Godot `main.gd` Ctrl branch:
  enemy → `SetManualPriorityTarget`, barrel / mine / crate / trapped object → `AOperativeCharacter::ShootAtObject`;
  in the tactical pause → `PlanTargetedShot`, run by `USquadSubsystem` on release before the planned moves).
  `Combat/TargetedShotRules` = mine hit chance clamp(max(10, Accuracy − cold·0.25)·stance − m·perMetre, 0, 95),
  stance 0.6/4.0, 1.0/1.8, 1.25/1.0; miss reasons. `Accuracy` per role 90 / 75 / 85 (roster). The priority target is
  used first by `FindBestCombatTarget` while alive, in range and in the line of fire (barricade blocks prone only).
  Godot `player.gd shoot_at_*`, `calculate_mine_shot_hit_chance`, `set_manual_priority_target`, `_find_shoot_target`.
- **Quick fire = Ctrl + click on a point** (game designer 2026-10-19; Ctrl + click by the user's decision the same day, UE-only;
  `docs/design/QUICK_FIRE.md`): `HandleWorldHit`'s Ctrl branch -> `ACodexTacticsPlayerController::IssueCtrlClick` (Orders unit): a shootable
  actor (`ClassifyShotTarget`: live enemy, barrel, mine, crate, trapped object) or a visible live enemy within 1.5 m of the point
  (`QuickFireRules::PickTarget`) = `IssueTargetedShot` exactly as before; a squad mate = its hint; anything else (and an unseen enemy's body)
  = `IssueQuickFire`: the selection (group, else the leader) gets `AOperativeCharacter::OrderQuickFire(Point)` - area fire
  (`FireQuickFireRound`: stance spread, capped at the weapon range, the visibility trace without the squad, an enemy crossed - seen or not -
  hit by its chance x 0.5, a barrel / mine / trapped object hit by the round goes off), M16 3 rounds / single-shot weapons 1 at the weapon's
  cadence, turn into the aim cone first, the sniper kneels, from cover the corner shot (`IsCornerShotTarget`: the cover pose, the point =
  the threat side in `GatherCoverThreat`) else the open shot. Runs inside `RunCombatShooting` before the posture / exploration gates.
  Pause: `PlanQuickFire`, executed first by `USquadSubsystem::HandleTacticalPauseReleased`; turn-based: Ctrl + click stays the grid attack;
  tutorial lock `Attack.QuickFire`. RMB never fires (drag = camera, minimap = move). Feedback `SpawnFirePing`, «Fire!».
  Tests `CodexTactics.Combat.QuickFire.*`, `QuickFireSmoke`.
- `Combat/CombatFeedbackSubsystem` + `CombatFeedbackActor` — shot tracers with muzzle flash (operative: weapon
  `TracerColor`, glow / fade by damage type, misses deflected; turret: green), cyan plan markers of the tactical pause
  (planned moves, interaction approach, relocation / deploy targets, targeted shots; cleared on every combat-mode
  change), Ctrl + click target flash (red light + glowing overlay on the target's meshes). Glow material
  `/Game/VFX/Materials/M_CombatFeedback` (unlit additive, `Color` × `Intensity`) from
  `Scripts/Editor/create_feedback_material.py`. Effects run on world time (slow down in the pause, as Godot tweens).
  Godot `_spawn_muzzle_tracer` (player / turret), `_spawn_waypoint_marker`, `_clear_planned_markers`,
  `_highlight_target_feedback`.

## Fire postures / ROE (old §5)

- **Fire postures / ROE (2026-10-06; semantics rev 2026-10-19, game designer play-test):** `FirePostureRules` (tests `CodexTactics.Squad.Posture.*`), per operative (`GetFirePosture`: override, else the squad's). **Passive** = never opens fire on its own (only direct orders: Ctrl + click, blind fire, planned shots, grenades). **Defensive** = returns fire / engages only the enemies that attack or have detected the squad: per target `MayAutoFireAt` - an enemy that hunts the squad (`AEnemyCharacter::IsHuntingSquad`: off patrol duty with a target / an alarm point / the horde's knowledge) is fair game; once the operative is attacked (`bProvokedThisFight`, `Codex.Posture.DefensiveSquadWide`) any enemy for that fight; it never fires at a patrol that has not spotted the squad and throws no autonomous grenade unprovoked. **Aggressive** = fires at any enemy in sight; in EXPLORATION it opens fire where contact starts the fight - an ambush level OR a level with enemies placed in the map (fix 2026-10-19: the gate asked `IsAmbushCombatStart()` only, but the Demo 01 lake is a «button» level whose placed patrols start the fight on contact - `ShouldStartFightOnContact`; so the squad saw them and never fired) - through the normal start path (`ULevelEncounterSubsystem::NotifyHostileContact(SquadAutoFire)`, which still refuses a non-placed enemy on a «button» level). `AOperativeCharacter::MayAutoFireAtEnemy` filters the Commander Mode target and the visible enemies in `FindShootTarget`; `MayAutoFireNow` (provoked) gates the autonomous grenades / the turret gunner. Keys `,` `.` `/` (selected), Alt = squad. Smoke `PostureSmoke`.

## Turn-based combat (old §5)

**Turn-based combat** — `Tactics/TurnBasedCombatSubsystem` (starts when the flow enters TurnBased, ends on victory /
defeat / leaving): 14 x 14 grid (1.5 m) around the leader, floor traced under it, static colliders baked as obstacles,
barricades / barrels / mines / turrets registered, everything else frozen (enemy / turret / mine ticks off, wave
spawning paused in `UWaveSubsystem::Tick`). Squad round: 8 AP each — move 1 / diagonal 2 (reachable BFS, a mine on the
path stops the walk and blows), stance 1, turn 90° 1, one attack 3 (fire lane, LoS, hit chance with stance bonus, arc
damage − armour, barrel = 3 x 3 blast + 3 burning rounds with a 5 x 5 fire-fear area), Tab next operative, Enter
end the squad turn. Turret phase (nearest enemy ≤ 45 m, fallback curve, 25 dmg). Enemy phase: nearest operative, path
to an orthogonal neighbour (fire-fear aware), bite for 2 AP after a 1.3 s yellow warning (arc x stance multiplier),
step back. Steps animate 0.52 s (enemy 0.48 s, x1.414 diagonal). Victory → «🏆 ПОБЕДА…», flow → tactical pause.
`UI/TurnBasedHudWidget` (bottom-right panel «ХОД ОТРЯДА» / «ХОД ПРОТИВНИКА», unit, AP / HP, end squad turn, next
operative, stance, turn; barrel push disabled — Godot `gorky17_combat_hud.gd`). `Tactics/TurnGridOverlayActor` (instanced glow tiles: grid, reachable, attack targets, enemy reach, active, warning,
fear). Controller: clicks go to `HandleWorldClick`; 1..3 / Z C V / R / Tab / Enter as above. Grid manager gained
`FindPathToAdjacent` / `FindPathClosestOutsideForbidden`; barrels `IgniteForTurnBased` / `ExtinguishNow`.

## M16 range 25 m and weapon tuning data (old §6)

- **M16 range 25 m (user decision 2026-10-11; was Godot rifle_m16.tres 14 m).** `Content/Data/Weapons/weapons_tuning.json` "m16"
  (master copy, applied at StartPlay; `Scripts/Editor/apply_weapon_tuning.py` [`CODEX_WEAPONS=m16`] writes it onto
  `DA_Weapon_m16` so the Details match): `attack_range_m` 14 -> 25 (prone x1.35 = 33.75 m, crouched x1.15 = 28.75 m, high
  ground x its multiplier), `max_range_cells` 5 -> 8 (turn-based; the sniper keeps 30 m / 10 cells), `base_hit_chances` per
  1.5 m cell (real time reads it by round(m / 1.5), clamped to the last entry) 0.95 0.85 0.75 0.65 0.5 -> 0.95 0.85 0.75 0.65
  0.5 0.5 0.5 0.5 0.5 0.47 0.44 0.41 0.38 0.35 0.32 0.29 0.26 (no change inside the old 14 m; 14-25 m falls to 0.26),
  `distance_damage_multipliers` 17 x 1. Everything else derives from the weapon: auto fire / fire postures
  (`EvaluateShotLine`), blind fire, autonomy target reach (x1.3), the bot's rifle range, the turn-based attack pattern and
  overlay, the operative vision cone. Not changed: `SquadAutonomy::ThreatRangeCm` 25 m, `CoverDecisionRules::BlindFireMaxRangeCm`
  25 m, the turret 12 m. Gameplay note: the marksman's 20-35 m band (`enemy_weapons.marksman_rifle.preferred_*`) is now partly
  inside the squad's reach (standing 25 m, prone 33.75 m) - tune it in the Wave Editor if he becomes too easy.

## Knockdown values (old §6)

**Knockdown values (user decisions 2026-10-11; `FKnockdownConfig` defaults + `Codex.Knockdown.*` CVars, no DataAsset).** Chance `KnockdownChance` 0.5 (`Codex.Knockdown.Chance`) for everyone: a qualifying heavy hit (>= 40 HP, Brute only on a crit) knocks down only when the roll (`FKnockdownHit::ChanceRoll`, rolled in `UKnockdownComponent::TryKnockDown`) is below it; a blast within 2.5 m, a pounce (hound run-up pounce, cutter leap landing) and the Frost Brute's heavy blow (user answer 2026-10-11: guaranteed, like the pounce; its qualify rules unchanged) always knock down. Durations: fall = clip (0.87 / 0.93 s), `DownedSeconds` 0.6, `GetUpSeconds` 0.8 at up to `GetUpMaxPlayRate` 2.1x (Revive_Back 0.8 s, Revive_Front 1.19 s) -> ~2.27 s on the ground (was ~3.35 s). Downed damage: one `DownedDamageMultiplier` 1.33 (`Codex.Knockdown.DownedDamage`) for any blow — shots, melee, blasts / traps — real time (`AOperativeCharacter::TakeHit` incl. its bypass path, `UHealthComponent::TakeDamage`) and turn-based (`ApplySquadHit` -> `TakeHit`, rounded); `EKnockdownBlow`, `DownedRanged/MeleeDamageMultiplier` and their CVars are gone. Turn-based: never a free stand-up; on his turn a lying operative may stay down (x1.33, no move / shot / stance / facing / barrel / relocate / deploy; items, unit switch and ending the turn work) or order «GET UP (2 AP) [Z]» (`UTurnBasedCombatSubsystem::GetUpActiveUnit`; the panel entry replaces the stance button only while he lies — falling / getting up the stance entry stays, disabled, so no empty GET UP button flashes); with < 2 AP it is refused with a feed line and he stays down. Enemies keep spending their whole turn on the get-up (`GetUpForTurn`). Smokes needing a guaranteed knockdown set `Codex.Knockdown.Chance 1` and restore it.

