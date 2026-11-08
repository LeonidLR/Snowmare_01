# WEAPONS_FRAMEWORK.md - the unified weapon schema (step 1 of the weapons week)

> Status: **step 1 done on branch `feat/weapons-framework` (2026-11-08)** - the schema, its loader / validator and the plumbing.
> Week plan (game designer, 2026-11-08): Nov 8 = this schema; Nov 9 = ballistics + armour penetration; Nov 10 = framework tests +
> the pistol; then the shotgun D-1 (#21), the sniper rifle moved onto the framework, the horde. Later weeks build on the same schema:
> RPG-7 and fixed mortar nests (week 2, shared projectile / explosion code), squad of 4-6 (week 3), APC (week 4).
>
> Step 1 changed no number (parity test, section 9). **Step 2 (section 12) changes balance on purpose** - the game designer's decisions
> of 2026-11-08 (section 11); every changed number is listed in 12.2.
>
> Code: `Source/CodexTactics/Public/Data/WeaponStats.h` (the structs), `Data/WeaponStatsRules.h/.cpp` (JSON <-> stats, validation),
> `Data/WeaponDataAsset.h` (`UWeaponDataAsset::Stats`), `Data/WeaponTuning.cpp` (the file loader). Tests `CodexTactics.Weapons.Schema.*`.
> System doc: `docs/systems/weapons.md`.

## 1. Three layers, one thing the game reads

```
DA_Weapon_<id>.uasset (base fields, the fallback)  -->  UWeaponDataAsset::Stats  <--  Content/Data/Weapons/weapons_tuning.json
        built on load / creation / an editor edit          (transient, never saved)      applied + VALIDATED at StartPlay (master copy)
                                                                   |
                       every reader: the squad, the hostile PMC fighters, the turn-based grid, the bot, the HUD
```

- **The JSON file is the master copy** of the numbers (the Wave Editor «Оружие» tab writes it; unchanged rule since 2026-10-04).
- **The asset** keeps identity and visuals (`WeaponId`, `WeaponName`, `HandMesh`, grip transform, muzzle offset, `TracerColor`) and its
  old flat fields as the **base layer** (what a weapon has if the file does not list a key). The flat fields keep their names, so the ten
  existing `DA_Weapon_*` assets load unchanged - **no .uasset was touched**.
- **`Stats`** (`FWeaponStats`) is what gameplay reads: base layer + file. It is `Transient`: never written into the asset (before, the file
  was written into the asset's fields in memory, and a save in the editor could bake PIE values into the asset - that cannot happen now).
- Gameplay code must not read the base fields; `CodexTactics.Weapons.Schema.NoLegacyReads` scans `Source/` and fails on such a read (a
  branch cut before the schema that adds `CurrentWeapon->AttackRangeCm` would otherwise silently skip the file after its merge).

## 2. The schema: common core + per-kind blocks

`FWeaponStats` = `Kind` + `Handling` + nine core blocks (every weapon has them) + three per-kind blocks (read only for their kinds).
JSON keys are flat for the core (the Wave Editor's keys, Godot `weapon_data.gd` names) and nested objects for the per-kind blocks.
Units: JSON metres / seconds; C++ centimetres / seconds.

### 2.1 Common core (read today)

| C++ (`Stats.`) | JSON key | Unit | Default | Meaning | Read by (today) |
|---|---|---|---|---|---|
| `Kind` | `kind` | hitscan / projectile / thrown / melee / beam | base: `melee` if no ammo, else `hitscan` | delivery family (section 3) | nobody yet (the dispatch starts Nov 9) |
| `Handling` | `handling` | standard / sniper_rifle | standard | fire-stance rules + animation set | `IsSniperRifle()`: sniper rules, anim, smokes |
| `Damage.BaseDamage` | `base_damage` | HP | 18 | one hit before armour / cover / stance | aimed shot (`SquadFireRules::ComputeShotDamage`), turn-based (`TurnBasedRules::GetDamageForDistance`), HUD |
| `Damage.DamageType` | `damage_type` | kinetic / melee / fire / cryo / energy / explosive | kinetic | elemental affinities, numbers, tracer | `FDamageSpec`, tracers, turn-based |
| `Damage.ArmorPenetration` | `armor_penetration` | 0..1 | 0.20 | share of the target's armour reduction ignored | `FDamageSpec` -> `UHealthComponent::TakeDamage` |
| `Range.MaxRangeCm` | `attack_range_m` | m (JSON) | 14 | max reach: no shot beyond | auto fire / `EvaluateShotLine`, quick fire, PMC bands, autonomy reach (x1.3), bot, weapon zone, cones |
| `Range.EffectiveRangeCm` | `effective_range_m` | m | 0 (= max) | hit chance unchanged up to it, falls to `range_falloff.floor_multiplier` at the max | `GetRangeHitMultiplier`, weapon zone |
| `Fire.ShotIntervalSeconds` | `fire_rate` | s between shots (**an interval**, the old name stays) | 0.65 | cadence of aimed / quick fire | shoot timer, quick fire, SUPPRESS |
| `Fire.FullAutoIntervalSeconds` | `full_auto_interval` | s | 0 (= none) | full-auto cadence of the SUPPRESS order only | `SuppressionRules::NextOrderRoundDelay` |
| `Fire.SuppressionWeight` | `suppression_weight` | x | 1 | x the suppression a round causes | suppression rounds |
| `Ammo.bUsesAmmo` | `uses_ammo` | bool | true | false = melee: no clip, no reload | `UsesAmmo()` |
| `Ammo.MagazineSize` | `max_clip_size` | rounds | 30 | full magazine | reload, autonomy reload, quick-fire burst, PMC ammo, corner aim |
| `Ammo.ReserveAmmo` | `default_reserve_ammo` | rounds, -1 = infinite | 100 | start reserve | arsenal init, PMC, sniper kit |
| `Ammo.ReloadSeconds` | `reload_time` | s | 2.68 | full reload | reload timer |
| `Accuracy.HitChanceByCell` | `base_hit_chances` | 0..1 per cell | empty = 0.85 | hit chance by distance; real time cell = 1.5 m (`round(m / 1.5)`), turn-based = a grid cell | aimed shot, quick fire, turn-based |
| `Accuracy.DamageMultiplierByCell` | `distance_damage_multipliers` | x per cell | empty = x1 | damage by distance (the current damage falloff) | aimed shot, turn-based |
| `Status.Effect` | `status_effect` | none / burning / frozen / stagger / bleeding / armor_shred / shocked | none | status on hit | `FDamageSpec` |
| `Status.DurationSeconds` | `status_duration` | s | 0 | its length | `FDamageSpec` |
| `Status.TickDamage` | `status_tick_damage` | HP / tick | 0 | burning / bleeding | `FDamageSpec` |
| `Thermal.SelfColdPerShot` | `self_cold_generation` | cold points | 0 | cryo emitter chills the shooter | operative shot |
| `Thermal.SelfWarmthPerShot` | `self_warmth_generation` | cold points | 0 | flamethrower warms the shooter | operative shot |
| `TurnBased.AttackShape` | `attack_shape` | rays8 / rays4 / melee_adj / free_target | rays8 | Gorky grid pattern | `TurnBasedRules` |
| `TurnBased.MaxRangeCells` | `max_range_cells` | cells | 5 | grid reach | `TurnBasedRules` |

Shared by every weapon (not per weapon): `range_falloff` (`floor_multiplier` 0.1, `exponent` 2) at the file's root, as before.

### 2.2 Reserved fields (parsed + validated today, read from Nov 9 on; defaults = today)

| C++ | JSON key | Default | For |
|---|---|---|---|
| `Damage.PenetrationClass` | `penetration_class` (none / light / medium / heavy / vehicle) | light | armour classes, section 4 |
| `Damage.FalloffDamageFloor` | `damage_falloff_floor` | 1 (no falloff) | Nov 9 ballistics: damage x this at the max reach, linear from the effective range |
| `Range.MinRangeCm` | `min_range_m` | 0 | the mortar's minimum range (week 2) |
| `Ammo.ReloadMode` | `reload_mode` (magazine / per_round) | magazine | shotgun shell-by-shell reload (open question 3) |
| `Ammo.PerRoundReloadSeconds` | `reload_per_round_s` | 0 | per-round reload time |
| `Accuracy.SpreadScale` | `spread_scale` | 1 | x the existing stance spread of quick fire / SUPPRESS (`QuickFireRules` / `SuppressionRules` per stance) |
| `Accuracy.RecoilPerRoundDeg` | `recoil_per_round_deg` | 0 | bloom per consecutive round |
| `Accuracy.RecoilRecoverySeconds` | `recoil_recovery_s` | 0 | bloom settles in |

### 2.3 Per-kind blocks (reserved)

| Block (JSON object) | Keys (unit, default) | Kinds | Week |
|---|---|---|---|
| `pellets` | `count` (1), `cone_half_angle_deg` (0, < 45) | hitscan | shotgun D-1 |
| `projectile` | `speed_mps` (0; > 0 required for `projectile`), `gravity_scale` (0 = flat, 1 = ballistic), `arming_distance_m` (0), `high_angle` (false) | projectile, thrown | 2 (RPG-7, mortar nests) |
| `explosion` | `radius_m` (0 = no blast), `full_damage_radius_m` (0, <= radius), `edge_damage_multiplier` (1), `back_blast_length_m` (0), `back_blast_half_angle_deg` (0) | projectile, thrown | 2 (shared explosion code) |

## 3. How every kind fits one schema

| Kind | Who | Core used | Block | Today's behaviour (unchanged) |
|---|---|---|---|---|
| hitscan | M16, pistol, sniper rifle, shotgun, plasma carbine | all | `pellets` (shotgun) | aimed shot = line check (`EvaluateShotLine`) + a roll against `HitChanceByCell` x range falloff x cover / stance / lean; area fire (quick fire / SUPPRESS) traces a stance spread cone |
| beam | flamethrower, cryo emitter | all; status + thermal matter | none needed yet (a stream = short range + high cadence + status) | fires the same hitscan path with its status / thermal |
| thrown | grenade | damage, range (= throw reach), ammo (1 + reserve), grid shape | `projectile` (arc), `explosion` | real time: the operative's grenade values (`grenade` block, see 6.2); turn-based: the asset's stats |
| melee | knife, hammer | damage, range (reach), cadence, status; `uses_ammo` false | none | blows within reach |
| projectile | RPG-7, mortar shell (week 2) | all | `projectile` + `explosion` | none yet |

Validation ties kinds to data: a melee weapon uses no ammo; a projectile needs a speed (section 7).

## 4. Armour classes and penetration (data today, behaviour Nov 9)

What the code did before step 2: each body has ONE armour reduction fraction (`UHealthComponent::BaseArmorReduction`: operatives and
PMC 0.10; hound / base 0.10, frostbitten / cutter 0.15 (tier Light); spitter 0.40, marksman 0.15, cryo drone 0.05 (tier Medium); brute
0.75 (tier Heavy) - typed in C++, `EnemyAIRules::GetBaseArmor`) and the hit does `damage x (1 - reduction x (1 - armor_penetration))`,
then x the elemental affinity. (Step 2 moved these numbers, unchanged, into `weapons_tuning.json` "armor.bodies", section 12.) The APC hull has its own factors per source (`vehicle_weapons.apc_hull.rifle_factor` 0.25, `explosion_factor` 1, ...).

Schema: `EArmorClass` {None, Light, Medium, Heavy, Vehicle} and the weapon's `penetration_class` (the highest class it defeats at full
effect). The model below was the recommendation of open question 1; it is decided (section 11) and implemented (section 12): bodies get `armor_class` + the class's reduction moved from C++ into data (the
`EArmorTier` values above are the starting numbers); the multiplicative rule stays for a weapon at or above the target's class; below it,
the penetration does not apply (the full class reduction), and against Vehicle only explosives / the RPG matter (the hull factors).
Classes per weapon (decided, in the file since step 2): knife / pistol / shotgun / cryo / flamethrower light, M16 / hammer / grenade
medium, sniper / plasma heavy, RPG-7 vehicle (week 2).

## 5. Operatives, PMC fighters and enemies share it

- **Squad operatives and hostile PMC fighters** are the same class (`AOperativeCharacter`, `TeamId != 0` = PMC) with the same
  `CurrentWeapon` (a `DA_Weapon_*`): one weapon, one set of numbers, both sides. PMC bands (`PmcSquadSubsystem`) read `Stats.Range`.
- **The Marksman enemy** still has its own rifle block (`enemy_weapons.marksman_rifle` -> `FMarksmanConfig`: damage, accuracy, aim time,
  cooldown, crit, stance bonuses, preferred band, effective / max range, turn-based `tb_*`). Planned (with "the sniper rifle onto the
  framework", this week): the ballistic part becomes a `weapons` entry read through `FWeaponStats`; the AI part (aim time, preferred band,
  stance bonuses) stays the enemy's.
- **Melee enemies** (hounds, brutes, cutters, spitter acid) attack from `EnemyArchetypeAsset` / `ai_tuning.json`: not weapons; out of scope.
- **APC** turret / MG (`vehicle_weapons.apc_turret`): its own block today; week 4 maps cannon / MG onto `FWeaponStats` (cannon = projectile
  + explosion, MG = hitscan with full-auto) and the hull onto armour class Vehicle.

## 6. Migration table

### 6.1 Old field -> new field (C++ and JSON)

| Old asset field (still the base layer) | JSON key (unchanged) | New `Stats.` path |
|---|---|---|
| `BaseDamage` | `base_damage` | `Damage.BaseDamage` |
| `DamageType` | `damage_type` (new key) | `Damage.DamageType` |
| `ArmorPenetration` | `armor_penetration` | `Damage.ArmorPenetration` |
| `AttackRangeCm` | `attack_range_m` | `Range.MaxRangeCm` |
| `EffectiveRangeCm` / `GetEffectiveRangeCm()` | `effective_range_m` | `Range.EffectiveRangeCm` / `Range.GetEffectiveRangeCm()` (asset helper kept) |
| `GetRangeHitMultiplier()` | - | `Range.GetHitMultiplier()` (asset helper kept) |
| `FireRate` | `fire_rate` | `Fire.ShotIntervalSeconds` |
| `FullAutoInterval` / `HasFullAuto()` | `full_auto_interval` | `Fire.FullAutoIntervalSeconds` / `Fire.HasFullAuto()` |
| `SuppressionWeight` | `suppression_weight` | `Fire.SuppressionWeight` |
| `bUsesAmmo` | `uses_ammo` (new key) | `Ammo.bUsesAmmo` |
| `MaxClipSize` | `max_clip_size` | `Ammo.MagazineSize` |
| `DefaultReserveAmmo` | `default_reserve_ammo` | `Ammo.ReserveAmmo` |
| `ReloadTime` | `reload_time` | `Ammo.ReloadSeconds` |
| `BaseHitChances` / `GetHitChanceForDistance()` | `base_hit_chances` | `Accuracy.HitChanceByCell` / `Accuracy.GetHitChanceForCells()` (asset UFUNCTION kept) |
| `DistanceDamageMultipliers` | `distance_damage_multipliers` | `Accuracy.DamageMultiplierByCell` |
| `StatusEffect` / `StatusDuration` / `StatusTickDamage` | `status_effect` (new) / `status_duration` / `status_tick_damage` | `Status.Effect` / `.DurationSeconds` / `.TickDamage` |
| `SelfColdGeneration` / `SelfWarmthGeneration` | `self_cold_generation` / `self_warmth_generation` (new) | `Thermal.SelfColdPerShot` / `.SelfWarmthPerShot` |
| `AttackShape` / `MaxRangeCells` | `attack_shape` (new) / `max_range_cells` | `TurnBased.AttackShape` / `.MaxRangeCells` |
| `Handling` / `IsSniperRifle()` | `handling` (new) | `Handling` / `IsSniperRifle()` |
| - | `kind` (new, in the file for all ten) | `Kind` |
| `WeaponId`, `WeaponName`, `HandMesh*`, `TracerColor` | - | stay on the asset (identity / visuals) |

The "new" JSON keys are optional: today the file adds only `kind` (and root `schema_version: 2`); the enum / thermal values still come from
the assets. `CodexTactics.DumpWeaponTuning` now writes every key, so one dump moves them into the file when the designer wants the file
to describe each weapon completely (then the asset fields could retire, a later step).

### 6.2 Per weapon (the values in force, `weapons_tuning.json`, unchanged)

| id | kind | damage | eff / max m | interval s | pen | mag / reserve | reload s | full-auto s | cells | status s / tick |
|---|---|---|---|---|---|---|---|---|---|---|
| cryo_emitter | beam | 16 | 7 / 10.5 | 0.5 | 0 | 30 / 60 | 2.2 | 0 | 5 | 3.5 / 0 |
| flamethrower | beam | 32 | 6 / 8.5 | 0.4 | 0.3 | 50 / 100 | 2.8 | 0 | 5 | 4 / 10 |
| grenade | thrown | 85 | - / 12 | 1 | 0.5 | 1 / 2 | 1 | 0 | 5 | 0 / 0 |
| hammer | melee | 65 | - / 2.2 | 1.25 | 0.6 | 0 / 0 | 0 | 0 | 5 | 1.5 / 0 |
| knife | melee | 22 | - / 2 | 0.35 | 0.15 | 0 / 0 | 0 | 0 | 5 | 4 / 4 |
| m16 | hitscan | 18 | 28 / 60 | 0.65 | 0.2 | 30 / 100 | 2.68 | 0.1 | 8 | 0 / 0 |
| pistol | hitscan | 16 | 15 / 25 | 0.45 | 0.1 | 12 / -1 | 1.3 | 0 | 6 | 0 / 0 |
| plasma_carbine | hitscan | 28 | 10 / 16 | 0.8 | 0.65 | 20 / 60 | 2.4 | 0 | 5 | 1 / 0 |
| shotgun | hitscan | 45 | 8 / 15 | 1.15 | 0.1 | 8 / 40 | 2.4 | 0 | 5 | 0 / 0 |
| sniper_rifle | hitscan | 70 | 90 / 120 | 2.4 | 0.6 | 5 / 15 | 3.5 | 0 | 10 | 0 / 0 |

Known duplication (kept, flagged): the grenade has two sources - `weapons.grenade` (the turn-based grid's stats, 85 / 12 m) and the root
`grenade` block (real-time throw: `damage` 85, `effect_radius_m` 4, `throw_range_m` 12, `max_carried` 4 onto the operative). Week 2's
explosion code should make `weapons.grenade.explosion` the one source.

## 7. Validation (an invalid weapon block is rejected; the weapon keeps its asset values; a warning names every problem)

No negative damage / range / time; `attack_range_m` > 0; `effective_range_m` <= `attack_range_m`; `min_range_m` below the effective
range; `fire_rate` > 0; `armor_penetration` and `damage_falloff_floor` in [0, 1]; with `uses_ammo`: `max_clip_size` >= 1 and
`reload_time` > 0; `default_reserve_ammo` >= -1; a melee weapon uses no ammo; `base_hit_chances` in [0, 1], damage multipliers >= 0;
`max_range_cells` >= 1; a status effect has a duration; `pellets.count` >= 1, cone < 45 deg; a projectile has `speed_mps` > 0;
`full_damage_radius_m` <= `radius_m`; edge multiplier in [0, 1]; back-blast half-angle <= 90. A wrong type (`"18"` for a number) or an
unknown enum name is an error too; an unknown key is a warning (typo guard), not an error.

## 8. What changed in behaviour (on purpose, outside play)

1. An invalid weapon block no longer reaches the game (before: any value was applied as is). The project file validates clean.
2. `ApplyFile` starts each weapon from the asset's base values, then applies the file (before: on top of the previous run's in-memory
   values, so a key deleted from the file kept its old value until the editor restarted).
3. The editor's Details panel of a `DA_Weapon_*` shows the asset base in the flat fields and the resolved numbers in **Weapon | Stats**
   (read-only); PIE no longer writes the file's numbers into the asset objects.

## 9. Parity proof

`CodexTactics.Weapons.Schema.ParityWithLegacyPipeline`: for each of the ten weapons the pre-schema pipeline (the asset's flat fields + a
verbatim copy of the old `ApplyWeapon` key mapping) is compared bit-exact with the new one (`MakeBaseStats` + the schema's `ApplyJson`),
22 values per weapon before and after the file, plus the derived helpers (effective range, the hit chance at 1..20 cells, full-auto,
sniper handling). The other suites (`Data.WeaponTuning.*`, `Data.WeaponImportParity`, `Combat.*`, `Tactics.*`) still pass with the
readers moved, and the combat smokes run unchanged (section 11 of the commit report / `docs/systems/weapons.md`).

## 10. The Gemini draft (`WEAPONS_BALLISTICS.md`, 2026-11-07) checked against the code

Correct: the field list in 1.1 (names / meanings), the hit-chance falloff (`1 - 0.9 t^2` = floor 0.1, exponent 2), the M16 / sniper /
pistol / shotgun numbers marked "in data". Contradictions / corrections:

1. **Armour formula.** Draft: `damage x (1 - max(0, reduction - penetration))` (subtractive). Code: `damage x (1 - reduction x (1 -
   penetration))` (multiplicative), then x the elemental affinity. The schema keeps the code's rule; the draft's 3.2 table is wrong for
   today.
2. **Body classes.** Draft: 4 classes with per-damage-type reductions, "Class 3 = Frostbitten Brute". Code: one fraction per body; the
   Brute is Heavy 0.75, the Frostbitten is Light 0.10; operatives / PMC 0.10. There is no per-damage-type armour (that is the elemental
   affinity table).
3. **APC numbers.** Draft "M16 vs APC front 25 % (in data)": the 0.25 in data is the hull's `rifle_factor` (all directions) and, separately,
   the turret cannon's `cannon_armour_factor`; there are no front / side / rear / wheel zones in the code.
4. **Hitscan model.** Draft: every shot is a `LineTraceSingle` on Visibility. Code: an aimed shot is a line check plus a dice roll
   against the hit chance (distance table x range falloff x cover / stance / lean x0.5); the tracer is cosmetic. Only area fire traces a
   spread cone.
5. **Mortar.** Draft: a carried 60 mm mortar, gunner + ammo bearer, deploy 3.5 s, min 35 m / max 160 m. Designer decision 2026-11-08
   (`SQUAD_SIX_FIRE_TEAMS.md` 7.3): **fixed nests, crew of three, both sides, spotter rule, min 30 m / max 250 m (guess)**. The draft's
   4.2 and the mortar row of 4.3 are superseded.
6. **Suppression.** Draft proposes `suppression_power` (points); the game has `suppression_weight` (x the class weight in
   `SuppressionRules`). The schema keeps `suppression_weight`; no second suppression number.
7. **Spread by stance / movement.** The stance spread already exists per fire mode (`QuickFireRules`, `SuppressionRules`); the schema adds
   one per-weapon `spread_scale` on top instead of a second per-stance table.
8. **Noise.** Draft `noise_radius_m` per weapon: gunshots use one hearing radius (`enemy_perception.json` `hear_gunshot_m`). Not in the
   schema (not on the board); easy to add as a reserved key when wanted.
9. **Shotgun pellets.** Draft: 45 split over 8 pellets, 6 deg half-angle - consistent with the recommendation in open question 2.
10. The draft's PMC engagement bands (5.1) were not checked against `pmc_commander.json`; they are AI tuning, not weapon data.

## 11. Decisions 2026-11-08 (the game designer: "Согласен со всеми рекомендациями" - all five recommendations accepted)

1. **Armour classes: 5** - none / light / medium / heavy / vehicle (today's three enemy tiers + "none" + the APC). A weapon at or above the
   target's class works as today; **below it the weapon's armour penetration does not apply** (the target's full armour reduction); a
   vehicle is hurt seriously only by explosives / the RPG. The armour formula stays multiplicative (section 4). Per-weapon classes: knife /
   pistol / shotgun / cryo emitter / flamethrower light, M16 / hammer / grenade medium, sniper rifle / plasma carbine heavy, RPG-7 vehicle.
2. **Shotgun damage:** `base_damage` 45 = the whole shot at point blank, split evenly over 8 pellets (5.625 each); a partial hit does less.
3. **Shotgun reload: per shell**, ~0.5 s a shell (`reload_mode` per_round, `reload_per_round_s` 0.5), interruptible - he fires as soon as
   one shell is in.
4. **Damage falloff beyond the effective range: only the shotgun (`damage_falloff_floor` 0.4) and the pistol (0.6)**; linear from the
   effective range (x1) to the max reach (x floor); rifles none (default 1).
5. **M16 full-auto recoil:** ~0.3 deg of extra spread per round (`recoil_per_round_deg` 0.3), settling in 0.5 s (`recoil_recovery_s` 0.5);
   single aimed shots stay roll-based without recoil.

Implementation: step 2 (ballistics + armour penetration), section 12.

## 12. Step 2: ballistics + armour penetration (branch `feat/weapons-framework`, 2026-11-08, ahead of the Nov 9 plan)

Code: `Combat/ArmorRules` (the class rule, the body table), `Combat/WeaponBallisticsRules` (falloff, pellets, per-shell reload, recoil),
`UHealthComponent::ArmorClass`, `FDamageSpec::PenetrationClass`. Tests `CodexTactics.Weapons.Armour.*`, `CodexTactics.Weapons.Ballistics.*`.

### 12.1 What it does

1. **Armour classes on bodies (decision 1).** `weapons_tuning.json` root `"armor": {"bodies": {...}}`, nine bodies: `operative` (the
   squad AND the PMC fighters) light 0.10, `base` / `frost_hound` light 0.10, `frostbitten` / `cutter` light 0.15, `spitter` medium 0.40,
   `marksman` medium 0.15, `cryo_drone` medium 0.05, `brute` heavy 0.75. The reductions are the old C++ values (no reduction changed,
   test `BodiesFromFile`), the classes the old tiers. Read as a body spawns (the enemy archetype setup, `AOperativeCharacter::BeginPlay`);
   a body without an entry keeps the C++ fallback. The APC hull keeps its own factors (`vehicle_weapons.apc_hull`: rifle x0.25,
   explosion x1) - it is class Vehicle by construction; nothing changed there.
2. **The class rule (decision 1), the multiplicative formula kept.** `reduction met = base (x0.3 when shredded) x (1 - penetration)` only
   when the weapon's `penetration_class` >= the body's class; below it `reduction met = base`. Only weapon hits carry a class
   (`ApplyRangedHit`: aimed shots, quick fire, SUPPRESS - the squad and the PMC fighters); every other damage source (claws, blasts,
   turrets, traps, burning ticks, the marksman's rifle) keeps the old formula (`FDamageSpec::PenetrationClass` default Vehicle = no limit).
   Turn-based keeps its own Gorky flat armour points (no change).
3. **Damage falloff (decision 4).** Real-time hits: x1 up to the effective range, linear to `damage_falloff_floor` at the max reach -
   shotgun 0.4 (8 -> 15 m), pistol 0.6 (15 -> 25 m), every other weapon 1. It stacks with the per-cell `distance_damage_multipliers`.
4. **Shotgun pellets (decision 2).** `pellets` {count 8, cone 6 deg}: an AIMED shot rolls its hit chance 8 times; the hit does
   45 x hits / 8 (5.625 a pellet); all 8 missing = a miss. The average damage is unchanged (45 x chance); single shots vary less and a
   "MISS" is rarer ((1 - p)^8 instead of 1 - p). Quick fire / SUPPRESS rounds stay one roll (an area spray). The shotgun is not in the
   starting arsenal: it matters where a level / loot hands one out, and for the D-1 (#21).
5. **Per-shell reload (decision 3).** Shotgun `reload_mode` per_round, 0.5 s a shell: 8 shells from empty = 4.0 s (was 2.4 s for the whole
   magazine). Between shells the reload stops as soon as he has something to fire at (an engaged target, a quick fire or SUPPRESS order)
   and at least one shell is in. The reload animation still plays once at the start (one shell's length); a per-shell loop in the ABP is
   the user's polish.
6. **M16 full-auto recoil (decision 5).** The SUPPRESS order's full-auto rounds add 0.3 deg of spray spread each, settling at 0.6 deg / s
   (one round's kick in 0.5 s): a 10-round burst at 0.1 s ends 2.16 deg wider than it started (standing 3.5 -> 5.66 deg). Aimed shots,
   quick fire and semi-auto SUPPRESS rounds: no recoil. `spread_scale` (x1, unchanged) now multiplies the quick fire / SUPPRESS spread.

### 12.2 Every number that changes (real time; one hit before cover / stance / crit / elemental affinity; before -> after)

Class rule (inside the effective range; only these weapon x body pairs change):

| Weapon (class, penetration) | spitter (medium 0.40) | marksman (medium 0.15) | cryo drone (medium 0.05) | brute (heavy 0.75) |
|---|---|---|---|---|
| knife 22 (light, 0.15) | 14.52 -> 13.20 | 19.20 -> 18.70 | 21.07 -> 20.90 | 7.98 -> 5.50 |
| pistol 16 (light, 0.10), point blank | 10.24 -> 9.60 | 13.84 -> 13.60 | 15.28 -> 15.20 | 5.20 -> 4.00 |
| shotgun 45 (light, 0.10), all pellets in | 28.80 -> 27.00 | 38.92 -> 38.25 | 42.98 -> 42.75 | 14.62 -> 11.25 |
| flamethrower 32 (light, 0.30) | 23.04 -> 19.20 | 28.64 -> 27.20 | 30.88 -> 30.40 | 15.20 -> 8.00 |
| M16 18 (medium, 0.20) | 12.24 = | 15.84 = | 17.28 = | 7.20 -> 4.50 |
| hammer 65 (medium, 0.60) | 54.60 = | 61.10 = | 63.70 = | **45.50 -> 16.25** |

Unchanged: every weapon against the squad / the PMC fighters, hounds, frostbitten, cutters (all light); the cryo emitter (penetration 0:
nothing to remove); the sniper rifle and the plasma carbine (heavy) against everything; the grenade, the turret, barrels, mines, the
marksman's rifle and every enemy attack (no class limit); turn-based combat.

Damage falloff (against a hound, light 0.10):

| Weapon | 1.5 m | 5 m | 8 m | 10 m | 11.5 m | 15 m | 20 m | 25 m |
|---|---|---|---|---|---|---|---|---|
| shotgun | 40.95 = | 40.95 = | 40.95 = | - | 40.95 -> 28.66 | 40.95 -> 16.38 | - | - |
| pistol | 14.56 = | 8.74 = | - | 1.75 = | - | 1.75 = | 1.75 -> 1.40 | 1.75 -> 1.05 |

Shotgun against the brute: 14.62 -> 11.25 up to 8 m, 7.87 at 11.5 m, 4.50 at 15 m (class rule x falloff).
Reload: shotgun empty -> full 2.4 s -> 4.0 s (0.5 s a shell, firing possible after the first). Recoil: M16 SUPPRESS spray +0.24 deg per
round inside a 0.1 s burst, gone 0.5 s per round after the burst.

A reading of the data for the designer (not a fork): the pistol's own per-cell table (`distance_damage_multipliers` 1 / 0.8 / 0.6 / 0.4 /
0.25 / 0.12 per 1.5 m, from Godot's turn-based grid) already cuts its real-time damage to 12 % beyond ~8 m; the new falloff only bites past
15 m on top of that. If the pistol should do real damage at 10-15 m, that table (not the falloff) is the number to change. Likewise the
hammer against the brute loses most (its 0.60 penetration no longer counts on a heavy body).
