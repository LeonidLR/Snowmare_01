# Suppressive fire

> Game designer decisions 2026-10-26 (the spec below), implemented on branch `combat/suppression`. UE-only, no Godot reference.
> Pure rules `Source/CodexTactics/{Public,Private}/Combat/SuppressionRules.*` (tests `CodexTactics.Combat.Suppression.*`), runtime
> `Combat/SuppressionSubsystem.*`, the SUPPRESS order `Characters/OperativeCharacterSuppression.cpp`, tuning
> `Content/Data/Combat/suppression.json`, per-weapon data in `Content/Data/Weapons/weapons_tuning.json`, smoke `CodexTactics.SuppressionSmoke`.

## 1. The user's decisions (2026-10-26)

1. Suppression is symmetric: our operatives suppress enemies, enemies' fire suppresses our operatives.
2. A suppressed ENEMY does not fire back at all while suppressed (he hugs his cover) - the window in which the player flanks him.
3. A suppressed OPERATIVE fires blind with a large spread. Under VERY intense suppression (pinned) he does not fire at all.
   The "very intense = no fire" tier applies to both sides.
4. Suppression costs a lot of ammo. Assault rifles switch to full-auto while suppressing.
5. Turn-based mode: NO suppression. The state is cleared on entering turn-based and nothing accumulates there.
6. Vehicle turret machine guns DO suppress, and turrets in general can suppress (reverses the 2026-10-12 note "no suppression
   stagger" in `VehicleTurretComponent.h`).

## 2. Model

- **Level.** Every unit (operative, enemy character) has a suppression level 0..1 (`FSuppressionState`).
- **Rounds.** Every shot path reports its round (`USuppressionSubsystem::ReportRound`, `FSuppressionRound`: shooter, side, muzzle -> end,
  the unit aimed at, hit or not, struck something solid at the end, weapon class). For every unit of the other side the round is rated
  (`SuppressionRules::ClassifyImpact`): **Hit** (it hit him), **CoverStrike** (it struck cover / a wall / the ground within
  `round.cover_strike_radius_cm` of him), **NearMiss** (it passed within `round.near_miss_radius_cm` of his capsule centre; the cannon uses
  `round.explosive_radius_cm`). The stronger of near miss / cover strike wins.
- **Pressure** = weight of the weapon class x (hit: `round.hit_factor`; cover strike: `round.cover_strike_factor`; near miss: 1 at point
  blank falling linearly to `round.near_miss_edge_factor` at the radius) x the weapon's own `suppression_weight` (weapons_tuning.json)
  x `weight.deliberate_multiplier` for an enemy round fired under a deliberate "suppress target" intent (the enemy-AI hook, section 6).
- **Weapon classes** (`ESuppressionWeapon`): SemiAuto (operatives' normal fire, pistol, shotgun...), FullAuto (the SUPPRESS order with an
  assault rifle), MachineGun (squad turrets, the APC's coaxial MG), Sniper (the marksman's rifle, the operatives' sniper rifle), Acid
  (spitter / cryo drone), Cannon (the APC's autocannon).
- **Decay.** The level always loses `decay.per_second`; after `decay.quiet_seconds` without a round near him it also loses
  `decay.quiet_per_second`. With the defaults ONE rifleman's normal fire (a semi-auto round every 0.65 s) stays below the decay and never
  suppresses alone; two riflemen on one target do, slowly; one SUPPRESS order does within ~2-3 s and pins within ~4-6 s.
- **Tiers** with hysteresis (`SuppressionRules::ResolveTier`): **Suppressed** from `tier.suppressed_enter` (left below
  `tier.suppressed_exit`), **Pinned** from `tier.pinned_enter` (back to Suppressed below `tier.pinned_exit`).

## 3. Effects

| Tier | Enemy (ranged: spitter, cryo drone, marksman) | Operative |
|---|---|---|
| None | as before | as before |
| Suppressed | no fire at all (aim broken), holds his spot (no advance / flank / kiting run), the marksman goes crouched | fires blind: hit chance x `effect.suppressed_hit_multiplier`, spread / miss offset x `effect.suppressed_spread_multiplier`; in cover always blind fire, no corner lean / peek, no corner hold, no stepping off the wall for the open shot (the corner aim ends "suppressed: head down") |
| Pinned | as Suppressed, the marksman goes prone | no fire at all - direct orders included (they wait, their timeouts paused) |

- Melee enemies (hounds, cutters, frostbitten, brutes, zombies, the PMC fighter) gather a level (HUD badge) but are NOT held: they do not
  fire (see section 8, open question 1).
- Commander Mode (`USquadAutonomySubsystem::DecideInCover`): the measured level feeds the existing `SuppressionPressure` (the stronger of
  the shooter count and the level), so autonomous operatives crouch / pick blind fire / hold under real suppression too.
- Rage: the pinned / blind rules apply to a raging operative too (open question 2).

## 4. The SUPPRESS order

- **Input.** Key **7** (every letter is taken; 7 is the next free number key after the stealth lure's 5 / 6 - docs checked: F1 controls
  registry `Core/PlayerCommands.cpp`, `docs/port/HANDOFF.md`) or the action bar's **SUPPRESS** button arms the suppress aim (HUD hint
  by the cursor, button "AIM..."); the next LMB click picks the target: a live enemy under the cursor (seen by the squad) or a visible live
  enemy within ~1.5 m of the point (`QuickFireRules::PickTarget`) = suppress HIM (his cover takes the rounds when he hides; out of
  sight his last spot); anything else = suppress the SPOT (rounds walked over a disc of `order.spot_scatter_cm`). RMB / Esc or 7 again
  disarm the aim. While the selection suppresses (and no aim is armed) the button reads "STOP SUPP." and 7 ceases the fire.
  Command id `Attack.Suppress` (tutorial locks under `Attack.*`), F1 row in the Combat section.
- **Who.** The box-selected group, else the operative you control; nobody aboard a vehicle. Refusals as quick fire (dead, knocked down,
  raging, panicking, the knife, a frozen weapon, hands busy) plus "LOW AMMO - NO SUPPRESSIVE FIRE" and "SUPPRESSIVE FIRE IS OFF".
- **Tactical pause.** Planned (marker, "[PLAN] Suppressive fire planned"), given on the pause release with the planned quick fire.
- **Turn-based.** Refused ("Suppressive fire is real time only."); a running order ends on entering it.
- **Fire.** An explicit order: any fire posture, before the automatic fire (`RunCombatShooting` -> `RunSuppressFire`). He stops and turns
  to the target (in cover: the cover pose - lean, or blind when suppressed himself; a point in front of the wall: the open shot unless he
  is suppressed). Each round is an area round (`AOperativeCharacter::FireAreaRound`, shared with quick fire): the spray cone
  `order.spread_*_deg` by stance, an enemy it crosses is hit by the weapon's chance x `order.hit_multiplier` (real damage), barrels / mines
  / trapped objects go off, and it suppresses what it passes / strikes.
- **Cadence / ammo.** An assault rifle (`UWeaponDataAsset::FullAutoInterval` > 0, weapons_tuning.json `full_auto_interval`: the M16 0.1 s)
  fires full-auto bursts of `order.burst_rounds` with `order.burst_pause_seconds` between them (~5 rounds/s vs ~1.5 of the normal fire);
  any other ammo weapon fires at its own cadence (FireRate). It reloads as needed.
- **End.** The target dies, magazine + reserve reach `order.stop_at_rounds` ("LOW AMMO - SUPPRESSION ENDS"; he keeps those rounds), a
  move / cover / group cover order, 7 again, turn-based combat, `Codex.Combat.Suppression 0`, rage / panic, or no round could leave for
  `order.timeout_seconds`. Not saved (as quick fire).

## 5. Where rounds are reported

| Shooter | Path | Class |
|---|---|---|
| Operative aimed shot | `AOperativeCharacter::ShootAtTarget` (a miss at a target in partial cover = cover strike) | SemiAuto / Sniper |
| Operative quick fire / SUPPRESS order | `AOperativeCharacter::FireAreaRound` | SemiAuto / Sniper / FullAuto |
| Squad turret | `ATurretActor::Fire` | MachineGun |
| APC coaxial MG / cannon | `UVehicleTurretComponent::FireMgRound` / `FireCannon` | MachineGun / Cannon |
| Marksman | `AMarksmanEnemyCharacter::Fire` (a miss at a target in cover = cover strike) | Sniper |
| Spitter / cryo drone | `AEnemyCharacter::TickSpitter` | Acid |

Turn-based grid shots never report (and the subsystem ignores every round there).

## 6. Enemy-AI hook (later task)

Teaching enemies to suppress deliberately and flank is a later enemy-AI task. The hook: `USuppressionSubsystem::SetSuppressIntent(Shooter,
Target)` / `ClearSuppressIntent` / `GetSuppressIntent`; an enemy round at the intent's target counts x `weight.deliberate_multiplier`
(`AEnemyCharacter::ReportRoundAtSquad`). `OnTierChanged` (native multicast) lets an AI react to an operative being suppressed / pinned
(e.g. send a flanker), `GetTierOf` / `GetLevelOf` answer any decision point.

## 7. HUD, A/B, data

- **HUD cue:** "SUPPRESSED" (amber) / "PINNED" (red) with a level bar over every suppressed unit (`ACodexTacticsHUD::DrawSuppressionBadges`),
  operatives and enemies; English only.
- **A/B:** console variable `Codex.Combat.Suppression` (default 1; 0 = no suppression at all - no level, no effect, the order refused;
  the pre-2026-10-26 game).
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
| `decay.per_second` | 0.1 | the level always decays this much per second |
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

Per weapon (`weapons_tuning.json`): `full_auto_interval` (s; M16 0.1, every other weapon 0 = no full-auto) and `suppression_weight`
(1 for every weapon).

## 8. Open questions for the game designer

1. Should suppression also stop MELEE enemies (hounds / zombies) rushing - e.g. slow or halt their advance? Now they only show the badge.
2. Should a raging operative ignore suppression (berserk)? Now rage gets no exemption: pinned = no fire.
3. Should normal aimed fire of several operatives suppress (it does now, slowly: two riflemen on one marksman suppress him in ~7 s), or
   only the SUPPRESS order / MGs?
4. Grenade / mine / barrel explosions do not suppress yet - add them (an explosive near miss)?

## 9. Checks

| Check | What it proves |
|---|---|
| `CodexTactics.Combat.Suppression.*` (7 unit tests) | impact classification, pressure by weapon, tiers + hysteresis, accumulation / decay (one rifleman never suppresses, the order suppresses < 4 s, pins < 7 s), fire effects both sides, order cadence / ammo use / stop, the tuning file mirrors the defaults |
| `SuppressionSmoke` (L_MovementTest, own arena) | A: key 7 arms, the order on a marksman 14 m out: full-auto (~6 rounds/s vs 1.5), the marksman SUPPRESSED in ~2 s, no shot while the engineer sprints round his flank (held, no aim, still, head down), 7 ceases, the level decays and he fires again, the low-ammo stop keeps 10 rounds; B: a marksman's sustained fire suppresses the commander - blind shots (hit chance 0.17 vs 0.50), then PINNED: no round for 2 s; C: a squad turret suppresses a spitter; D: turn-based clears every level, ends the order, ignores rounds, refuses 7; E: CVar 0 = no suppression, the order refused |
