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
