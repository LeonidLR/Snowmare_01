# Known gaps / tech debt (moved from the old HANDOFF.md)

> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19). Index: [HANDOFF](HANDOFF.md) | [system docs](../systems/README.md) | [CHANGELOG](CHANGELOG.md)

## 9. Known gaps / tech debt

- **Deliberate deviations (user decisions 2026-10-26, branch `gameflow/user-answers-1026`; the open questions of that day CLOSED):** (1) "the
  tactical pause only by Space": the grid's own end goes to real time (Godot `_exit_turn_based_combat`: a 20 s pause) and the tutorial never pauses
  (the A4 `tactical_pause` action retired); (2) no free first pause of a wave - the wave's first Space is an ordinary pause (UE-only feature of
  2026-10-11 removed); (3) the group cover order runs in the fight too (Godot `is_tactical_mode` gave individual control in the fight); (4) no gear
  from a knocked-down, living operative (kept); (5) a fallen operative's remains hold the Godot `get_items_list` kinds (ammo, medkits, food,
  grenades, the weapon) - UE keeps the ammo of EVERY weapon he carried (Godot had one weapon); (6) the squad's corner peek (the peeker stands at
  a high wall, the others crouched) confirmed. Details: `waves-horde-encounters.md`, `squad-and-orders.md`, `interactables-traps-loot.md`,
  `combat-and-fire.md`.
- **`CoverSmoke` "in the right-hand corner cover after 12.0 s (556 cm off the slot)" fails on main 804d64c2 (found 2026-10-27, branch
  `formation/wall-hug-gap`; NOT caused by it - identical with `-PreCmds "Codex.Formation.WallTraceBlocking 0,Codex.Formation.WallSnap 0"`).**
  After the left corner the Commander's cover order to the wall's other end is accepted (`to the high cover at (124, -6338) at a sprint`) but
  he never walks (`cover slot not reached (556 cm off), stays in the open`); every right-corner check after it fails in turn. The log shows the
  formation running round him (`corner watch`, `rear guard: Z (column)`) - to look at with the 2026-10-26 formation-in-the-fight / group cover
  in combat changes (bc3a4a03).
  **Investigation so far (2026-10-28, branch `fix/coversmoke-regression`, WIP - stopped on the user's request, no fix yet):**
  - CVar A/B on efbda183 (`smoke_parallel -Commands CoverSmoke -NoRetry -PreCmds ...`): `Codex.Debug.MoveTrace 1` alone -> FAIL (same 556 cm);
    `+ Codex.Squad.CombatFormation 0` -> **PASS** (157 s). `Codex.GroupCover.InCombat 0` not run yet. So the trigger is the squad keeping its
    formation in the fight (3ceb5629 / dbb4b247, merged 90d7f281), not the group cover in combat.
  - Evidence (`Codex.Debug.MoveTrace 1`): the leader's path to the right-hand slot is fine (`result 2, 2 points, length 527, partial 0`), then
    `move completed: Blocked[Blocked], 556 cm from the goal, velocity 0` ~2.9 s later; the next cover order (to the wall's middle) ends the same
    (`Blocked`, 296 cm). The leader is physically blocked from the start of the walk - most likely a follower who now walks into his formation slot
    beside the leader at the wall (the smoke parks the others 4 m behind; with the formation off in the fight they stayed there) stands on the
    leader's way along the wall face.
  - Next: log the followers' positions at the order (or run with `Codex.Formation.Sectors 0`), confirm the blocker; decide between the game fix
    (a follower in the leader's way along a cover walk should step aside / the leader's own cover walk should not be blocked by followers) and the
    smoke fix (CoverSmoke is a single-operative cover check: Solo on / park the others for good). Then git bisect only if the A/B is ambiguous.

- **`SuppressionSmoke` scene A can flake under load (seen once in a batch 2026-10-26, passed alone; NOT the combat formation).** The log
  (main checkout, 18:57:39): the marksman's second shot came 0.5 s after his first and hit the Commander -> the Commander himself became
  SUPPRESSED (level 0.70), his SUPPRESS rounds went blind ("[suppressed: blind]", none reached the marksman), the marksman's level decayed to
  free (0.38) before the Engineer reached the flank. The Commander is the leader and on his own fire order; the followers' movement plays no
  part. To harden: make the commander immune to suppression in scene A, or retune `TuneMarksman` so two hits cannot land 0.5 s apart.

- **`PauseBarrelOrderSmoke` "B's paused push ran: barrel 2 stands on the marked spot" fails (found 2026-10-26, branch
  `gameflow/realtime-horde-squad-combat`; NOT caused by it - it fails the same with every old rule restored:
  `-PreCmds "Codex.Relocation.RealTimeCombat 0,Codex.Squad.CombatFormation 0,Codex.HordeIntro.AutoPause 1,Codex.HordeIntro.LaterWaves 1"`).**
  The Engineer's walk up to barrel 2 (`URelocationSubsystem::TickTask` stage 1, `GetApproachPoint`) is "move to (-194, 1227) -> unreachable"
  every retry on the current L_MovementTest; moving the target / the barricade does not change it. Every other check of the smoke passes, the
  new real-time relocation stages included. To look at: the barrel's spawn spot vs the map's navmesh (the barrel / barricade nav areas).

- **(FIXED 2026-10-18: close-in ON by data, ai_tuning.json; §7 lists its follow-ups.) Melee enemies stopped short of their bite
  (found 2026-10-18, bug hunt; `Codex.Enemy.MeleeCloseIn`).** The close-in move is `MoveToActor(AttackRange x 0.5)` and UE's reach test adds
  both capsule radii: a hound / frostbitten (range 180, radii 40 + 40) stops 1.65-1.75 m from the operative, while
  `EnemyAIRules::CanMelee` (3D to his centre 1 m up) needs <= 1.5 m planar. They stand there without biting and after 3 s
  give him up ("no way to BP_Operative_C_0, picking another target", 6-19 times per CornerHoldSmoke run) and run for another
  target (the generator 80 m off); bites only happen when they overshoot. `Codex.Enemy.MeleeCloseIn 1` uses
  `EnemyAIRules::MeleeApproachAcceptance` (hound 50 cm -> stops ~1.3 m off and bites): CornerHoldSmoke then fails massively
  (hound pounces knock the commander out of his corner every phase), i.e. a balance change - shipped by data 2026-10-18, the smoke adapted (§10).
  CornerHoldSmoke's flank phase counts flankers that ran off this way (`FlankGone`) so it does not depend on it.
- **Smoke reliability (cleanup day task 2, 2026-10-15): known flaky list EMPTY** (`Scripts/test_map.json` `knownFlaky`).
  Proof: the 11 target smokes 3 x with `-Jobs 3 -NoRetry` (fixed step) 33 / 33, once on the wall clock (`-RealTime`) 11 / 11,
  a 30-smoke regression set 30 / 30, all 123 smokes on the fixed step (before the last fixes) with only the ones fixed
  below failing. Causes -> fixes (test-only unless marked GAME):
  - `PanicSmoke` (failing): the sudden horde (55, random spread round the squad) parked only 50 m under the floor still
    repelled Susanin in 2D (panic flight = away from every enemy within 20 m in 2D) -> towards the test hound ->
    `SmokeUtils::ParkEnemyFarAway` (1 km away in 2D too); also `GroupSelectSmoke`.
  - `SquadControlSmoke` (failing): stale - rev 5e replaced "Alt + V refused while moving" by prone on the move -> checks the
    dive / stop-then-prone and waits for the walk to start (condition).
  - `VisionConeSmoke` (failing): stale - BP_Operative saves `bVisionConeRangeFromWeapon` = false (fixed 14 m cone) -> expects
    the Blueprint's way, checks the weapon way with the flag flipped.
  - `CornerHoldSmoke` (230-410 s, rerun): wall-clock length (~200 s of horde phases) + "3 of 5 flank hounds down in 20 s"
    was hit / crit luck -> fixed step (~55-65 s wall with 3 jobs), the flank phase runs until 3 are down (cap 45 s),
    timeout 600 s for heavy CPU load.
  - `TurnBasedAimSmoke`: auto reload on leaving turn-based zeroes the aim offset (condition wait now); the AimYaw travel
    budget gets one full swing per combat retarget (30..77 deg/s seen for the same behaviour); per-frame metrics -> step 240.
  - `KnockdownSmoke`: a brute frozen with `CustomTimeDilation 0` still struck on its first tick (DeltaTime 0, AttackTimer 0)
    -> its tick is disabled until the scripted blow.
  - `CoverSmoke`: his own cover AI re-took the old slot after the smoke's LeaveCover, the order became a 5 m shimmy ->
    waits for arrival at the slot.
  - `LevelWaveSmoke`: "on the move after 6 s" with a 55-enemy horde through the async path queue -> condition (cap 20 s);
    wave 2 spacing checked on the spawn frame (0.5 s later two running hounds brushed at 75 cm).
  - `GroupSelectSmoke`, `EnemyTacticsSmoke`, `TurnBlastDeathSmoke`: fixed waits for async walks / a fall-back / the
    turn-based shot timeline -> conditions with generous caps.
  - `FacingSmoke` (on the fixed step): a hound running off after the 3 s "no way" skip (melee stand-off gap below) counted
    steering wiggles as tremor -> sampled from its arrival, below 250 cm/s.
  - `SaveRoundTripSmoke` (on the 30 fps step) - **GAME**: mine / tripwire arming countdowns went below 0 by one frame, were
    saved negative and loaded as 0 (round trip not exact) -> `DeployableRules::StepArmingCountdown` clamps (test
    `CodexTactics.Deployable.ArmingCountdownNeverNegative`).
  - Clock overrides (`smokeOptions`): `TutorialSmoke` realTime (dialogue Enter hold is wall-clock timed by design);
    `TurnBasedAimSmoke` / `CoverMoveSmoke` / `SniperSmoke` fixedStepFps 240 (per-frame thresholds).
  - Still `knownFail`: `CombatMoveSmoke` (unchanged, not part of this task).

- **Demo 01 rev 5 (2026-10-13):** (1) FIXED 2026-10-15 (cleanup day 2): the `VisionConeSmoke` 1400 cm failure was a stale expectation -
  BP_Operative serialises `bVisionConeRangeFromWeapon` = false (the only bool it overrides; the C++ default is true), i.e. the
  designer keeps Godot's fixed 14 m cone; the smoke now expects the Blueprint's way and checks the weapon way with the flag flipped. (2)
  `UNavigationSystemV1::FindPathToLocationSynchronously(.., UNavFilter_NoCrawl)` returned a FULL path through a crawl-only strip in
  `BridgeFenceSmoke` while the operative's own `RequestMove` with the same filter ends before the fence (the behaviour that matters);
  the smoke logs the query result as info only - investigate the filter class handling of the synchronous query. (3) The bridge keeps
  no memory of a refused operative's destination: a box-selected group ordered across stops at the edge and must be re-ordered
  (formation followers continue on their own); option: re-issue `LastMoveDestination` when admitted. (4) The crawl-only area has
  cost 1: a prone body goes straight under; standing bodies path round when the fence is open at the ends (design: close the line).

- **Sprint 15 heat gaps (2026-10-11):** no flamethrower in the arsenal (puddle hook `AFirePuddleActor::SpawnAt` unused but by the
  barrel); no night / darkness system (the flare's "removes night accuracy penalties" has no hook); no gas canister refill item; the stove
  is not a drag & drop item (it moves through F hand-overs only); flares cannot be thrown in turn-based combat; the bot does not use the
  new items; heat items are not shown on the HUD squad panel (drawer only).
- **Melee chasers stop just outside their bite (found 2026-10-11, pre-existing, NOT changed - gameplay decision):** the chase is
  `MoveToActor(Target, AttackRange * 0.5)` with `bStopOnOverlap` (the reach test adds both capsule radii), so a hound (1.8 m
  range) is "at the goal" at about 1.65-2 m centre to centre, while `EnemyAIRules::CanMelee` measures feet to the target's
  Godot position 1 m higher (3D <= 1.8 m, i.e. <= 1.5 m flat). Unpushed, it stands there; after 3 s `no way to …` skips
  the target for 8 s (EnemyTacticsSmoke: ~10 such retargets in 10 s, and its side / rear check relies on them). With the
  re-path throttle's first version (no per-tick already-at-goal ask) hounds walked on into melee and pounced; the parity
  rule keeps the stand-off. A fix (e.g. acceptance from the flat melee reach, or the reach test without the radii) is a
  gameplay change and re-tunes EnemyTacticsSmoke / the pounce rate: ask the user.
- Enemy spawn points follow Godot exactly: L_MovementTest points carry the Godot lane names («Северные ворота»,
  «Левый фланг (Прорыв)» HOUND only, «Правый фланг» SPITTER only, «Дальний периметр») while the level JSON uses
  NORTH_GATE / WEST_FLANK / EAST_FLANK — in Godot these never match, so laned entries spawn at a random static point
  and only "ANY" entries honour the type filter. Renaming `SpawnLane` on the points (or the JSON lanes) makes lanes
  work; left as in Godot. No Godot level has a dynamic breach point (`bIsDynamic`), so the breach code is dormant
  until a designer places one; `ActiveWaves` / `WarningLeadTime` are exported but unused (like Godot). The breach
  floats «ЧЁРТ ПОДЕРИ» over the leader like Godot.
- Susanin (`URecruitSubsystem`): spawned from the operative Blueprint with `ACodexTacticsGameMode::RecruitSusanin`
  (pink tint via BodyColor — the user's BP decides the real look; Godot swaps the Explorer_Coat material). The spot is a
  TargetPoint tagged `SusaninSpawn` on the west side of the yard (Godot uses a hard-coded (-8.2, 18.0) in its own
  layout). The narrative pause uses the minimum world time dilation, restored with `UGameFlowSubsystem::ApplyTimeDilation`.
  Rage uses the general keys (see Rage below); panic uses the susanin_* stress keys (ordinary susceptibility, user
  decision 2026-10-01). His death does not fail the mission (expendable) and leaves searchable remains (§10).
  The Godot `dialogue_susanin_recruitment.tres` export is unused by Godot's code (it builds both dialogues inline) — same here.
- Waves beyond the level config (or without one) follow Godot's fallback (`FallbackWaveRules` + test,
  `UWaveSubsystem::StartWave`): balance-driven hound / spitter / brute counts from DA_GameBalanceConfig, the whole wave
  at once at the type-filtered spawn points (Gemini's queued built-in waves are gone).
- Level spawn `custom_stats` are all imported (health, damage, speed m/s, attack_range m, attack_cooldown s; `AEnemyCharacter::ApplySpawnEntry`, Godot _spawn_custom_json_wave; no Godot level sets them yet).
- Still hand-typed (Phase 2 continues): camera (user-tuned, see §8.7), enemy visuals / capsules (Gemini). Imported and wired:
  weapons, turn-based balance, operative health / speeds / matches / lift & sprint limits (per role), cold rules, enemy stats and crit chances.
- Floating combat texts (`UFloatingTextSubsystem`, drawn by the HUD over the name plates): wired for operative hits /
  dodge / crit, misses, misfires, frozen / thawed weapon, frostbite, enemy numbers (type prefix, armor «🛡️», immunity,
  burn / bleed ticks), turn-based turret miss, mine armed / spotted / set-up / mishap, barricade cover, repairs, solo
  mode, guard, heal, Susanin, breach, rage, squad stance, sector, cover. Not wired (feature not ported): panic /
  allegiance texts,
  enemy fear of fire, cutter texts, solo frost-crawl. Godot moved the weapon-frozen / frostbite lines
  from the feed to floating texts; UE now does the same (they no longer appear in the message feed).
- **Parity fix:** real-time squad fire follows Godot player.gd (`SquadFireRules`, `AOperativeCharacter::FindShootTarget`):
  range x posture (1 / 1.15 / 1.35) x elevation (1.25), dead zone under platforms, line of fire from the muzzle to the
  enemy (+0.8 m) — walls block, barricades block prone shooters and give crouched ones cover 0.8, another enemy in the
  way becomes the target; target memory with the stance switch delay / ratio from game_balance_config; damage = weapon
  x stance (1 / 1.25 / 1.6) x cover x crit (luck %, x2) x elevation (1.15) x the weapon's distance factor; cryo / fire
  weapons chill / warm the shooter. Before, the nearest enemy in range was shot for flat weapon damage. Not ported:
  panic refusal, reload_after_shots (user decision: 30 rounds).
- Autonomous grenades (`AIGrenadeRules`, `AOperativeCharacter::TryAIGrenadeThrow`; Godot _evaluate_ai_grenade_opportunity):
  in the real-time fight (not raging / reloading, 3 s cooldown) an operative throws at the biggest cluster of 3+ enemies
  in its stance range, 5 m+ away, with no squad member / turret within 5 m of the aim point; the empty-weapon switch
  (Godot _auto_switch_on_empty: M16 with rounds -> pistol -> a grenade at a cluster -> knife) replaces «out of ammo».
- Rage (`URageComponent`, `RageRules`; Godot rage_component.gd): crits per attacker (real-time 25 s memory), chance decays
  with the component's lifetime like Godot's combat_time_elapsed, per-role keys (<role>_rage_*) for the squad, general
  keys for Susanin (Godot re-applies only the general ones to a recruit spawned later). Raging: chaotic target (random
  enemy within 25 m every 0.55 s), fire rate / damage multipliers, +30 luck, no ammo spent, reload paused, OrderMoveTo /
  SetManualPriorityTarget refused. HUD badge «ЯРОСТЬ!»; the fiery aura is an orange 30 cm band at 0.9 m radius under the feet (`URageComponent::UpdateAura`, M_CombatFeedback segments).
  movement_speed_multiplier is exported but unused in Godot — not applied.
- **Parity fix:** enemy attacks on operatives now use Godot player.gd take_damage (dodge luck × 0.4 %, stance defense,
  fortitude cut clamp(f × 1.5 %, 0, 50 %)); before, the enemy armor formula was used (no dodge / fortitude). Grenades /
  traps on the squad use the bypass path (max(1, amount)). A hit on a carrier drops every carried object (`URelocationSubsystem::DropAllForCombat`, alarm line).
- **Parity fix:** turn-based squad shots and turret shots on enemies go through the enemy's own affinity
  (Godot occ.take_damage(final_dmg, KINETIC, 0.0)). **2026-10-11 (audit, user "fix"):** each armour once — a squad shot's
  grid damage already subtracted the grid armour, so it passes `TurnBasedRules::SquadShotArmorPenetration` (1) and the
  health component's % armour no longer cuts it again (Godot did both); the turret / blasts keep the % armour (no grid armour).
  Turn-based barrel / mine blasts (`ApplyBlast`, Godot _detonate_barrel / _detonate_mine): the unit's own hit rules
  (enemy armor; operative `TakeGridBlast` = luck dodge (as Godot) + fortitude + downed x1.33 on the grid-stance amount — the
  real-time stance defence is no longer applied a second time, no cover, no heavy-hit knockdown; `OperativeHitRules`;
  numbers that still differ from real time: `docs/port/tb_parity_2026-10-11.md`), and an enemy dies when its
  health could not take the raw blast (Godot grid hp decides the kill and calls die()). **Deviation:** operatives die by
  real health only — Godot's grid hp would drop a living operative from the fight.
- **Parity fixes (squad control):** operatives are wounded below `wounded_health_threshold_percent` of max health
  (Godot is_wounded; before, `bWounded` was never set, so the wounded speed / no-sprint rules never applied — the speed
  now follows every health change; `UHealthComponent::ApplyDirectHealthLoss` broadcasts OnHealthChanged); a ground click
  during an active wave outside the tactical pause is refused («Перемещение во время боя возможно только в режиме
  тактической паузы»); double-click sprint lines (frozen / wounded / «Бегом к позиции!»). New: Alt + Z / C / V squad
  stance, Shift + click sector facing (no facing indicator arrow; UE has no persistent fixed-facing, the operative just
  turns), auto-cover crouch on arrival next to a barricade in combat with the radio callout. Box selection (Godot
  _perform_box_selection / _set_selected_squad / _get_group_target_positions): LMB acts on release; a drag past 12 px
  draws the cyan box (HUD `DrawSelectionBox`) and selects the living members whose feet / centre / head fall inside
  (`ACodexTacticsPlayerController::SelectInBox`, `USquadSubsystem::SetSelectedGroup`); rings under them (gold leader
  only in a group); ground orders move / plan the whole group in Godot's slots around the click (`OrderGroupMove`,
  `SquadFormation::ComputeGroupTargets`); picking a leader by number drops the group; not in the grid fight (clicks are
  grid orders there). In exploration the formation still pulls followers (as in Godot). The action bar marks
  a squad slot in barricade cover (green frame) or holding (guard / solo; blue) with « ●» (no emoji in the UI font) and
  Godot's tooltips.
- Overhead labels (`FOverheadLabel`, `AEnemyCharacter` / `AInteractableActor::GetOverheadLabel`, HUD `DrawWorldLabels`;
  Godot Label3D): enemies (armor-tier square instead of 🟢🟡🔴, name, statuses as words — ГОРИТ / ЛЁД / ОГЛУШЁН / БРОНЯ-,
  the font has no emoji — HP; hidden for stasis enemies outside a turn-based fight), barricade, turret, generator with
  the Godot texts / colours.
- **Enemy AI (parity pass; Gemini's AEnemyCharacter::Tick replaced):** `EnemyAIRules` + `AEnemyCharacter::Tick` / `FindTarget`
  / `TickSpitter` (Godot enemy_base.gd _physics_process / _process_enemy_behavior / _find_closest_squad_member /
  _find_blocking_barricade / _find_nearest_active_fire_source, enemy_frost_spitter.gd, enemy_frost_brute.gd):
  per-type affinities and base armor (brute kinetic 0.25!, spitter 0.6, hound melee 1.3 …); stagger stops, frost halves
  the speed; hounds / cutters / frostbitten flee burning barrels and active heat sources within 6 m (x1.2 speed,
  «СТРАХ ОГНЯ»); target weights — small enemies: generator 0.4 x, turret 0.5 x, squad 1 x (so hounds rush the generator
  from the start of a wave, like Godot: the generator is always in the "generators" group); large: squad 0.7 x, turrets
  0.85 x (0.6 x after a turret hit) and any turret within 10 m; a barricade / turret within 2.2 m towards the target is
  smashed (brutes double on barricades; an enemy blow sets off a trap); melee needs range and <= 1.2 m height difference;
  spitters pick targets by 100 - distance (+50 elevated), approach / back off around 12 m, shoot only with a line of
  fire (prone behind a barricade hidden, crouched cover 0.65), red tracer. Movement stays on the navmesh (UE AI
  MoveTo) — Godot's ramp routing, wall-slide / stuck avoidance and flocking separation are covered by navigation /
  crowd avoidance. Frostbitten blows shove the operative (+2.5 m/s); hits flash (operative: red light, enemy: red
  overlay; burning ticks do not flash, Godot does). Not ported: the cryo drone (UE maps it to spitter stats), attack
  animation locks (is_attacking; with the AnimBP).
- **Enemy re-path throttle (audit 2026-10-11 #1):** every `AAIController::MoveTo` is a synchronous pathfind in UE 5.8, and
  the real-time enemy brain asks for its move every tick. All per-tick asks go through `AEnemyCharacter::RequestAIMoveToLocation` /
  `RequestAIMoveToActor` -> `AEnemyAIController::RequestMoveTo*`, which issue a MoveTo only when `RepathRules::ShouldRepath`
  (pure, tests `AI.Repath.*`) says so: the pawn within the wanted move's reach (`HasReached`, the test MoveTo runs first) every
  tick as before (an already-at-goal finish, no pathfind - without it a followed chase walked on past the boundary where the
  per-tick ask stopped it, hounds then bit / pounced instead of the old stand-off, and EnemyTacticsSmoke lost its flank
  retargets); a new goal (other actor / location <-> actor / other acceptance) or a stopped / aborted /
  replaced move at once; an actor goal being followed never (the engine's observed path updates it, 0.5 s / 1 m, and the last
  segment walks straight at the actor); a followed location goal when it moved > `Codex.AI.RepathMinGoalDeltaCm` (75) and
  `Codex.AI.RepathInterval` (0.3 s) passed; a reached goal on a goal move or the interval; a failed move every interval.
  `Codex.AI.Repath 0` restores the per-tick behaviour (A/B). The follow state comes from the path following (status, current
  request id, `OnMoveCompleted` result). Turn-based hold and the world AI pause reset the throttle. NOT throttled (own gating):
  `IssuePatrolMove` (patrol route first walk / 1 s stuck, escort tether) - the marksman's override resets the throttle before
  its run; the marksman's other runs (`ExecuteMoveTo`: approach / back-off / flank / retreat) are throttled. Operatives were
  already gated (squad follow `RepathInterval`, autonomy `DecisionInterval`, bot 0.2 s / orders). Counters:
  `AEnemyAIController::GetTotalMoveRequestCount` / `GetTotalPathfindCount` / `GetTotalMoveAskCount` (+ per controller); measure with
  `-ExecCmds="[Codex.AI.Repath 0,]CodexTactics.RepathProbe"` (20 s wave chase, leader walking; not in the smoke registry).
- Cutter (Godot enemy_cutter.gd): own stats (75 HP, 6.2 m/s, 18 damage, 2 m, 1.1 s, crit 0.25 x1.75), the pounce at
  3.5-9 m (windup 0.4 / 1.85 s, ballistic LaunchCharacter flight 1.3 / 1.85 s capped at 9 m, impact damage 28 within
  2.2 m on the squad and barricades, «💥 НАЛЁТ N», recovery 0.75 / 1.85 s, 6 s cooldown) with the numbers imported from
  resources/enemies/anims/cutter.tres (`Scripts/Editor/import_enemy_anim_configs.py` -> /Game/Data/Enemies/DA_EnemyAnim_*);
  Blueprint hooks On Jump Attack Started / Impact for the clips. Godot only jumps when the model has the jump clip —
  UE jumps whenever the config enables it. Shot down mid-leap it keeps falling on the world and crashes («СБИТ В
  ВОЗДУХЕ», «КРАХ»). The airborne kill gives the cutter EXP (16) and counts in the kill statistics (every death goes through HandleDied).
- Frost vignette: drawn with UMG (a UI material drawn by the canvas HUD ignores its opacity). Canvas HUD texts sit under
  it, like Godot's squad HUD under the FrostOverlay. Material traps met on the way (see the script): in a Custom node
  the float3 colour path rendered nothing — keep Custom nodes to scalar math and do colour / opacity with nodes; with
  MaterialEditingLibrary connect a Clamp's first input with the pin name "" (not "Input").
- Silhouette (Godot player.gd + silhouette.gdshader): every operative traces the Visibility channel from the camera to
  its centre (feet + 1 m) 20 times a second; a hit more than 35 cm short puts `M_Silhouette` (colour per role, leader
  cyan) as the overlay material of its visible meshes. Other characters block the ray too (Godot's mask 1 | 2 includes
  bodies on layer 1).
- Event bus (`UCodexEventBus`, Godot event_bus.gd): a hook point for Blueprints / audio / VFX; systems still call each
  other directly (Godot's bus mostly fed the UI). Only the signals Godot actually emits are there; declared-but-never-
  emitted ones, panic / allegiance ones (not ported) and camera_shake_requested (a fallback; the camera is called
  directly) are left out. item_used fires from the inventory drawer only (as in Godot; H / J / K / L do not).
- Turn-based camera choreography (user decision 2026-09-30, Godot main.gd + camera.gd): entering zooms to 14 m; each
  operative's turn / selection glides to it (0.75 s, 16 m); the active operative's own number / a click on it glides
  there (0.85 s) with «🎥 Фокус камеры на бойце»; an enemy's turn glides to it (0.4 s, 11.5 m), its walk (0.35 s,
  11.5 m); after the last enemy the camera glides to the squad centre (0.85 s, 17 m) and waits that long before the
  squad turn. A squad shot (click / Ctrl + click on an enemy, Ctrl + click on a barrel / barricade — Shift until 2026-10-06) plays `AttackCellCinematic`: frame both
  (mid-point, clamp(span x 1.35 + 4 m, 10, 19 m), 0.4 s), shot + shake, the round lands 0.35 s later (`AttackCell`),
  0.65 s to read it, glide back to the shooter at `Config.DistanceCombat` (1.1 s). A turret volley: frame both 0.4 s,
  head turn (snapped, Godot tweens 0.25 s), volley + shake, 0.65 s, glide to the squad centre 0.85 s. Meanwhile
  `IsBusy()` holds every player order. Godot distances are view distances in m; UE uses the same distance in cm. In
  headless runs (no rendering, like Godot's can_tween) the delays are skipped — smokes set
  `bForceCinematicsForTesting` to check them. A turn-based non-leader follow target (enemy) is framed exactly, the
  leader with the deadzone.
- Camera shake (Godot camera.gd): only in turn-based combat, trauma from the squad attack (pistol 0.20 / rifle 0.35)
  and each turret volley (0.28), decay 4 / s, offset = amplitude 0.18 m x trauma² x Perlin noise along the view's right
  / up axes (Godot FastNoiseLite simplex; UE FMath::PerlinNoise1D), values from DA_GameBalanceConfig camera_shake_*.
  The offset is taken off before the follow smoothing so it never drifts the camera.
- Wave victory (`UWaveVictorySubsystem`, `UVictoryPanelWidget`; Godot main.gd): the panel shows while the flow is in
  WaveCleared (time stopped), with Godot's title / subtitle, the kill statistics card and «Запустить следующую волну
  (N/M)» / «Завершить бой и продолжить исследование» (-> `AdvanceAfterWave`) and «Перезапустить уровень (X)». The HUD
  font has no emoji, so the statistics' 🐺 / 🏹 / ❄️ read «гончие / плевуны / громилы». Kills count per Godot
  register_enemy_kill (last attacker; unknown sources go to the leader; the recruit has no entry; only hound / spitter /
  brute per type). Post-combat: enemies removed, the squad teleported to where it stood when the cutscene ended
  (Godot initial_prep_station; before that its position at the post-combat start, Godot: its spawn), orders dropped,
  the commander leads, every bDeployable turret / barricade / mine (abandoned level ones too, as in Godot) goes into
  the supply by Godot's member order and caps with the engineer's feed line. Not ported: deployables_combat_stats
  damage and _record_telemetry (telemetry only); a carried object is not dropped explicitly.
- Progression (Godot player.gd, profile_dialog.gd): kill EXP to every squad member (exp_reward_<type> from
  DA_GameBalanceConfig, cutter 16); level x 250 per level, cap 10; +3 points, full heal, floating text and radio line
  per level; + / - within [start value, cap] (HP +5 up to 200, luck 60, accuracy 100, fortitude 50). Profile on P, on
  the leader's own number key, and on wave clear for the first member with points; Esc closes it. A cleared wave gives
  every member exp_reward_wave_complete (25; `UWaveSubsystem`, Godot _on_wave_cleared); exp_reward_loot_crate is
  config-only in Godot and unused in UE too. Godot names the recruit's profile
  «Полковник Васин» (fallback); UE shows his own name, role «Рекрут / Проводник». The portrait emoji is the role
  initial (no emoji in the font). Level, EXP and points are saved (Godot save_manager.gd).
- Panic is ported and ON (user decision 2026-10-01; Godot ships it disabled), see §10 `UPanicComponent`. The allegiance
  component is only used by Godot tests — not ported.
- Narrative elements (`ANarrativeElementActor`; Godot narrative_element.gd) and dialogue triggers (`ADialogueTriggerVolume`;
  dialogue_trigger.gd): L_MovementTest gets the Godot note (by the generator), signpost (central road), poster (gate wall)
  and the wave-rest trigger (right side, midway) at semantic spots — the Godot scene coordinates don't map onto the UE
  layout. The type emoji marker is a small square (no emoji in the HUD font); in-world text wraps at 48 characters and
  is not occluded by walls (Godot Label3D no_depth_test = false). has_been_read is not saved (Godot neither).
- Space hold (Godot main.gd + tactical_hold_sphere.gd + gorky17_combat_hud charge bar): `AHoldSphereActor` (additive dome
  + growing ring + 15 m boundary ring, eased p x (2 - p)), the squad holds fire while Space is held
  (`AOperativeCharacter::bTacticalCeaseFire`), HUD `DrawSpaceCharge` («ВХОД В ПОШАГОВЫЙ БОЙ (GORKY 17): x.xc / y.yc» /
  «ВОЗВРАТ В ТАКТИЧЕСКУЮ ПАУЗУ»). The dome centre is the leader's feet (Godot ray-casts the floor below the leader).
- Barricade contact damage (Godot barricade.gd contact_*; `ABarricadeActor::ContactType / ContactDamage`): every interval
  enemies within 2.2 m take half of it, a striking enemy takes all of it (not when trapped — the trap goes off), fire
  burns 3 s (tick max(1, dmg / 4)), cryo freezes 2.5 s, energy staggers 1 s, 15 % armour pen, source «Баррикада»; the
  label adds [шипы] / [огонь] / [холод] / [ток]. L_MovementTest's generator barricade (Barricade_Abandoned_West, Godot
  AbandonedBarricadeGenerator) has spikes 5 and vault off, like Godot (BarricadeContactSmoke).
- Pushing / defusal / set-up animations: none (operatives only slow down / crouch).
- **Deliberate deviation (user request 2026-09-30):** enemies that fear fire walk round burning barrels / active heat zones on an arc (35° steps on a circle 1 m outside the zone, `FireDetourWaypoint`) and wait at the edge for a target inside one (the running generator); once panicking they calm down 1 m past the edge. Godot only flees, which made them shake at the edge.
- Turn-based walk: units and pushed objects follow one continuous speed profile over the path (accelerate over the first cell, cruise, decelerate over the last) in Godot's total time; Godot eases each step (first in, last out, middle linear) and snaps the facing per step — UE turns smoothly (~0.15 s).
- Vault (Godot locomotion_controller.gd check_vault_obstacle / start_vault, player.gd try_vault_obstacle; `VaultRules` + test,
  `AOperativeCharacter::TryVault` / `UpdateVault`, `VaultNavigation`): barricades (`bVaultable`, on by default like Godot)
  and level objects tagged `Vault` stop cutting the navmesh and get a `UNavArea_Vault` (cost 3) instead — operatives path
  across and vault (knee-height probe 1.25 m ahead, 25..105 cm, landing 1.35 m behind within ±1 m, arc apex height + 15 cm,
  0.83 s running / 1.09 s walking, collision off, 1.2 s cooldown, the walk resumes); not on the run unless stopped, followers
  only when blocked, never prone or in the grid fight. Enemies use `UNavFilter_NoVault` (they go round / smash, as in Godot).
  No vault clips yet: the anim instance exposes `bIsVaulting` and feeds the arc speed to the locomotion (VaultSmoke).
- Hidden mines are revealed by a distance scan in the mine's Tick (Godot scans from each operative) — same result.
- Weapon switching does not change the weapon mesh (no pistol / shotgun / knife models in the art yet — ASSETS has only
  pistol animations) (the operative Blueprint owns WeaponMesh) and has no holster
  animation. X cycles the arsenal like Godot switch_weapon (grenade -> aim, «🔫 Оружие: …»), R reloads outside placement / the grid fight (Godot KEY_R, «🔄 Перезаряжаю …»).
- Grenades: placeholder sphere mesh, no explosion VFX / sound (Godot has none either) — `AGrenadeActor` Blueprint events
  On Landed / On Detonated and the operative's On Grenade Throw + GrenadeThrowDuration are the hooks.
- Turn-based set-up: the item grows in like Godot (0.2 s, then scale 0.05 -> 1 in 0.45 s, back ease-out; `StartGrowIn`); no assembly animation (Godot play_action_animation "working_device" — no clip yet);
  a squad mine placed in turn-based skips the real-time mishap roll like Godot.
- Turn-based relocation: a hologram of the object follows the hovered cell (Godot _create_relocate_ghost_preview; `SetRelocationHover`, green on a target cell, red elsewhere);
  target cells use the reachable layer (for barricades: cells valid at the current angle). The barricade glides to
  its new place in 0.25 s like Godot (quad ease-out; instant headless).
- Exposed-zone outlines pulse like Godot (alpha 0.65 ± 0.35 at 6 rad/s as glow brightness; the target warning blinks 0.25..0.95 at 9 rad/s, `ATurnGridOverlayActor::Tick`); the additive M_CombatFeedback glow reads pink-white on the
  light floor instead of red — a material for the user to tune (layer colours / intensity in TurnGridOverlayActor.cpp).
- Relocation / placement ghost: `M_GhostHologram` (create_ghost_hologram_material.py; unlit translucent, opacity 0.65, emission x2 like Godot's ghost material).
- Click rules (Godot main.gd plain click, `ACodexTacticsPlayerController::HandleWorldHit`): in a wave a live enemy becomes
  the priority target (direct assignment, no Ctrl); set-up items / movable objects are refused outside the pause; in
  the pause / preparation a set-up item opens its menu at once, in the pause a movable object is picked up for
  relocation at once. A leader alone in a camera zone may move / pick up objects mid-wave (Godot is_zone_solo; `bInCameraZone`).
- Targeted shots: the priority target now gets the barricade cover / blocking of `EvaluateShotLine` and is refused
  while raging (panic refusal: panic not ported). The barrel
  line «💥 Прицельный выстрел…» is posted only when the shot actually fires (Godot posts it even when frozen).
- Save / load: the full state inventory (SAVED / DERIVED / GAP per owner), the restore order and the open follow-ups (APC repair in progress,
  autonomy primary-weapon memory, tutorial multi-hit trigger counts, a stone / grenade in flight, the manual corner peek) are in
  `docs/systems/save-load.md` (audit 2026-10-19, format version 3).
- Save / load differences: a load resumes an active wave from its preparation (Godot only flips flags; neither saves
  enemies); «В главное меню» restarts the level with the start menu (Godot shows the menu over the running scene);
  `AAutosaveTrigger` is not ported — Godot's only trigger (gate_kpp.tscn AutosaveTrigger_Gate) has no collision shape
  and never fires; the start menu has no «Загрузить» (Godot neither). The save file stores the weapon id (Godot:
  current_weapon_idx) and grenades / ExtraAmmo in addition.
- **Deliberate deviation:** the «Патроны 9мм (12 шт.)» hand-over works (Godot shows the button but _transfer_item_to_target
  has no PISTOL_AMMO branch). Ammo handed to a mate without that weapon is kept in ExtraAmmo (Godot drops it).
- **Deliberate deviation (user decision 2026-09-28):** a shot at an untrapped supply crate only posts «💥 Пуля пробила
  ящик снабжения.» — Godot also detonates it (bug: `detonate_trap` always exists on loot_crate.gd).
- Tracers leave `GetWeaponMuzzleLocation` (the weapon's "Muzzle" socket or `MuzzleOffset`); the line of fire keeps
  Godot's stance heights (1.4 / 0.85 / 0.25 m). Light intensity mapping
  (`FeedbackLightPerEnergy` 1500 per Godot light_energy) is a first guess for the user to tune.

