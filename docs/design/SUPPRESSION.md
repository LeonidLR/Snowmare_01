# Suppressive fire

> Game designer decisions 2026-10-26 (the spec below, with the answers of the same day to the first round's questions and the controls
> change), implemented on branch `combat/suppression`. UE-only, no Godot reference.
> Pure rules `Source/CodexTactics/{Public,Private}/Combat/SuppressionRules.*` (tests `CodexTactics.Combat.Suppression.*`), runtime
> `Combat/SuppressionSubsystem.*`, the SUPPRESS order `Characters/OperativeCharacterSuppression.cpp`, the Shift + click
> `ACodexTacticsPlayerController::IssueSuppress`, the red marker `Characters/DefenseMarkerSubsystem`, tuning
> `Content/Data/Combat/suppression.json`, per-weapon data in `Content/Data/Weapons/weapons_tuning.json`, smoke `CodexTactics.SuppressionSmoke`.

## 1. The user's decisions (2026-10-26)

1. Suppression is symmetric: our operatives suppress enemies, enemies' fire suppresses our operatives. Aimed fire suppresses too.
2. A suppressed ENEMY does not fire back at all while suppressed (he hugs his cover) - the window in which the player flanks him.
3. A suppressed OPERATIVE fires blind with a large spread. Under VERY intense suppression (pinned) he does not fire at all.
   The "very intense = no fire" tier applies to both sides.
4. Suppression costs a lot of ammo. Assault rifles switch to full-auto while suppressing.
5. Turn-based mode: NO suppression. The state is cleared on entering turn-based and nothing accumulates there.
6. Vehicle turret machine guns DO suppress, and turrets in general can suppress (reverses the 2026-10-12 note "no suppression
   stagger" in `VehicleTurretComponent.h`).
7. Melee enemies (hounds, zombies, cutters, frostbitten, brutes) are NOT suppressed. Instead DENSE fire into an area SCATTERS large crowds
   of dogs / zombies the way a burning barrel / heat object does: a temporary fear zone the pack breaks up round.
8. Explosions (grenades, mines, barrels) suppress, for a shorter time than gunfire.
9. An operative in RAGE ignores suppression: he fires normally (no blind spread, no pinned no-fire) and is not held in cover; the level
   still accumulates. The rage mechanic will be reworked later - this is one small check, `SuppressionRules::IgnoresSuppression(bRaging)`.
10. Controls: no own key and no action-bar button. Suppression goes through Shift + click: on enemies it is the suppress order with a
    RED marker; elsewhere Shift + click keeps its green meaning.

## 2. Model

- **Level.** Every unit (operative, enemy character) has a suppression level 0..1 from gunfire (`FSuppressionState::Level`) plus a short
  explosion burst (`Burst`); the tier is resolved from `EffectiveLevel` = min(1, Level + Burst).
- **Rounds.** Every shot path reports its round (`USuppressionSubsystem::ReportRound`, `FSuppressionRound`: shooter, side, muzzle -> end,
  the unit aimed at, hit or not, struck something solid at the end, weapon class). For every unit of the other side the round is rated
  (`SuppressionRules::ClassifyImpact`): **Hit**, **CoverStrike** (it struck cover / a wall / the ground within
  `round.cover_strike_radius_cm` of him), **NearMiss** (it passed within `round.near_miss_radius_cm` of his capsule centre; the cannon uses
  `round.explosive_radius_cm`). The stronger of near miss / cover strike wins.
- **Pressure** = weight of the weapon class x (hit: `round.hit_factor`; cover strike: `round.cover_strike_factor`; near miss: 1 at point
  blank falling linearly to `round.near_miss_edge_factor` at the radius) x the weapon's own `suppression_weight` (weapons_tuning.json)
  x `weight.deliberate_multiplier` for an enemy round fired under a deliberate "suppress target" intent (the enemy-AI hook, section 6).
- **Weapon classes** (`ESuppressionWeapon`): SemiAuto (operatives' normal fire, pistol, shotgun...), FullAuto (the SUPPRESS order with an
  assault rifle), MachineGun (squad turrets, the APC's coaxial MG), Sniper (the marksman's rifle, the operatives' sniper rifle), Acid
  (spitter / cryo drone), Cannon (the APC's autocannon).
- **Decay.** The gunfire level always loses `decay.per_second`; after `decay.quiet_seconds` without a round near him also
  `decay.quiet_per_second`. With the defaults ONE rifleman's normal fire never suppresses alone; two riflemen on one target do, slowly
  (~7 s); one SUPPRESS order does within ~2-3 s and pins within ~4-6 s.
- **Explosions** (`ReportExplosion`, fed by every blast through `GameplayEvents::RaiseExplosion` - grenades, mines, barrels, tripwires,
  traps, the APC's cannon): every unit of BOTH sides within the blast radius x `explosion.radius_factor` (at least
  `explosion.min_radius_cm`) gets a burst of `explosion.pressure` (x `explosion.edge_factor` at the edge), capped at `explosion.max_burst`
  (blasts do not stack), decaying at `explosion.decay_per_second` - a blast next to him suppresses at once and is gone within a second or
  so (gunfire's 0.6 lasts several seconds).
- **Tiers** with hysteresis (`SuppressionRules::ResolveTier`): **Suppressed** from `tier.suppressed_enter` (left below
  `tier.suppressed_exit`), **Pinned** from `tier.pinned_enter` (back to Suppressed below `tier.pinned_exit`).

## 3. Effects

| Tier | Enemy (ranged: spitter, cryo drone, marksman) | Operative |
|---|---|---|
| None | as before | as before |
| Suppressed | no fire at all (aim broken), holds his spot (no advance / flank / kiting run), the marksman goes crouched | fires blind: hit chance x `effect.suppressed_hit_multiplier`, spread / miss offset x `effect.suppressed_spread_multiplier`; in cover always blind fire, no corner lean / peek, no corner hold, no stepping off the wall for the open shot (the corner aim ends "suppressed: head down") |
| Pinned | as Suppressed, the marksman goes prone | no fire at all - direct orders included (they wait, their timeouts paused) |

- **Rage** (decision 9): `USuppressionSubsystem::GetActingTierOf` returns None for an operative in rage (`SuppressionRules::IgnoresSuppression`),
  so `GetOperativeFireOf` / `MayExposeNow` - every operative-side effect above - do not act on him; his level (and the HUD badge) still
  accumulates.
- **Melee enemies** (decision 7) are never held: they do not fire. Instead the squad's DENSE fire scatters them (section 3.1).
- **Commander Mode** (`USquadAutonomySubsystem::DecideInCover`): the measured level feeds the existing `SuppressionPressure` (the stronger
  of the shooter count and the level).

### 3.1 Dense fire scatters melee packs

- Every squad / turret / APC round counts to a **fire cell** where it ended (`SuppressionRules::FindFireCell`: the cell whose centre is
  within `zone.cell_cm` of the end, else a new one; its centre is the count-weighted average). The count decays at
  `zone.density_decay_per_second` (capped at twice the threshold). At `zone.density_threshold` the cell opens a **fear zone** of
  `zone.radius_cm` round its centre for `zone.duration_seconds` (refreshed while the fire goes on) - never within
  `zone.min_squad_distance_cm` of a live operative (a hound biting an operative is fought, not scattered).
- With the defaults a SUPPRESS order (~5 rounds/s) or an APC MG opens one in ~2.5 s; one rifleman (~1.5 rounds/s) and a squad turret
  (~2.2 rounds/s) never do; three riflemen on one spot do, slowly.
- The zone is the burning-barrel / heat-source fear zone of the fire-fearing enemies: `AEnemyCharacter::FindNearestFireZone` adds it
  (`USuppressionSubsystem::FindFearZone`, no wall check - the rounds land there). An enemy inside flees out of it ("SCATTERS!"), one
  approaching paths round it (`EnemyAIRules::FireDetourWaypoint`: each hound round the side it comes from, so a pack splits). The
  same enemies as fire: hounds, cutters, the frostbitten and their zombie variants (`bFearsFire`); brutes walk through as through fire.
  A target standing in such a zone is NOT made unusable (that part of the fire fear, `IsInFearZone`, is left out).

## 4. The SUPPRESS order

- **Input** (decision 10; no own key, no action-bar button): **Shift + click** (LMB, and Shift + RMB) on enemies -
  an enemy under the cursor the squad knows of (seen: him; hidden: his silhouette's spot), a silhouette, or a ground point within
  `order.click_radius_cm` of a known enemy (seen or a silhouette; `SuppressionRules::PickClickTarget`) - gives the selection the SUPPRESS
  order on that enemy (his cover takes the rounds). A **RED** marker (the hold-line marker in red: overlay on the enemy the squad sees,
  else on the spot, a red ring of `order.marker_radius_cm`, "SUPPRESS" on the HUD) shows where suppressive fire goes. Shift + click on
  that red marker (the target, or within the ring) ceases it (toggle). Anywhere else Shift + click keeps its green meaning: Shift + LMB
  watch sector, Shift + RMB hold line (green shield). Command id `Attack.Suppress` (tutorial locks under `Attack.*`), F1 row
  "Shift + click on enemies: suppressive fire (red)".
- **Who.** The box-selected group, else the operative you control; nobody aboard a vehicle. Refusals as quick fire (dead, knocked down,
  raging, panicking, the knife, a frozen weapon, hands busy) plus "LOW AMMO - NO SUPPRESSIVE FIRE".
- **Tactical pause.** Planned (red marker, "[PLAN] Suppressive fire planned"), given on the pause release with the planned quick fire.
- **Turn-based.** Shift + click on enemies orders nothing (the grid handles every click); a running order ends on entering it.
- **Fire.** An explicit order: any fire posture, before the automatic fire (`RunCombatShooting` -> `RunSuppressFire`). He stops and turns
  to the target (in cover: the cover pose - lean, or blind when suppressed himself; a point in front of the wall: the open shot unless he
  is suppressed). Each round is an area round (`AOperativeCharacter::FireAreaRound`, shared with quick fire): the spray cone
  `order.spread_*_deg` by stance, an enemy it crosses is hit by the weapon's chance x `order.hit_multiplier` (real damage), barrels / mines
  / trapped objects go off; it suppresses what it passes / strikes and counts to the fire cell where it lands.
- **Cadence / ammo.** An assault rifle (`UWeaponDataAsset::FullAutoInterval` > 0, weapons_tuning.json `full_auto_interval`: the M16 0.1 s)
  fires full-auto bursts of `order.burst_rounds` with `order.burst_pause_seconds` between them (~5 rounds/s vs ~1.5 of the normal fire);
  any other ammo weapon fires at its own cadence (FireRate). It reloads as needed.
- **End.** The target dies, magazine + reserve reach `order.stop_at_rounds` ("LOW AMMO - SUPPRESSION ENDS"; he keeps those rounds),
  Shift + click on its red marker, any new move / cover / group cover / attack order (Ctrl + click targeted shot or quick fire, a priority
  target click, a shot at an object), turn-based combat, `Codex.Combat.Suppression 0`, rage / panic, or no round could leave for
  `order.timeout_seconds`. Not saved (as quick fire). (The API also takes a spot without an enemy - `OrderSuppress(nullptr, Point)`, the
  smoke's fear-zone case; the Shift + click always means an enemy.)

## 5. Where rounds and blasts are reported

| Shooter | Path | Class |
|---|---|---|
| Operative aimed shot | `AOperativeCharacter::ShootAtTarget` (a miss at a target in partial cover = cover strike) | SemiAuto / Sniper |
| Operative quick fire / SUPPRESS order | `AOperativeCharacter::FireAreaRound` | SemiAuto / Sniper / FullAuto |
| Squad turret | `ATurretActor::Fire` | MachineGun |
| APC coaxial MG / cannon | `UVehicleTurretComponent::FireMgRound` / `FireCannon` | MachineGun / Cannon |
| Marksman | `AMarksmanEnemyCharacter::Fire` (a miss at a target in cover = cover strike) | Sniper |
| Spitter / cryo drone | `AEnemyCharacter::TickSpitter` | Acid |
| Every blast | `GameplayEvents::RaiseExplosion` -> `USuppressionSubsystem::ReportExplosion` | (burst) |

Turn-based grid shots never count (the subsystem ignores every round and blast there).

## 6. Enemy-AI hook (later task)

Teaching enemies to suppress deliberately and flank is a later enemy-AI task. The hook: `USuppressionSubsystem::SetSuppressIntent(Shooter,
Target)` / `ClearSuppressIntent` / `GetSuppressIntent`; an enemy round at the intent's target counts x `weight.deliberate_multiplier`
(`AEnemyCharacter::ReportRoundAtSquad`). `OnTierChanged` (native multicast) lets an AI react to an operative being suppressed / pinned
(e.g. send a flanker), `GetTierOf` / `GetLevelOf` answer any decision point.

## 7. HUD, A/B, data

- **HUD cue:** "SUPPRESSED" (amber) / "PINNED" (red) with a level bar over every suppressed unit (`ACodexTacticsHUD::DrawSuppressionBadges`),
  operatives and enemies the squad sees; the red SUPPRESS marker of every order (`UDefenseMarkerSubsystem`, `DrawDefenseMarkers`). English only.
- **A/B:** console variable `Codex.Combat.Suppression` (default 1; 0 = no suppression at all - no level, no effect, no fear zone, Shift +
  click keeps only its green meaning; the pre-2026-10-26 game).
- **Tuning file** `Content/Data/Combat/suppression.json` (`{"params": {...}}`, read at game start by `SuppressionRules::ReloadConfig`;
  staged in packaged builds via `Config/DefaultGame.ini`). The `FSuppressionConfig` defaults mirror it (test
  `CodexTactics.Combat.Suppression.TuningFileMirrorsDefaults`). JSON instead of `UGameBalanceConfig`: that header is generated from the
  Godot `game_balance_config.gd` (no field for a UE-only system) and the project's other UE-only tunables live in JSON the same way
  (weapons_tuning.json, squad_roe.json, ai_tuning.json).

### Every tuning value (defaults)

| Key | Default | Meaning |
|---|---|---|
| `tier.suppressed_enter` | 0.5 | level at which a unit becomes Suppressed |
| `tier.suppressed_exit` | 0.4 | a Suppressed unit is free again below this |
| `tier.pinned_enter` | 0.85 | level at which a unit becomes Pinned |
| `tier.pinned_exit` | 0.7 | a Pinned unit drops back to Suppressed below this |
| `decay.per_second` | 0.1 | the gunfire level always decays this much per second |
| `decay.quiet_seconds` | 1.5 | no round near him this long = quiet |
| `decay.quiet_per_second` | 0.3 | extra decay per second while quiet |
| `round.near_miss_radius_cm` | 200 | a round passing this close to his capsule centre is a near miss |
| `round.near_miss_edge_factor` | 0.5 | a near miss at the radius edge counts this share |
| `round.cover_strike_radius_cm` | 250 | a round striking cover this close to him is a strike on his cover |
| `round.cover_strike_factor` | 0.9 | cover strike x weight |
| `round.hit_factor` | 1.5 | hit x weight |
| `round.explosive_radius_cm` | 450 | the cannon shell's near-miss radius |
| `weight.semi_auto` | 0.04 | one semi-auto round |
| `weight.full_auto` | 0.07 | one full-auto round |
| `weight.machine_gun` | 0.09 | one MG round (turrets, APC) |
| `weight.sniper` | 0.3 | one sniper / marksman round |
| `weight.acid` | 0.1 | one acid spit |
| `weight.cannon` | 0.45 | one cannon shell |
| `weight.deliberate_multiplier` | 1.5 | enemy round under a "suppress target" intent |
| `effect.suppressed_hit_multiplier` | 0.35 | a suppressed operative's hit chance factor (blind) |
| `effect.suppressed_spread_multiplier` | 3 | a suppressed operative's spread / miss offset factor |
| `order.burst_rounds` | 6 | rounds per full-auto burst |
| `order.burst_pause_seconds` | 0.6 | pause between bursts |
| `order.hit_multiplier` | 0.25 | a suppressing round crossing an enemy hits by the weapon's chance x this |
| `order.spread_standing_deg` / `crouching` / `prone` | 3.5 / 2.5 / 1.8 | spray cone half-angle by stance |
| `order.stop_at_rounds` | 10 | the order ends at this many rounds left (kept) |
| `order.timeout_seconds` | 6 | no round for this long: the order is dropped |
| `order.aim_height_cm` | 30 | aim above an enemy's capsule centre |
| `order.spot_scatter_cm` | 120 | fire at a spot walks the rounds over this radius |
| `order.click_radius_cm` | 400 | Shift + click on the ground this close to a known enemy = suppress him |
| `order.marker_radius_cm` | 250 | the red marker's ring; Shift + click within it ceases the order |
| `explosion.pressure` | 0.6 | a blast's burst at its centre |
| `explosion.edge_factor` | 0.4 | the burst's share at the edge of the reach |
| `explosion.radius_factor` | 1.5 | reach = the blast's damage radius x this |
| `explosion.min_radius_cm` | 400 | the reach at least |
| `explosion.max_burst` | 0.9 | the burst's cap (blasts do not stack) |
| `explosion.decay_per_second` | 0.6 | the burst's decay |
| `zone.cell_cm` | 400 | a squad round ending this close to a fire cell's centre counts to it |
| `zone.density_threshold` | 6 | rounds counted in a cell (after decay) that open a fear zone |
| `zone.density_decay_per_second` | 2.5 | the count's decay (sustained fire must beat it) |
| `zone.radius_cm` | 350 | the fear zone's radius |
| `zone.duration_seconds` | 3 | the zone stays this long after the fire there drops below the threshold |
| `zone.min_squad_distance_cm` | 500 | no zone this close to a live operative |

Per weapon (`weapons_tuning.json`): `full_auto_interval` (s; M16 0.1, every other weapon 0 = no full-auto) and `suppression_weight`
(1 for every weapon).

## 8. Answered questions (2026-10-26) and what is still open

- Melee enemies: not suppressed; dense fire scatters them (section 3.1). Explosions: suppress briefly (section 2). Aimed fire suppresses:
  kept. Rage: ignores suppression (section 3; the rage mechanic itself is reworked later).
- Open: none from this round. To watch in play-tests: the fear zone's density / radius / duration (`zone.*`), and whether a squad
  turret's ~2.2 rounds/s should also scatter packs (it does not with the defaults).

## 9. Checks

| Check | What it proves |
|---|---|
| `CodexTactics.Combat.Suppression.*` (11 unit tests) | impact classification, pressure by weapon, tiers + hysteresis, accumulation / decay (one rifleman never suppresses, the order suppresses < 4 s, pins < 7 s), fire effects both sides, order cadence / ammo / stop, the tuning file mirrors the defaults, explosions suppress at once and briefly (capped, not stacking), dense fire opens a fear zone (not one rifleman, not by the squad, closes after the fire stops, cell clustering), the Shift + click target pick, rage ignores suppression |
| `SuppressionSmoke` (L_MovementTest, own arena) | A: Shift + click away from enemies = watch sector, no suppression; Shift + click on the snow beside a marksman 14 m out = the order on him + a RED marker; full-auto (~6 rounds/s vs 1.5), the marksman SUPPRESSED in ~2 s, no shot while the engineer sprints round his flank (held, no aim, still, head down); Shift + click on the red marker ceases it, the marker goes, the level decays and he fires again; the low-ammo stop keeps 10 rounds; a move click cancels an order. B: a marksman's sustained fire suppresses the commander - blind shots (hit chance ~0.17 vs 0.50), then PINNED: no round for 2 s; in RAGE he fires normally while pinned. C: a squad turret suppresses a spitter; a grenade blast 1 m off suppresses it, free again within 1.5 s. F: a spot under suppressive fire opens a fear zone; 6 charging hounds get past it without crossing its core and split left / right. D: turn-based clears every level, ends the order, ignores rounds, Shift + click orders nothing. E: CVar 0 = no suppression, the order refused |
