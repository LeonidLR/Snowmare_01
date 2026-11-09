# Weapons: data, the unified schema, who reads it

> Every weapon's numbers live in `Content/Data/Weapons/weapons_tuning.json` (the master copy, Wave Editor «Оружие»), are applied and
> validated at StartPlay onto `UWeaponDataAsset::Stats` (`FWeaponStats`, the unified schema of the weapons framework, 2026-11-08), and
> every gameplay reader - the squad, the hostile PMC fighters, the turn-based grid, the bot, the HUD - reads `Stats`. The asset's own
> flat fields are only the fallback layer. Design: [WEAPONS_FRAMEWORK.md](../design/WEAPONS_FRAMEWORK.md) (fields, kinds, armour classes,
> migration table, open questions). Combat formulas that consume the numbers: [combat-and-fire](combat-and-fire.md).
>
> Key files:
> - `Data/WeaponStats.h` (structs + enums `EWeaponKind`, `EArmorClass`, `EWeaponReloadMode`, `EWeaponHandling`), `Data/WeaponStatsRules`
>   (JSON <-> stats, `Validate`, enum names), `Data/WeaponDataAsset` (`Stats`, `MakeBaseStats`, `RebuildStatsFromAsset`),
>   `Data/WeaponTuning` (`ApplyFile` / `ApplyWeapon` / `GetLastReport` / `Dump`, console `CodexTactics.DumpWeaponTuning`)
> - Tests: `CodexTactics.Weapons.Schema.*` (EveryWeaponLoads, ParityWithLegacyPipeline, ProjectFileApplies, RejectsBadData, RoundTrip,
>   NoLegacyReads), `CodexTactics.Data.WeaponTuning.*`, `CodexTactics.Data.WeaponImportParity`
> - Smokes that exercise the readers: `QuickFireSmoke`, `SuppressionSmoke`, `SniperSmoke`, `WeaponReachSmoke`, `SquadFireSmoke`,
>   `TurnBasedSmoke`, `PmcStandoffSmoke`

## The weapons framework, step 1: the schema (2026-11-08, branch `feat/weapons-framework`)

- **Layers.** Asset base fields (built into `Stats` on load / creation / editor edit) -> `weapons_tuning.json` at StartPlay
  (`WeaponTuning::ApplyFile`: each weapon starts from its base, the file's block is parsed by `WeaponStatsRules::ApplyJson` onto a copy,
  `Validate`d, and only valid data replaces `Stats`). `Stats` is `Transient`: never saved into the `.uasset`.
- **File.** Root `schema_version: 2`; per weapon the old flat keys (unchanged, the Wave Editor edits them) + `kind` (hitscan / projectile /
  thrown / melee / beam). Optional keys the schema also reads: `handling`, `damage_type`, `status_effect`, `attack_shape`, `uses_ammo`,
  `self_cold_generation`, `self_warmth_generation`; reserved (Nov 9+): `penetration_class`, `damage_falloff_floor`, `min_range_m`,
  `reload_mode`, `reload_per_round_s`, `spread_scale`, `recoil_per_round_deg`, `recoil_recovery_s`, blocks `pellets`, `projectile`,
  `explosion`. The Training Ground keeps unknown keys when it saves.
- **Log.** `Weapon tuning: 10 weapons + grenades from <path> (schema v2, 0 rejected)`; a rejected block logs
  `"<id>" REJECTED, the asset's own values stay: <problems>`; an unknown key logs a typo warning.
- **Guard.** Do not read the asset's flat fields (`CurrentWeapon->AttackRangeCm`, ...) in gameplay code: read
  `CurrentWeapon->Stats.Range.MaxRangeCm`. `CodexTactics.Weapons.Schema.NoLegacyReads` scans `Source/CodexTactics*` and fails on such a read.
- **Moved readers (identical numbers):** `OperativeCharacter{Combat,QuickFire,Suppression,Weapons,Sniper,Overlays,CornerAim}.cpp`,
  `SquadAutonomySubsystem`, `PmcSquadSubsystem`, `SquadOrderCatalog`, `PlaytestBotSubsystem`, `TurnBasedRules`,
  `TurnBasedCombatSubsystem`, `CodexTacticsPlayerControllerItems`, `ActionBarWidget`, the dev smokes that read weapon numbers.
- **Not moved (own data, planned):** the Marksman's `enemy_weapons.marksman_rifle` (with the sniper move this week), the APC
  `vehicle_weapons` (week 4), the operative's real-time grenade values (root `grenade` block; week 2 explosion code).

## Step 2: ballistics + armour penetration (2026-11-08, the game designer's decisions; balance changes on purpose)

- **Body armour** from `weapons_tuning.json` "armor.bodies" (class + reduction; `ArmorRules`), applied as an enemy / operative spawns;
  `UHealthComponent::ArmorClass`. **Class rule:** a weapon hit whose `penetration_class` is below the body's class gets no
  `armor_penetration` (`FDamageSpec::PenetrationClass`; non-weapon damage has no limit). Formula still multiplicative.
- **Damage falloff** (`WeaponBallisticsRules::DamageFalloffMultiplier`) in `ApplyRangedHit`: shotgun x0.4, pistol x0.6 at the max reach.
- **Shotgun pellets** in the aimed shot (`RollPelletHits`, `GetLastShotPelletHits()`); **per-shell reload** in `StartReload` / the
  reload tick (interrupted between shells by a target / quick fire / SUPPRESS); **M16 full-auto recoil** bloom in the SUPPRESS order
  (`RecoilBloomDeg`, `SettleRecoil`).
- Every changed number: `docs/design/WEAPONS_FRAMEWORK.md` 12.2. Tests `CodexTactics.Weapons.Armour.*` / `.Ballistics.*`.

## The roster (game designer decisions 2026-11-08)

- **In play:** M16 + grenade (the grenade unchanged). **Next:** shotgun -> pistol -> sniper rifle + RPG-7 (pistol / sniper keep their
  current data and places until remade). **Out of play:** hammer, flamethrower, knife, cryo emitter, plasma carbine.
- `weapons_tuning.json` "roster" (`WeaponRoster`, lazily read before StartPlay); the five left the "weapons" block; their `DA_Weapon_*`
  assets stay on disk, unreferenced. The starting arsenal is M16 + pistol + grenade (+ the Medic-Sapper's sniper rifle); PMC fighters
  draw from `pmc_operators.json` weapon_pool (m16 / shotgun / sniper_rifle, unchanged).
- Gate `WeaponRoster::IsOutOfPlay`: `InitArsenal` / `AddArsenalWeapon` / `SwitchToWeaponById` refuse them; `DropOutOfPlayWeapons` cleans
  an operative after `InitArsenal`, a save load and the campaign carry (the M16 in hands); `FLootContents::GetCount` returns 0 for
  fuel / coolant / plasma cells and an out-of-play bonus weapon; `TransferRules::GetAvailable` 0 for their ammo;
  `CorpseLootRules::IsLootableWeapon` false; the weapon selector hides the knife line; the campfire text drops the fuel option.
- TRAP: a new weapon path must ask `WeaponRoster::IsOutOfPlay` (or go through `InitArsenal` / `SwitchToWeaponById`); a removed weapon id
  must go to "roster.out", never only out of a list in code.

## The D-1 shotgun: the Engineer's primary weapon (game designer decisions 21 / 21+, 2026-11-03 / 11-08; branch `feat/weapons-framework`)

- **Identity.** Stable id `shotgun` (saves, loadouts, `DA_Weapon_shotgun`, PMC weapon_pool unchanged); `weapons_tuning.json` "name"
  "D-1 Shotgun" -> `UWeaponDataAsset::WeaponName` (HUD / selector). No save migration needed: the id did not change.
- **Numbers.** 45 damage per shot at point blank split over 8 pellets (5.625 each, `pellets.count` 8, cone 6 deg), full up to the effective
  range 8 m, falloff to x0.4 at the 15 m reach (`damage_falloff_floor` 0.4), penetration class light, clip 8 + reserve 40, per-shell reload
  0.5 s (`reload_mode` per_round; empty -> full 4.0 s), interruptible between shells.
- **Loadouts (data).** `weapons_tuning.json` "loadouts" (`Data/WeaponLoadoutRules`): commander m16 / pistol / grenade, engineer
  **shotgun / pistol / grenade (no M16)**, medic_sapper m16 / sniper_rifle / pistol / grenade; the first id is in hands. The game mode's
  `SpawnOperative` uses it for the player's squad (team 0) roles it lists (`ExtraWeapons` skipped then); other roles keep `StartingArsenal`.
  Out-of-play ids are dropped. Fallbacks that used to hard-code the M16 now take the operative's primary weapon
  (`AOperativeCharacter::GetPrimaryWeaponId` / `TakePrimaryWeapon` / `EquipRestoredWeapon`): a save load / the campaign carry restore the
  saved weapon if owned (`WeaponLoadoutRules::ResolveHeldWeapon`), the grenade throw and a cancelled aim go back to the primary, an empty
  weapon switches to the primary.
- **Model.** "visual" block: `hand_mesh` (static or **skeletal**: the Fab D-1 import is a skeletal mesh -> `WeaponSkeletalMesh`, a child of
  `WeaponMesh`), `attach_location` / `attach_rotation_pyr` / `attach_scale` (relative to the hand_r weapon socket; the import is ~100x scale,
  so x0.0118), `muzzle_offset` (mesh space). The Fab asset `/Game/Fab/_Model_D-1__Shotgun` is read-only; its second skeletal mesh (the pump,
  Object_29) is not used.
- **Animation.** The shotgun weapon layer ([animation-setup](animation-setup.md) "Shotgun weapon layer"): `handling: "shotgun"`
  (`EWeaponHandling::Shotgun`) switches any operative / PMC fighter holding it to the ShotgunLocomotionPack set.
- **Checks.** Tests `CodexTactics.Weapons.Loadout.*`, `CodexTactics.Animation.WeaponLayer.*`; `CodexTactics.ShotgunSmoke` (loadout, model,
  pellets / falloff at 3 / 8 / 14.5 m, partial pellet hits, per-shell reload + interruption, the layer, a PMC fighter's layer, the #51 probe);
  rendered proof `CodexTactics.ShotgunD1Shots` -> `Saved/Screenshots/ShotgunD1/`.

## The pistol (the weapons week, "the whole pistol", 2026-11-08; the designer's numbers pending his confirmation, data only)

- **Damage by distance.** Full up to 10 m (`effective_range_m` 10, was 15), then a straight ramp down to x0.6 at the 25 m reach
  (`damage_falloff_floor` 0.6). The Godot per-cell table (`distance_damage_multipliers` 1 / 0.8 / 0.6 / 0.4 / 0.25 / 0.12 per 1.5 m, which cut it
  to 12 % past ~8 m) is gone (`[]`). Against the hound (light armour 0.10): 14.56 at 5 / 10 m (was 8.74 at 5 m, 1.75 at 10 m), 11.45 at 18 m,
  8.93 at 24.5 m. Armour class light (unchanged).
- **Turn-based.** `TurnBasedRules::GetDamageForDistance`: a weapon WITHOUT a per-cell table takes the real-time falloff curve at the cells'
  distance (1.5 m a cell) - the pistol's 6-cell grid reach (9 m) is all full damage now (was 1 / 0.8 / 0.6 / 0.4 / 0.25 / 0.12). Weapons that
  keep a table (M16, sniper rifle) are unchanged; the D-1 (no table, 5 cells = 7.5 m < its 8 m effective range) stays full.
- **Handling** `pistol` (`EWeaponHandling::Pistol`): the pistol set of the weapon layer ([animation-setup](animation-setup.md) "Pistol weapon layer").
- **The sidearm switch** (`Combat/SidearmRules`, `weapons_tuning.json` "sidearm": enabled, weapon pistol, `threat_range_m` 10, `draw_s` 0.35,
  `holster_after_s` 3): when the primary (the M16, the Engineer's D-1) runs dry and its reload starts with a live enemy within 10 m (his combat
  target, any monster, any fighter of another team) the operative draws the pistol instead of standing through the reload (`TryDrawSidearm`,
  from the combat tick; no shot for the draw time, the equip clip plays); after 3 s without a target (or with the pistol dry) he goes back to the
  primary, which then reloads (`UpdateSidearmReturn`). A tactical top-up (rounds still in the clip) never switches; an empty primary with no
  reserve left still switches for good (`AutoSwitchOnEmpty`). The player's own controls are unchanged (X cycles the weapons, the weapon
  selector). Every operative carries the pistol (12 + 24).
- **Model (2026-11-09).** The optimized Pistol 9mm from the art package (ArtPipeline/Weapons/Pistol9mm, one material / 1K atlas; imported by
  `Scripts/Editor/import_optimized_weapons.py` into `/Game/Weapons/Pistol9mm/` + `Attachments/`, 9.2 MB): the operatives hold
  `SM_Pistol9mm_Pistol` (3,000 tris) + `SM_Pistol9mm_Magazine` ("visual.hand_mesh_parts": extra static parts attached to the hand model at identity,
  `AOperativeCharacter::WeaponMeshParts`). Every part shares the grip-centre pivot (muzzle +X, up +Z), so the attachments (suppressors 1-3, laser,
  flashlights 1-2, red dot, picatinny 1-2; suppressor 2 / 3 poses estimated by the art agent) mount at identity when the customisation comes.
  Grip transform from the retargeted aim pose (`Scripts/Editor/calibrate_pistol_grip.py`), muzzle at x 14.6 / z 6.4 (the bore). No emissive yet
  (the designer's decision pending). The D-1 likewise: `/Game/Weapons/ShotgunD1/SK_ShotgunD1_1slot` (4,500 tris, MI_ShotgunD1, 3.2 MB) on the
  Fab pack's original skeleton (read only); the Fab pack is untouched.
- **Checks.** Tests `CodexTactics.Weapons.Sidearm.Rules`, `CodexTactics.Weapons.Pistol.DamageByDistance` (real time + turn-based);
  `CodexTactics.PistolSmoke` (loadout, the switch + equip clip + layer, damage at 5 / 10 / 18 / 24.5 m, reload, the sidearm draw and return,
  a PMC fighter's layer); rendered proof `CodexTactics.ShotgunD1Shots pistol` -> `Saved/Screenshots/PistolD/`.

## The sniper rifle on the framework (the Medic-Sapper's rifle, "Roger's sniper rifle" in the plan; 2026-11-09)

- **Data (unchanged numbers, now explicit):** `weapons.sniper_rifle` - `handling: sniper_rifle` (kneel / prone only, the bolt: `SniperRules`,
  `OperativeCharacterSniper.cpp`), armour class **heavy** (decision 2026-11-08), 70 damage, armour penetration 0.6, reach 120 m / effective 90 m,
  2.4 s between shots, 5 rounds + 15, reload 3.5 s, no damage falloff (cell table all 1). Against the Brute (heavy 0.75) the heavy class pierces:
  0.75 x (1 - 0.6) = 30 % stopped (a light weapon: 75 %) - measured in SniperSmoke as x2.80 of the same shot with a light class.
- **The Marksman enemy's rifle on the schema:** `enemy_weapons.marksman_rifle` now carries schema keys for its ballistic part - `kind`,
  `base_damage` (was `damage`), `fire_rate` (the shot interval, was `shot_cooldown`), `effective_range_m`, `attack_range_m` (was `max_range_m`) -
  parsed and validated by `WeaponStatsRules` (`WeaponTuning::ApplyMarksmanRifleJson`, `MarksmanRifleStats`) and written into `FMarksmanConfig`;
  the AI part (base accuracy, aim time, crit, stance bonuses, the preferred band) and `tb_*` stay the enemy's keys. Same numbers (45 / 2.5 s /
  80 / 110 m / 0.6). Old files with the old key names still load; an invalid schema part is rejected (the rifle keeps its values). The Training
  Ground's enemy-weapon panel edits the new key names. His shot keeps the enemy-attack formula (no armour-class limit).
- **PMC fighters** draw the sniper rifle from `pmc_operators.json` weapon_pool: the same `DA_Weapon_sniper_rifle`, the same sniper handling
  (no shot standing).
- **Animation:** the sniper keeps its own native pose set (`OperativeAnimInstanceSniper.cpp`: stand aim idle, kneel / prone aim, fire, bolt,
  magazine reload) - not the weapon layer (the layer is off in the sniper pose); nothing new needed.
- **The sidearm** also covers her: an empty sniper magazine with an enemy within 10 m draws the pistol.
- **Checks:** tests `CodexTactics.Weapons.Sniper.RifleData`, `.MarksmanOnSchema`; `CodexTactics.SniperSmoke` part 8 (the same damage at 10 / 30 m,
  the Brute, the Marksman's config from the schema, a PMC fighter's rifle); render `CodexTactics.ShotgunD1Shots sniper` -> `Saved/Screenshots/SniperRifle/`.

## The shared explosion model (2026-11-09; `docs/design/WEAPONS_FRAMEWORK.md` 16)

- **One function for every blast:** `Explosions::Detonate(World, FExplosionSpec)` (`Combat/Explosion.h`) - the grenade (`AGrenadeActor::ApplyAreaEffect`),
  the placed charges (`AInteractableActor::ApplyBlast`: fuel barrel, proximity mine, tripwire, trapped objects / crates / turrets / barricades) and
  the projectiles. It applies the falloff to enemies / the squad / other bodies, explosive damage = armour class Vehicle (decision 1), the
  status, chained barrels / traps, the vehicles' hulls, the noise / patrol alert, the knockdown (2.5 m), the explosion event (snow craters +
  the suppression report) and `Explosions::OnDetonated()` (the camera shake; a knockback hook later). Returns the victims (`FExplosionResult`).
- **Parity:** `GrenadeSpec` / `ChargeSpec` build the old numbers (grenade half-at-the-edge curve, the squad x0.65, every actor in reach;
  charges: enemies x0.4 / squad x0.5 linear curves, the trap event); `CodexTactics.Weapons.Explosion.Parity` compares them bit-exact with
  `GrenadeRules::DamageFalloff` / `DeployableRules::GetBlastDamage` at every caller's numbers. The vehicle cannon's splash keeps its own
  direct-hit / kill-credit code (`VehicleTurretComponent`; not a blast of this model).
- **Pure rules** (`ExplosionRules`): the falloff family (`HalfAtEdge`, `Linear`, `FullCore` from a weapon's `explosion` block), the cover rule,
  the RPG-7 back-blast cone (`BackblastMultiplier`: 1 at the launcher, 0 at `back_blast_length_m` / outside `back_blast_half_angle_deg`), the
  camera trauma (linear to 0 at 4 radii).
- **Cover:** `weapons_tuning.json` "explosions": a wall between the blast and a body x`wall_multiplier` (0.25; a Visibility trace from 50 cm
  above the centre to the body's chest); a plunging round (high-angle) ignores walls - only a roof above the body (a trace 10 m up from its
  head) protects, x`roof_multiplier` (0). `occlusion` switches the rule per blast kind: on for `projectile`, off for grenade / barrel / mine /
  tripwire / trap (their numbers stay as they were; the designer can turn it on in data).
- **Camera shake:** `explosions.camera_trauma` (0 = off, as before) - the tactical camera adds it at its focus on every blast, in real time
  too (`ATacticalCameraPawn::HandleExplosion`).

## The shared projectile layer (2026-11-09; no RPG-7 yet)

- **Schema:** `kind: projectile`, block `projectile`: `speed_mps`, `gravity_scale`, `arming_distance_m`, `max_flight_s` (0 = 10 s),
  `trajectory` `flat` / `ballistic` / `high_angle` (the old `high_angle: true` still reads as the high-angle trajectory); a ballistic /
  high-angle round needs `gravity_scale > 0`. The mortar's band is the core `min_range_m` / `attack_range_m` (30 / 250 m,
  `ProjectileRules::IsWithinFireBand`).
- **Flight** (`ProjectileRules`): `SolveLaunch` (flat = straight at the aim point; ballistic = the low arc; high-angle = the high plunging arc;
  false when out of reach), fixed 1/120 s steps with exact constant-gravity kinematics (the same points at any frame rate; at most 0.5 s per
  frame), arming by the flown distance.
- **Actor + pool:** `UProjectileSubsystem::Fire` / `FireAt(FProjectileLaunch, Target)` take a parked `AProjectileActor` (or spawn one). Each step
  sweeps a sphere (8 cm) against WorldStatic / WorldDynamic / Pawn / PhysicsBody / Vehicle / Destructible, the shooter ignored. Armed impact:
  the direct share on the actor struck + `Explosions::Detonate` (`AProjectileActor::BlastSpecFor`: every actor in reach, the weapon's full-core
  curve, plunging for a high-angle round); unarmed: a dud (the direct share as kinetic damage); past `max_flight_s`: an air burst (armed) or
  gone. `OnProjectileFinished` reports each flight's end; the round goes back to the pool.
- **Checks:** tests `CodexTactics.Weapons.Explosion.*` (Falloff, Occlusion, ArmorClass, Parity, Backblast) and `CodexTactics.Weapons.Projectile.*`
  (Launch, FixedStep, Schema); `CodexTactics.ProjectileSmoke` (a flat round at a hound 25 m away, a high-angle round at a hound 40 m away, the
  pool reuse; a projectile blast behind a wall x0.25, a plunging one ignoring the wall, a roof stopping it, the grenade ignoring the wall).

## The RPG-7 (2026-11-09; the shared layer's first weapon; placeholder model; THE GAME DESIGNER'S NUMBERS 2026-11-09)

- **Data** (`weapons_tuning.json` "weapons.rpg7", asset `/Game/Data/Weapons/DA_Weapon_rpg7`): `handling: launcher`, `kind: projectile`, a
  flat rocket at **115 m/s**, the fuse **armed at once** (`arming_distance_m` 0), self-destruct after 4.5 s; a **direct hit = 140** on the
  target struck (`explosion.direct_hit_damage` - its whole share, the blast leaves it out: 140, not 140 + the splash); everyone else within the
  **4 m** blast takes the **splash 60** (`base_damage`; full to 1.5 m, x0.15 at the edge); explosive, class **vehicle** (hurts hulls, decision 1;
  a hull struck takes the 140 through its hull rules, no second hull hit from the blast), armour penetration 0.8; one rocket in the tube,
  3 in reserve, 4.5 s reload (it starts by itself after the shot); reach 150 m, effective 100 m.
- **No back-blast damage, no back-blast / stance refusals** (the designer, 2026-11-09): mates behind the shooter are safe, he may fire with a
  wall right behind him, from a closed room and prone. `back_blast_length_m` / `back_blast_half_angle_deg` stay for a later puff visual only
  (the schema keys `back_blast_damage` / `back_blast_clearance_m` / `back_blast_refuses` are gone).
- **The one refusal left:** a target closer than the minimum range (`min_range_m` 10 m; `LauncherRules::MinimumRangeCm` never lets it fall
  inside the rocket's own blast radius, so the shooter never catches his own splash). Refusals in order: empty, too close. A miss (the weapon's
  hit chance) puts the aim point 1.5 - 3.5 m off; the rocket still explodes where it lands.
- **Who carries it:** any fighter can be the grenadier (the designer decides later): it is in the roster as `next` and in **no loadout yet**.
  Dev: `CodexTactics.GiveWeapon rpg7 [squad index]` (the leader by default) adds it to the arsenal and puts it in his hands.
- **Code:** `LauncherRules` (pure: `MinimumRangeCm`, `Refusal`, `DamageOn`, `MissPoint`), `AOperativeCharacter::FireLauncherAt` /
  `LauncherShotAllowed` (`OperativeCharacterLauncher.cpp`; `ShootAtTarget` branches to it for a launcher) on `UProjectileSubsystem` +
  `Explosions::Detonate` (`FExplosionSpec::StruckActor`: the actor a round's direct hit already struck is left out of its blast - any
  projectile with `direct_hit_damage` > 0). The animation is the rifle's for now. Not yet: the AI using it, turn-based specifics, the real model.
- **Checks:** tests `CodexTactics.Weapons.Rpg.Data` / `.Rules`; `CodexTactics.RpgSmoke` (30 m: the direct hit 140 is the struck dummy's whole
  damage, a dummy 2.5 m beside it takes the splash <= 60; no back-blast damage on a mate 2.5 m behind; one rocket reloaded; 80 m hit; a wall
  behind / a closed room / prone fire; a target 5 m away refused).

## Mortar nests (2026-11-09; SQUAD_SIX_FIRE_TEAMS.md 7.3; placeholder visuals; numbers proposed, pending the designer)

- **Fixed nests, both sides, not carried:** `AMortarNestActor`, spawned by `UMortarSubsystem` from the level JSON `"mortar_nests"`
  (`[{"id", "side": "squad" | "pmc", "team": N, "pos": [x, y(, z)], "yaw", "crew": N, "ammo": N}]`; no z = dropped on the ground; a PMC nest
  with `crew` gets its own commanded PMC team at its places). The combat lab (`L_FormationLabCombat.json`) has two test nests: `MORTAR_SQUAD`
  near the start and an UNMANNED `MORTAR_PMC` in the far corner (unmanned so the lab smokes are not shelled; `CodexTactics.MortarCrewPmc 1`
  gives it a crew). No Depot nests yet.
- **Crew of three** (`mortar_nest.crew_needed`): `AssignCrew` sends a man to a free place round the tube (behind / beside it), holds him there
  once he arrives (the formation no longer pulls him); he leaves the crew when he walks off (his own move order) or is more than
  `crew_leave_m` from his place, or dies. Fewer than three at the nest = no fire - every round re-checks. A nest left with no crew of its side
  changes side when the other side mans it (taken).
- **The spotter rule (the same for both sides):** a nest fires only at a point that a living man of its side SEES now
  (`UTacticalSightSubsystem::CanOperativeSee` at the point + `spot_height_m`), re-checked before every round: no spotter = no fire.
- **Range band** 30 m .. 250 m (`min_range_m` / `max_range_m`; 250 m is the designer's guess - the combat lab is 120 x 120 m and the Depot
  ~124 x 177 m, so 250 m reaches anywhere on today's maps; ~120-150 m would leave safe ground, a designer call).
- **The round** (`mortar_nest.round`, the weapon schema): a high-angle projectile (110 m/s, `gravity_scale` 4 - about 5-6 s of flight at any
  range in the band), blast 110 / 6 m (full to 2.5 m), explosive, class vehicle; through `Explosions::Detonate` as a plunging blast - walls and
  side cover do not protect, only a roof. `interval_s` 3.5 between rounds, `rounds_per_order` 6, `ammo` 24.
- **Bracketing (simplified):** the first round of a target lands `first_error_m` 15 m off (a random direction), each next one x `shrink` 0.55
  (8.2, 4.5, 2.5 m ...) down to `min_error_m` 1.5 m; an aim point more than `same_target_m` 10 m from the last one starts the walk again.
- **The player's orders (minimal, console):** `CodexTactics.MortarList`, `CodexTactics.MortarMan [nest] [squad indices]` (no indices = the
  squad), `CodexTactics.MortarFire [nest] <x y [z] | cursor>` (refused with the reason: crew / ammo / too close / too far / nobody sees it),
  `CodexTactics.MortarCease [nest]`. No HUD target picker yet (the grenade aim ghost could show the band later).
- **The PMC commander:** each decision a manned nest of the team that is not firing scores the biggest cluster of enemies the team sees now
  (`MortarRules::BestCluster`, `mortar_cluster_radius_cm` 8 m): `mortar_fire` 0.8 x min(1, cluster / `mortar_cluster_full` 3), 0 out of the
  band; it orders fire at >= `mortar_threshold` 0.3 (one seen man: 0.27, no; two: 0.53, yes) - `pmc_commander.json`. Its crewmen get no
  orders of their own (they stay at the tube).
- **Checks:** tests `CodexTactics.Weapons.Mortar.*` (Checks, Bracketing, Round, PmcCluster, Placement); `CodexTactics.MortarSmoke` (a crew of
  three walks rounds in on a seen point 80 m away - 15 / 8.2 / 4.5 / 2.5 m, 5.6 s flights; the open dummy hurt, the roofed one untouched; a man
  off -> the fire stops, a crew of two refused; a point behind a wall refused (no spotter), 15 m refused; a PMC nest 70 m away shells the
  spotted squad on its commander's own decision).
