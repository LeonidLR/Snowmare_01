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
