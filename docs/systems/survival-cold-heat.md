# Survival: cold, heat sources, weather idles

> `UColdSurvivalComponent` + `ColdRules` port Godot's cold system; every heat source warms through `UHeatSourceComponent`. Sprint 15 added mobile and
> tactical heat (data in `tactical_heat.json`). The weather idle poses (cold idle, hand warming, sitting at the fire) are driven by `WarmIdleRules`.
>
> Key files:
> - `Survival/ColdRules`, `ColdSurvivalComponent`, `TacticalHeatRules`, `WarmIdleRules`, `Interactables/HeatSourceComponent`, `Interactables/ACampfireActor`
> - Data `Content/Data/Survival/tactical_heat.json`; smokes `ColdSmoke`, `TacticalHeatSmoke`, `WarmIdleSmoke`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Survival and mobile / tactical heat (old §5)

**Survival** — `Survival/ColdRules`, `ColdSurvivalComponent`; `Interactables/HeatSourceComponent`. Godot
`player.gd _process_cold_system`, `warm_zone.gd`.
**Mobile & tactical heat (Sprint 15, TANDEM #14, 2026-10-11, UE-only)** — every source warms through `UHeatSourceComponent`
(`UColdSurvivalComponent::IsNearHeatSource`: warming, warm regeneration, weapon thaw; the beasts' existing fire fear of any active
heat source applies too). Pure `Survival/TacticalHeatRules` (`FTacticalHeatConfig` = Gemini's spec values, `FHeatBurnState`, heat
pack / buffer, campfire wind + craft, puddle zones, `ShouldAdvanceRealTime`) read from `Content/Data/Survival/tactical_heat.json`
(`Data/TacticalHeatTuning`, loaded at StartPlay; `CodexTactics.DumpTacticalHeat`). `UTacticalHeatTimerComponent` (burn time / gas) is
advanced only by `Survival/UTacticalHeatSubsystem`: real time when not in the tactical pause (incl. the free horde pause and its
flyover), not turn-based and not under a dialogue / cutscene (`UWorldAIPauseSubsystem`); one turn-based squad round
(`UTurnBasedCombatSubsystem::OnRoundAdvanced`) burns total / rounds. The subsystem also runs the heat pack (`UseHeatPack`: self or a
mate, 1 AP in turn-based), the flare reveal (`IsRevealedByFlare`, used by `UTacticalSightSubsystem`) and the save of loose sources.
- Heat pack: `EPersonalItem::HeatPack`, `AOperativeCharacter::HeatPacksCount` (stack 3), U / drawer line; dragged onto a cold mate
  (`USquadTransferSubsystem::Execute`, `ACodexTacticsHUD::HandleTransferRequest`) it is used on him; `UColdSurvivalComponent::ApplyHeatPack`
  (-35 points, frostbite cleared -> crouch, 45 s buffer: `FColdEnvironment::AccumulationMultiplier` 0.5).
- Campfire: `Interactables/ACampfireActor` (`EDeployableType::Campfire`; count = what matches + `FirewoodCount` (or 10 flamethrower
  fuel) make), F cycle / drawer, kindled by `OnDeployedBy`; ashes can be rekindled from the menu; patrols within 15 m search
  (`AlertPatrols`, every 10 s); wind on high ground (base Z >= 140) -20 % burn time.
- Portable gas stove: `Interactables/APortableStoveActor` (`EDeployableType::PortableStove`, max 1 each, `StoveGasSeconds` travels
  with the item: `AOperativeCharacter::MoveDeployableTo`, `OnDeployedBy` / `OnPickedUpBy`); «Pack up» = the deployable pick-up
  (1.5 s, into the packer's inventory). Silent, no smoke, windproof; not auto-recovered after a wave.
- Signal flare: `Combat/ASignalFlareActor` (base `Survival/ATacticalHeatActor`), `FlaresCount`; Shift + G / the weapon selector's 5th
  line / drawer: `UGrenadeSubsystem::StartFlareAim` (grenade rings and arc, 20 m standing / 15 m crouched), `ThrowFlareAt` (throw clip),
  a click within 2 m = `LightFlareAtFeet`. Refused in turn-based combat.
- Fire puddle: `Combat/AFirePuddleActor` (core 1.2 m: Fire + Burning pulses every 0.5 s; ring 3.5 m: `UHeatSourceComponent::WarmthRate`
  12 %/s, `FColdEnvironment::WarmthRateOverride`), 12-15 s / 1 round; `ABarrelActor::Explode` / `IgniteForTurnBased` leave one.
- Visual slots: placeholder engine shapes + point lights; Niagara slots load `/Game/VFX/Heat/NS_Campfire|NS_PortableStove|NS_SignalFlare|NS_FirePuddle`
  when Gemini creates them (TANDEM request #15); game-mode classes `CampfireClass` / `PortableStoveClass` / `SignalFlareClass` / `FirePuddleClass`.

## Tactical heat data (old §6)

- **Tactical heat data (Sprint 15, 2026-10-11).** `Content/Data/Survival/tactical_heat.json` (staged as UFS) holds every value of
  `FTacticalHeatConfig` (snake_case keys, cm / s / cold points / AP): heat pack 35 / 45 s / x0.5 / 1 AP / stack 3; campfire 350 cm /
  75 s / 4 rounds / wind x0.8 from Z 140 / 1 match + 1 firewood or 10 fuel / noise 15 m every 10 s / 3 AP; stove 220 cm / 180 s gas /
  15 s per round / 2 AP / pack-up 1.5 s / 1 each; flare 180 cm / 40 s / 2 rounds / throw 20 m / at feet within 2 m / reveal 10 m /
  3 carried; puddle core 120 / ring 350 cm / 12 %/s / 12-15 s / 1 round / 10 dps (pulse 0.5 s) + Burning 3 s x 5 / barrel puddle on;
  new sources' heat tolerance 0 (old sources keep Godot's 50 cm). `starting_loadout` per role (commander 1 pack + 1 firewood + 1 flare,
  engineer 1 pack + 1 firewood + the stove (full canister), medic-sapper 2 packs + 1 flare). Loot: `FLootContents` HeatPacks 1 / Firewood 1 /
  Flares 0 per crate by default (editable per crate). Save keys (squad): `heat_packs_count`, `firewood_count`, `flares_count`,
  `stoves_count`, `stove_gas_seconds`, `heat_buffer_left`; objects: `heat_burning` / `heat_burnt_out` / `heat_time_left` /
  `heat_round_seconds`; world: `heat_sources` (flares, puddles).

## Weather idle poses, warming multiplier, squad gathers at the fire (old §6)

### Weather idle poses: cold idle, hand warming, sitting at the fire (user request 2026-10-12)

**Pack:** `/Game/90+_Weather_Animations` (untracked pack in the MAIN folder, junction in agents). NEW_version/Cold = 35 clips
(Hot 35 / Storm 24 / OLD_version 52 are for the later weather + day/night work). Sets (`AS_He_*` male, `AS_She_*` female): `Cold_1`
(standing, shivering), `Cold_Fire_1` (standing, hands out to a fire), `Cold_Fire_2` (sitting on the ground at a fire) - measured, loop
pelvis He 92 / 92 / 13 cm, She 96 / 95 / 35 cm. Each set: 1Start, 2Loop, 3Tranzit, 4Loop, 5Tranzit, 6End; exceptions: She Cold_Fire_1/2
name the second loop `5Loop`, **She Cold_1 has no 6End** (the pose blends out). No clip has root motion.
**Retarget:** the pack sits on its own UE5 Manny skeleton (161 bones). Bone-name playback on the UE4 Post_Apo skeleton (like the cover /
sniper clips) was measured wrong for these poses: reference poses differ (clavicle / upper arm ~24 deg, spine_01..03 7-11 deg local rot),
the sitting loop leaned the torso ~40 cm back and lifted the hands to the chest. So `Scripts/Editor/setup_operative_weather_animation.py`
IK-retargets the 35 clips with the M4 pack's `RTG_UE5Manny_UE4Manny` (its "Blend to Source" op switched off IN MEMORY - with it the
knees bent to ~85 % on the taller Post_Apo / Female meshes and her pelvis dropped 13 cm; the shared asset is never saved) onto
`SK_Post_Apo_Survivor_a` -> `/Game/Characters/Operatives/Anims/Weather/Male/AS_He_*` and `SK_Female_soldier` -> `.../Weather/Female/AS_She_*`.
Result per clip in `Saved/Logs/setup_operative_weather_animation.txt` (pack vs retargeted pelvis / head / hands / lowest foot / leg
extension: legs 0.91-1.01 of the mesh vs 0.94-1.02 in the pack, feet on the mesh's reference ground, hands within ~10 cm, scaled to the
taller bodies). No skeleton, no pack asset is modified. Re-run: `CODEX_RESET_WEATHER_CLIPS=1` (re-retargets + rewrites the class defaults),
`CODEX_WEATHER_DRYRUN=1` measures only.
**C++ (no graph change):** `UOperativeAnimInstance` class defaults `ColdIdleMale/Female`, `HandWarmMale/Female`, `SitWarmMale/Female`
(`FWarmIdleClipSet`: Start, LoopA, TransitAB, LoopB, TransitBA, End), `WarmIdleLoopsMin/Max` (2-4 loops before a transit),
`WarmIdleBlendSeconds` 0.3, `WarmIdleInterruptBlendSeconds` 0.2, `WarmIdleStillSpeed` 15, `bHideWeaponInWarmIdle` (the clips use both
hands; the rifle comes back when the pose ends), `bUseWarmIdleClips`. The layer (`OperativeAnimInstanceWarmIdle.cpp`) plays the set on
FullBodySlot; the female set is chosen when the mesh or skeleton name contains "Female" (recruits / Susanin = male). Graph hooks:
`bWarmIdleActive`, `WarmIdlePose`. **Nothing for you to do in the ABP** (the FullBody slot plays it); a later polish could put the
weapon on a back socket instead of hiding it.
**Rules (`Survival/WarmIdleRules`, `UWarmIdleSubsystem`):** exploration (and the post-combat return) only - never in a wave fight /
ambush / tactical pause / turn-based / cutscene / preparation. Gate: alive, not knocked down / frostbitten / in cover / carrying /
vaulting / panicking / raging, no task or use order, not moving, no threat (enemy in his cone / combat target / attacked in the last 5 s),
idle `warm_idle_delay_seconds` 2. Cold idle: no warm zone, standing, cold >= `warm_idle_cold_threshold` 40 (Chills; ends 5 below).
Barrel: everyone idle in its zone walks to a hand-warming place on a ring round it (`barrel_stand_distance` 125 cm, outside the 1.2 m
drum), faces it, `Cold_Fire_1`. Campfire / gas stove (user design): roles by cold - the coldest `warm_min_sitters` 2 (or all if fewer)
plus everyone >= `warm_extra_sitter_cold` 50 sit on the inner ring (campfire 115 / stove 95 cm; capped by the free seats), the rest stand
on the outer ring (175 / 150 cm, between the seats) warming their hands; roles are re-evaluated by the current cold when someone ARRIVES
or the fire is relit; when someone leaves the others keep their roles (only the coldest stander sits down if fewer than 2 remain). Spots:
`warm_spot_spacing` 85 cm apart, spread round the fire (the free slot farthest in angle from the taken ones), projected on the NavMesh and
probed for blocking geometry (blocked / unreachable spots skipped; after 3 failed walks he just stands in the zone), at least the source's
box + a capsule away; walk 160 cm/s; he faces the fire before the pose starts. Orders: any `OrderMoveTo` (player, interaction,
relocation) ends only THAT operative's pose at once (quick 0.2 s blend-out, no busy time); a parked follower keeps his spot while the
leader stays in that fire's warm zone (`USquadSubsystem::UpdateFollower` -> `ShouldFollowerHoldWarmSpot`), the leader leaving it takes
the squad along. Combat start / warm enough / fire out: the End clip (stands up / lowers the arms); threat / move: quick blend-out;
knockdown / death / cover: cut.
**Warming multiplier:** heat-source warming x `exploration_warmth_multiplier` 1.0, x `combat_warmth_multiplier` **0.5** in a wave fight /
ambush (any combat mode; a plain source 8 %/s x fortitude boost -> 4.6 %/s for the commander; preparation / closed-zone warming without a
heat source is not scaled). All values in `Content/Data/Survival/tactical_heat.json` (Unreal master; `FTacticalHeatConfig`).
**Verify:** `CodexTactics.Survival.WarmIdle.*` (8 tests), `WarmIdleSmoke` (cold idle He / She + order cut, barrel hand warming + End clip,
campfire 3 operatives: the two coldest sit on distinct inner seats facing the fire, the warmest stands on the outer ring, no flicker,
x1 warming; an order to one leaves the others seated; stove 2 sit / 1 stands; wave fight: End clips, no poses, x0.5 warming).
`WarmIdleSmoke shot` (rendered offscreen, `-RenderOffscreen -ResX=1280 -ResY=720`) saves stills to `Saved/Screenshots/WeatherAnim/`.

**Squad gathers at the fire (user decision + feedback 2026-10-12):** in EXPLORATION formation (`IsFormationActive`), while the LEADER holds a
warm spot at a barrel / campfire / stove (`AOperativeCharacter::WarmSpotSource`, i.e. he stood idle 2 s in the zone), `USquadSubsystem::UpdateFollower`
(at the repath rate) asks `UWarmIdleSubsystem::GetFollowerPullTarget` and walks the follower to a free place on that source's OUTER (stand) ring
instead of his formation slot (`WarmSpotWalkSpeed`); inside the zone he parks facing the fire, 2 s idle later the normal flow above gives him
his role (the coldest `warm_min_sitters` / >= `warm_extra_sitter_cold` sit on the inner ring, the rest stand on the outer ring right beside
them) and spot. So the formation radius effectively contracts to the fire's rings; campfire, stove and barrel (stand ring) behave the same.
Rule `WarmIdleRules::ShouldPullFollower` (`FWarmPullInput`): `warm_pull_followers` on, exploration (not combat / ambush / pause / turn-based /
cutscene: `IsExplorationPhase` + world AI pause), follower in the formation (solo mode and held followers never reach `UpdateFollower`, guards (T)
are not followers), leader at the fire, follower free (`CanHoldWarmPose`: alive, up, not frostbitten / in cover / carrying / vaulting / busy /
threatened / prone), no explicit order since the pull began (`GetMoveOrderSerial` changed -> that follower is opted out of THAT fire until the leader
leaves its zone), follower <= `warm_pull_max_distance` (1500 cm) from the source at the START (a running pull survives detours). The leader leaving the
warm zone ends the pull (followers follow the formation again; they stand up with the End clip). A fire that went out (barrel burnt out, stove spent) ends the pull at once (BarrelSmoke caught followers parked round a dead barrel). A pull the squad stops asking about lapses
after 3 s; it also ends at once outside exploration. Data: `tactical_heat.json` `warm_pull_followers` (bool), `warm_pull_max_distance`.
**Verify:** rule test `CodexTactics.Survival.WarmIdle.PullFollowers`; `WarmIdleSmoke` scenes 7 (campfire: the leader walks up to it, followers 9 / 11 m away
in formation -> both come in, the two coldest sit, the warmest stands on the outer ring beside them; leader walks away -> they follow again; then the
same at the gas stove; an order to one follower, a guard (T), solo mode keep that one out; wave fight pulls nobody).
**Smoke note (WarmIdleSmoke "squad of three", 2026-10-12):** the Medic-Sapper (max HP 120; Commander 140, Engineer 150 - the same with or without the user's
`BP_Operative_FemaleSoldier`) burned to death ~1 s after sitting at the stove: the barrel stage leaves a fire puddle (core 120 cm, 12-15 s, 10 dmg/s + Burning)
exactly where the later stages put the squad. Not a BP value: the smoke now removes puddles and heals the squad between stages (`ResetBodies`, logs the HP),
and `UHealthComponent::Die` logs `[Health] <actor> died (source '...', max HP ..) at ..` (no more silent deaths).

