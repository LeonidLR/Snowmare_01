# PMC squads: hostile operatives, the shared order catalog, the utility commander and the self-play arena

> User request 2026-11-03: "very smart, human-like PMC fighters, nearly as good as the player - I want to watch a real fight between humanoid
> squads", trained automatically later (the overnight trainer is the NEXT task; this doc gives it the hooks). UE-only, no Godot reference.
> Branch `feat/pmc-squad-ai`.
>
> Key files:
> - Teams: `Characters/OperativeCharacter.h` (`TeamId`, `IsPlayerTeam`), `Private/Characters/OperativeCharacterInternal.h` (`IsLiveTargetOf`, `GatherTargetsFor`)
> - `AI/PmcSquadSubsystem` (registry, team sight, targets, commander, spawning), `AI/PmcCommanderRules` (pure scoring + weights), `AI/PmcOperatorRules`
>   (looks / names / weapons / factions data), `AI/SquadOrderCatalog` (the shared catalog)
> - Data: `Content/Data/AI/pmc_commander.json` (weights = a personality), `Content/Data/AI/pmc_operators.json` (body, looks, weapon pool, stats, factions),
>   `Content/Data/LevelJson/L_PmcArena.json` (the arena's level data for `L_FormationLabCombat`)
> - Body: `Scripts/Editor/setup_pmc_operator_body.py` -> `/Game/Characters/Enemies/PMC/Operator/` (`SKM_PmcOperator`, `SK_PmcOperator_Skeleton`, `MI_PmcOperator_*`)
> - Encounters (section 7): `AI/PmcEncounterSubsystem` (level JSON `pmc_squads`, `CodexTactics.PmcSpawn`), `AI/PmcEncounterRules`; `PmcEncounterSmoke`,
>   `PmcEncounterHuntSmoke`; tests `CodexTactics.AI.PmcEncounter.*`
> - Dev: `CodexTactics.PmcArena` / `PmcArenaSmoke` (`Debug/PmcArenaCommand.cpp`), `PmcTeamSmoke`, `PmcCommanderSmoke`; tests `CodexTactics.AI.PmcCommander.*`,
>   `CodexTactics.AI.PmcOperators.Data`, `CodexTactics.AI.SquadOrderCatalog.*`

## 1. One body

A PMC fighter IS an `AOperativeCharacter` (the operative Blueprint `BP_Operative`, `ABP_Operative`) on a hostile team: every operative skill - formations,
cover, peeks, ladders, drop links, stances, weapons, healing, cold - present and future, is his too. The old melee "Frozen PMC" (`BP_Enemy_FrozenPMC_Fighter`,
a Frostbitten variant body with retargeted zombie clips) is **superseded** (user decision 2026-11-03): its asset test, `PmcFighterSmoke` and its
`OptimizedCharacterSmoke` row were retired; the Blueprint itself stays in `/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/` (it still works as a placed
Frostbitten-type enemy; `LadderEnemySmoke` still spawns it) until the user removes it from his maps.

**The body on the operative Animation Blueprint.** `USkeletalMeshComponent` spawns an anim instance only when every mesh bone (or an ancestor of it)
exists in the ABP's target skeleton (`USkeleton::IsCompatibleMesh`, the runtime gate; the "compatible skeletons" list is an editor filter only). The
Cyberpunk_Explorer_A body has an FBX scene root `SKM_Cyberpunk_Explorer_A` above the Manny `root`, unknown to the operative's UE4 mannequin skeleton
(the user's pack, never saved by us), so `setup_pmc_operator_body.py` makes a copy `SKM_PmcOperator` whose scene root is RENAMED `root` and the real root
`root_inner` (`UOperativeAnimGraphLibrary::RenameMeshBones` on the reference skeleton - `USkeletonModifier` refuses root removals and its commit opens a
Slate dialog in a commandlet), gives it its own skeleton `SK_PmcOperator_Skeleton` (`CreateSkeletonFromMesh` with the Frozen PMC skeleton's retargeting
modes) and lists the operative skeleton as compatible. The operative clips play on it by bone name (UE4 names are a subset of the Manny's; the Manny-only
`spine_04` / `neck_02` / twist bones keep the reference pose - the user's animation polish may retarget later). `PmcTeamSmoke` checks the spawned fighter
wears `SKM_PmcOperator` and runs `ABP_Operative_C`. Spawn: `UPmcSquadSubsystem::SpawnFighter` -> `ACodexTacticsGameMode::SpawnOperative(Entry, ..., TeamId)`
with `Entry.BodyMesh` = `pmc_operators.json "body_mesh"` (`ApplyBodyMeshOverride` before BeginPlay, the ABP stays).

**Looks.** `pmc_operators.json "looks"`: per fighter (seeded, a squad of three wears three different looks) a coat material instance for the slot
`coat_slot` plus the tint through the operative's role-colour path (`RoleColorMaterialSlot` / `RoleColorParameter` set from the look). The pack's jacket
material `M_Jacket_Body_A_4th_Inst` exposes NO vector parameter, so the three `MI_PmcOperator_Grey / _Dark / _Snowed` instances the script made are
identical for now and the tint is a no-op - **follow-up for the user's material work**: a `BaseColorFactor` / snow-layer parameter on the jacket master
(the character snow system `UCharacterSnowSubsystem` drives snow through the body's custom primitive data, which the pack material does not read
either). Visible variety today: the weapon pool (`m16` x3, `shotgun`, `sniper_rifle`; `DA_Weapon_<id>` added to the arsenal and put in hands).
Stats: accuracy 85, luck 25, fortitude 20, 1 medkit, 0 grenades (the autonomous grenade aims at monster clusters - team-aware grenades are a follow-up).

## 2. Teams and factions

`AOperativeCharacter::TeamId`: 0 = the player's squad, 1.. = hostile PMC squads, hostile to every other id. Set before BeginPlay (`SpawnOperative`'s
`InTeamId`). The seams:

| Player's system | Hostile fighter |
|---|---|
| `USquadSubsystem` (selection, HUD panel, formation, orders, minimap friendlies, campaign carry, saves) | never registered: BeginPlay registers only `IsPlayerTeam()`; `CampaignSubsystem` / the save's fallen list / `CombatCheats::IsImmortalActor` check the team |
| Enemy registry / plates / Ctrl + click / minimap enemies | he carries the `Enemy` tag and `RegisterTaggedActor`s himself (the player's men target him like any enemy) |
| Targeting (`FindShootTarget`, `FindBestCombatTarget`, `CanHitEnemy`, the suppress order) | `OperativeCharacterInternal::IsLiveTargetOf(Shooter, Actor)` / `GatherTargetsFor(Shooter)`: the player's men keep the registry; a hostile fighter shoots what HIS team sees (`UPmcSquadSubsystem::GetTargetsFor`) |
| Sight | `EvaluateShotLine` asks `UTacticalSightSubsystem::IsVisibleToSquad` for the player's men and `UPmcSquadSubsystem::IsVisibleToTeam` for a hostile one; `IsVisibleToSquad(an operative)` delegates to the player team's sight |
| Death | `HandleDied`: no HQ radio / remains crate / death cinematic; `NotifyOperativeDied` (the killer's team's kill) and `UWaveSubsystem::NotifyHostileOperativeDied` |
| Fight end | `CheckWaveCompletion` keeps the skirmish going while `HasLiveHostileOperatives` |
| Monsters | `pmc_operators.json "factions"`: `monsters_attack_pmc` (default true: `AEnemyCharacter::FindTarget` takes a PMC fighter in sight within 25 m as an operative candidate; the pack orders never replace such a target), `pmc_self_defence_m` 12 (a fighter shoots only a monster that hunts a mate within 12 m, noticed all round), `cold_applies` true (the cold affects the PMC like the player's men - pending the user's confirmation; the arena level has `cold_rate_mult` 0) |

**Team sight** (`UPmcSquadSubsystem::UpdateSight`, every 0.2 s, the player's team included): a team sees another team's man when one of its living men
sees his profile point by the same rules the squad sight uses (`UTacticalSightSubsystem::CanOperativeSee`: eye height per stance, the stance FOV, the
height range bonus, the 3D trace, the peek view), or within the 2 m touch reveal, or while he is demasked by his own fire; a remembered last-known spot
and the seconds since stay per target (the commander's threat memory). While the sight rules are off (other phases, `Codex.Sight 0`) everything is seen.
Trap found on the way (TRAPS.md): a sight line starting inside the observer's own capsule counted its first hit (himself, a pawn) as clear - the hostile
fighters are now in `HasClearSight`'s ignore list like the squad's men.

**Fog of war** (2026-11-03, section 7): a hostile fighter the player's team does not see (`IsVisibleToTeam(0, ..)`; demask, flares and the cinematic
reveal show him too) is CONCEALED by `UTacticalSightSubsystem::UpdateHostileOperatives`: every scene component of him and his attached actors is
`SetHiddenInGame` (the pose keeps evaluating), the ACTOR stays shown - `IsHidden()` means "absent" to the PMC registry, targeting and the HUD, so it
must stay false. A muzzle flash shows him at once; the fallen are shown. The minimap marks him as an enemy only while seen (then a fading last-known
mark, like a monster), never once fallen. The arena turns it off (`SetRevealHostileOperatives`). A shooter's demask is noticed by the teams within
`GunfireHearingCm` 60 m (it was the whole map: one shot told every PMC squad where the player stood).

**Combat bugs #33 / #43 / #44 (user PIE on Depot 2026-11-07, branch `fix/combat-bugs-43-44-33`; smokes `PmcUnderFireSmoke`, `FireArcSmoke`, `PmcDeathPoseSmoke`):**
- **Under fire (#43, `UnderFireRules`).** The shooter WAS known to the target's team (his flash demasks him within 60 m), but his body stood
  behind the corner: no body line, so no combat target - a walking man kept facing his path and nobody fired back (mates are never in a squad
  shot line: `AddShotLineAllies` ignores every operative). Now every round fired at an operative by another team's operative
  (`AOperativeCharacter::ShootAtTarget`) gives the target and his mates within 25 m the shooter's muzzle flash for 3 s
  (`NoteUnderFire`); the target's team sees the shooter for `DemaskSeconds` whatever the range (`UPmcSquadSubsystem::NotifyShotAt`).
  Without a combat target each of them turns to the flash - standing or walking (the 8-direction locomotion strafes), never in cover /
  sprinting / panicking / against a facing order; with no body line he returns fire AT THE FLASH when a line to it is clear and his posture
  fires on its own (a blind-factor round: x0.2, a hit only while the shooter stays at the spot; `ReturnFireShots`). Symmetric: a PMC fighter
  fired at does the same. No ghost silhouette yet.
- **The manoeuvre under fire (#43 rest, 2026-11-07, `UnderFireRules`, `AOperativeCharacter::UpdateUnderFireManoeuvre` in
  `OperativeCharacterUnderFire.cpp`; data `Content/Data/Combat/under_fire.json`).** Every 0.3 s an operative under fire (a live flash threat),
  not in cover / on his way into it, with no order of his own (no own move order - the player's, the autonomy's, the PMC commander's -, no use
  task / group cover wait, no fire or facing order), not carrying / panicking, in the real-time fight: (1) the nearest cover slot within
  `cover.radius_cm` 8 m whose wall shields him from the flash (`PmcCommanderRules::CoverQualityAgainst` >= 0.35, 16 rays, a short nav path, not
  a mate's slot) -> a sprint into it (`OrderTakeCover`); (2) else, with no clear line from his muzzle to the flash or a **mate in it**
  (`IsMateInLine`, 60 cm), a step **off the line** - 0.5 / 1 / 1.5 / 2 m either side, nearest first, on the navmesh and straight across (inside
  the passage), clear of mates - to the first spot with a clear line (`OrderMoveTo`, 2.5 s cooldown); (3) else prone (`prone.when_no_cover`),
  crouching again when it is over. **Solo vs formation:** a formation follower does nothing while the formation walks (the leader on a player's move order
  / moving; the leader's OWN manoeuvre walk - `IsOnUnderFireManoeuvreMove` - is no formation order: a follower under fire stays put instead of
  walking after him into the fire, `PmcDeathPoseSmoke`-style smokes switch the manoeuvre off) and waits `reaction_seconds` 0.6 s after the first round (the leader's slot logic first); the spot he
  manoeuvred to is HELD (`IsHoldingUnderFireSpot` counts in `USquadSubsystem::IsOnOwnOrder`, so the formation does not pull him back into the
  line) until 3 + `hold_seconds` 4 s after the last round or the leader's next move order. Solo / the leader / a PMC fighter: no grace. Commander
  Mode keeps its own cover rules (skipped). The PMC fighters run the same shared path (an uncommanded fighter acts on it alone; the commander's
  moving orders are own orders and win). Counters `UnderFireCoverMoves` / `UnderFireSidesteps` / `UnderFireProneDrops`, log `[UnderFire]`. Console A/B: `Codex.Combat.UnderFireManoeuvre 0` (off); `Codex.Pmc.Encounters 0` keeps the level-JSON PMC squads away (FallDropSmoke, LevelGenRoute / Ladders, DropSoftlock set both: walk / fall checks, not fights).
  Tests `CodexTactics.Combat.UnderFire.ManoeuvreGate` / `.ManoeuvreChoice` / `.SidestepAndMateInLine` / `.HoldSpotAndRise` /
  `.TuningFileMirrorsDefaults`; smoke `UnderFireManoeuvreSmoke` (L_Gen_Depot_1: the halted column in a 3.6 m passage, the shooter at its far end).
  Not done: a walking column keeps its slots (only the turn and the return fire while the leader walks); the cover search casts its rays
  from him (like `FindCoverNear`), so a slot on the far side of a crate he stands in front of (between him and the flash's back) is not found.
- **Fire arc (#33, `FireArcRules`).** Every aim turn (auto fire, the combat facing, quick fire, SUPPRESS) runs `TurnAimTowards`: eased,
  never slower than 360 deg/s - PRONE only at the prone turn rate (143 deg/s, once per frame: the facing and the fire loop used to step twice);
  prone (no upper-body twist) the shot residual is the larger of the rifle's and the body's error. A prone man with the target behind his back
  now takes over a second before the first round (was 0.15-0.35 s, the body spun round under the shot).
- **Death pose (#44).** Real cause: `UOperativeAnimInstance::UpdateState` ran the cover layer with LAST frame's `bIsDead`. Killed in the
  corner fire stance (the Depot PMC at the corner), `HandleDied` left the cover, the next update saw "left the cover, alive, in the fire pose"
  and played the fire -> idle exit clip - `PlayCoverMontage` fades out every FullBody montage, the death clip too - so the corpse blended
  back to the crouched / standing aim pose (smoke: `anim_M4_cvr_crch_fire_to_idle_L` on the slot, head 112 -> 93 cm). The death is now read
  before the cover layer; `UpdateDeathPose` holds the death clip / held fall against anything else that takes the FullBody slot (a
  `[Death] ... corpse pose was lost` warning, `DeathPoseRestores`). Not a skeleton / slot problem of the PMC body (the Sniper death clips
  play on `SKM_PmcOperator`), and the player's men had the same trap in cover.
- **A dead fighter's body drops the silhouette / highlight overlays (the game designer's screenshot 2026-11-08: a killed PMC fighter lying on
  the ground kept the orange see-through silhouette).** Root cause: a fighter seen by the camera only through a wall / roof carries the
  see-through silhouette (`AOperativeCharacter::SetSilhouetteVisible(true)` puts `SilhouetteMID` - the default role's orange - on every shown
  mesh). Unseen by the squad he is concealed: `UTacticalSightSubsystem::SetOperativeConcealed` hides all his components (`SetHiddenInGame`), and
  a hidden component is not `IsVisible()`. Killed so, `UpdateSilhouette` ("dead -> off") ran `SetSilhouetteVisible(false)`, which cleared only
  the SHOWN meshes and set `bSilhouetteVisible` false; the sight subsystem then showed the fallen again (a kill is seen) with the orange overlay
  still on, and nothing ever took it off (the flag said "off", the corpse never turns it on again). Same leak for any mesh hidden at the
  moment the silhouette goes off (a weapon hidden on a ladder), for the player's men too. Fix: `SetSilhouetteVisible(false)` also takes the
  silhouette off hidden meshes; `AOperativeCharacter::ClearBodyOverlays` (every overlay of him and his attached actors, shown or hidden - the
  silhouette, a hit flash / the target highlight restoring what it saved, a defend / suppress marker) runs in `BecomeCorpse` (killed, the
  knocked-down -> dead path, a load's `RestoreKilledInAction`, PMC and squad alike) and every tick of a corpse (`UpdateSilhouette`), so none
  comes back. Log `X (corpse): overlay M taken off Mesh`. Smoke `PmcDeathPoseSmoke`: the four bodies seen only through a roof slab (the
  silhouette on), the target highlight flashed at the kill, the two fighters killed concealed and shown again 0.5 s later - no overlay on any
  of their meshes 1 s and 4 s after the kill (without the fix the fighters keep `MID_M_Silhouette`).
Not done (follow-ups): no last-known silhouette (ghost) for a concealed fighter, no hearing of them, turn-based combat ignores them, hostile rounds suppress the player's men only through the generic suppression
rules, hostile fighters never loot / get looted.

## 3. The shared order catalog (`SquadOrderCatalog`)

ONE table of the orders a squad member can be given, for the player's controller and the AI commander alike: each entry = `CanDo(context)` + `Execute
(context)` on the operative's own API (the "shared component" is `AOperativeCharacter`). Entries: `Move` (walk / sprint), `TakeCover` (`OrderTakeCover`),
`LeaveCover`, `Stance`, `FirePosture` (the fighter's own override), `PriorityTarget`, `Suppress` / `CeaseSuppress`, `Heal` (`HealAlly` within reach),
`Reload`, `SwitchWeapon` (by id), `Hold` (stop). The common gate `IsOperativeOrderable`: alive, not KIA / knocked down / raging / panicking. The
commander goes only through it; the player's controller still calls the operative directly (routing the player's calls through the catalog where cheap is
a follow-up - the catalog entries ARE those calls, so nothing diverges).

**The 4-step rule for a new skill:** (1) implement it on `AOperativeCharacter` (its cluster file) - both sides have it at once; (2) a catalog entry
(`ESquadOrder` + `SquadOrderCatalogLocal::Build`) with its `CanDo`; (3) an `EPmcOrder` + a `PmcCommanderRules::ScoreOrder` case (0..1 considerations x a
weight, the default in `FPmcWeights` and `pmc_commander.json`) + its `ExecuteOrder` branch; (4) a smoke stage and an arena metric (`FPmcTeamStats`, the
JSON `stats`).

## 4. The commander (`UPmcSquadSubsystem` + `PmcCommanderRules`)

Every `decision_interval` (0.5 s) per commanded team: the squad view (alive, enemies known / seen now, contact within `contact_memory_seconds`, mates
suppressing / flanking / advancing / healing - never counting the fighter himself) and per fighter his view (health, clip / ammo fractions, cover and its
quality against the nearest known threat, a better slot nearby, exposed and for how long at this spot, stance, moving, medkit, sidearm, weapon range, the
nearest threat with its distance / seen-now / seconds since, the nearest wounded mate). Every order's score = weight x the product of its 0..1
considerations (`ScoreOrder`); `PickWithMargin` keeps a running order within `switch_margin` (0.1) of the best, a committed WALK or any order younger than
`commit_seconds` (2 s) within `move_switch_margin` (0.45) - the fix for the flank / suppress flapping seen in the first run.

| Order | When it scores (short) | Executes |
|---|---|---|
| Hold | the baseline: in cover and in contact worth most | stop (the combat facing / own fire do the rest) |
| TakeCover | a known threat, a better slot nearby, badly covered, exposed | `OrderTakeCover` on the best of 16 ray hits round him scored by `CoverQualityAgainst` (the wall between him and the threat) |
| Advance | bounding overwatch: a threat beyond 60 % of the range, numbers, health, ammo, no mate advancing | walk to `AdvancePoint` (sprint while unseen) |
| Flank | a seen threat 6-35 m, a mate pinning it or 2+ alive, no mate flanking | walk to `FlankPoint` (90 deg off the threat -> squad line, the far side, 10 m) |
| Suppress | a seen threat in range, ammo >= 40 %, a mate moving, no mate suppressing | `OrderSuppress` |
| FallBack | in contact below 35 % health or outnumbered | sprint 8 m away from the threat |
| Heal | a medkit, a mate under 45 %, nobody in sight (x0.3 under eyes) | walk up, `HealAlly` |
| Relocate | exposed at one spot >= 6 s with a slot elsewhere | another cover slot >= 3 m away |
| Ambush | no contact yet in the fight | crouch, a cover slot within 6 m |
| Reload | clip < 30 % with reserve, in cover / out of sight | `StartReload` |
| Sidearm | an empty clip, a seen threat under 4 m, a pistol | `SwitchToWeaponById("pistol")` |
| Crouch | in contact, standing still in the open | crouch |

**High ground** (2026-11-03, the Depot arena): `FindCoverNear` (the one search behind TakeCover, Relocate and Ambush) scores every slot x
`HighGroundFactor(level)` = 1 + `high_ground` (0.2) x min(level, `high_ground_max_levels` 3), the level = `SightRules::HeightLevel` (3 m steps) of the
slot over the ground under the fighter (the shared sight's ground measurement). Besides the 16 rays round him it probes navmesh spots 1-3 floors up at
8 bearings (0.6 x the search radius away) and tries walls there facing the threat; such an elevated slot is only taken with a full navmesh path
(ladders / ramps / drop links; length <= 3 x the straight distance + 30 m, at most two path queries per decision). Tested in
`CodexTactics.AI.PmcCommander.HighGround`; the trainer's bounds sidecar has `/high_ground` 0.1-0.3 (`high_ground_max_levels` frozen).

**Engagement bands** (user request 2026-11-04: "a man with an assault rifle keeps as far from his target as his weapon allows, fires from corners,
prone, never fights at point-blank range; with a pistol he must come closer but still not point-blank; the shotgun is the close-range weapon").
`pmc_commander.json` `engagement_bands` per weapon id (NUMBERS PENDING the user): m16 / plasma_carbine 25-60 m, sniper_rifle 45-110 m, pistol
10-25 m, shotgun 4-14 m (`close_quarters`), lmg 30-70 m (no such weapon yet); an unknown weapon 0.5-0.95 x its reach. `PmcCommanderRules::ResolveBand`
caps a band at `band_reach_frac` (0.95) x the weapon's reach and keeps a fifth of it below that (he must still hit from there): with today's
reaches m16 (25 m) -> **19-23.75 m**, sniper (30 m) -> 22.8-28.5 m, pistol (10 m) -> 7.6-9.5 m, shotgun (7 m) -> 4-6.65 m. `BandFactor` (1 inside,
quadratic down to `band_close_floor` 0.1 at point-blank, 0.5 at 2 x max) weighs: Hold / Suppress / Crouch x lerp(1, factor, `band_position` 0.7);
TakeCover x the best slot's factor (`FindCoverNear` scores every slot by its distance to the threat in the band and a corner slot - a free edge to
fire round - x (1 + `corner_bonus` 0.2)); **too close** (`IsTooClose`: nearer than `standoff_trigger_frac` 0.8 x the band's min, never a
close-quarters weapon) -> **FallBack** scores `standoff` 1.3 x (0.6 + 0.4 x severity) and runs as a standoff: a cover slot round him already at
band distance (sprint), else `StandoffPoint` (straight away to the band's middle); a running retreat keeps scoring until the band's middle
(`FPmcFighterView::RunningOrder` FallBack, hysteresis); a running Flank never triggers it (the flank walk round the threat passes inside
the band on purpose - PmcCommanderSmoke's flanker was pulled back at 11 m before), and a running Flank keeps scoring while the threat is only
remembered (the band's longer flank walk loses sight of it on the way; it used to be dropped by a Hold). Holding in cover against a known
threat, the commander makes it his priority target (catalog `PriorityTarget`): the cover pose faces it and the low-wall / corner fire stance
leans out for it; holding in the open with a threat not seen now, the catalog's `Hold` now does what its doc said - `StopAndFace` the
threat's last known spot (public since this change) - so a man back at his band after the walk turns round and fires (the smoke's riflemen
stood at 21 m with their backs to the squad before). **Advance** only past the band's max (a rifleman) and stops at `AdvanceStopCm` = clamp(0.6 x reach,
band) (19 m for the m16, 4.2 m for the shotgun); a close-quarters weapon advances x `close_in` 1.6 with the numbers factor floored at 0.5, and
only for a step of >= 2.5 m (the first Depot arena run showed a shotgunner 4.4 m off re-issuing a 20 cm advance every decision).
**Flank** points lie at `FlankDistanceFor` = the flank distance clamped into the band (19 m for a rifleman, 6.65 m for a shotgunner). **Prone:**
the Crouch order (still, in the open, in contact) lies down for a weapon whose band starts >= `prone_min_band_cm` 15 m when the threat is in the
band; any walk first gets him up to crouch. `use_engagement_bands` false = the old behaviour (A/B; a bool, not a trainer gene). New genes in
`pmc_commander.bounds.json` (+-50 %): standoff, standoff_trigger_frac, band_position, band_close_floor, corner_bonus, close_in, prone_min_band_cm;
the band rows and the reach / default fractions are frozen (designer numbers). Arena JSON: `standoffs` per team. Tested in
`CodexTactics.AI.PmcCommander.EngagementBand`; smoke **`PmcStandoffSmoke`** (L_MovementTest, an 85 cm wall 19.6 m ahead and 4.5 m aside): three
riflemen spawned 8 m from the squad FALL BACK (3 standoffs), within 4 s all stand 21.1-21.2 m off and fire from there (prone / suppressing);
three shotgunners 15 m out close in to 7.9 m (an advance + flanks) with no standoff. `PmcCommanderSmoke` uses a wider clear arena (+-26 m) for
the band-distance flank points. Known limits: the cover search casts rays from the fighter, so a slot on the far side of a wall that stands
between him and the band point is found only once he is past it (the standoff walks to the band point first; the operative's own cover walk
round a wall from the threat's side stopped short in the smoke). The over-the-top / corner peek view reached only 18 m; since #32
(2026-11-07) it reaches his sight range (`GetPeekSightRangeCm`, docs/systems/sight-and-vision-cones.md), so a rifleman at his 19-24 m band sees
round / over his cover, and the corner bonus (`corner_bonus`) counts only when the threat lies within that peek range (`PeekSightRules::IsCornerInPeekReach`).

**Round objective** (task #53, the arena only - inactive everywhere else, so every factor below is 1 and the order scores 0): the
team gets a role + the point (`UPmcSquadSubsystem::SetTeamObjective` / `UpdateTeamObjective`: the round's time fraction, the capture
clock, an enemy on the point) and every fighter view `FPmcFighterView::Objective` (`FPmcObjectiveView`: role, point, his distance,
radius). New order **Objective** (a committed walk to the point, each man to his own spot of a 0.4 x radius ring, sprint while unseen):
an attacker outside 0.7 x radius scores `objective` x (1 - `objective_urgency` + `objective_urgency` x time fraction) x health x
(0.6 while a threat is seen now) x (0.8 with defenders on the point); a defender scores `objective` x max(retake = 0.2 + 0.8 x
the attackers' clock while attackers stand on it, return = 0.7 when he is beyond the point's overwatch) x health. The **positions**
consideration `PmcCommanderRules::ObjectivePositionFactor` multiplies every cover slot `FindCoverNear` scores (TakeCover / Relocate /
Ambush / the standoff's slot): an attacker x max(0.25, 1 + `objective_approach` x gain), gain = (his distance to the point - the
slot's) / radius in -1..1 (1 on the point) - cover on the way to / round the point; a defender x (1 + `overwatch` x reach), reach 1
within `OverwatchRangeCm` (his band's max, >= 2 x radius) of the point, 0 at twice that - overwatch of the approaches. Hold (and Ambush)
x (1 + `objective_approach`) for an attacker on the point, x (1 + `overwatch`) for a defender within the overwatch. Genes in
`pmc_commander.json` (`objective` 1.0, `objective_urgency` 0.65, `objective_approach` 0.5, `overwatch` 0.5) + `.bounds.json` +-50 %.
Tested in `CodexTactics.AI.PmcCommander.Objective`. A team's assault point standing in for a threat (no enemy known) is flagged
`FPmcThreatView::bStandIn`: no "too close" standoff from it and no band penalty near it (the first objective smoke showed attackers
backing off the point they walked to).

**Target attention** (user 2026-11-07: "PMC seek the moment the operatives look away and attack through the blind zones"): only the
enemies the team SEES now count, with their facing as seen (`SeenWatchers`: position + body yaw). `PmcCommanderRules::AttentionOff`
(the `AmbushWatchRules` bearing / angle maths, a +-45 deg look sector) = how far a point lies outside every watcher's sector, 0..1,
the most attentive watcher decides; `FPmcThreatView::AttentionOff` is the fighter's own (0.5 = nobody seen). Moves: Flank, Advance (not a
shotgunner's close-in: PmcStandoffSmoke's shotgunners stalled at 8.9 m with it) and an attacker's Objective walk x (1 + `attention` x (off - 0.5)); a flank takes the mirrored side's point when that one is >= 0.25 less
watched. Fire discipline (`ShouldHoldFire`): an objective DEFENDER in ambush holds his fire (the catalog's FirePosture Passive, back to
Aggressive when he strikes; `[PmcAI] ... holds his fire / opens fire`; `SetTeamObjective` puts the defenders Passive at once - the first
Depot run showed their own auto-fire springing the ambush 0.2 s in, before any decision) while lying in wait with nobody seen now, or
while the seen threat still looks his way (off < `ambush_patience`), unless the target is deep in the kill zone (`kill_zone_frac` x his band's max / reach, or on the point), a shot was
already exchanged between the teams (`HandleWeaponFired`), he is seen himself, or `ambush_max_hold_s` passed since the first contact.
Genes `attention` 0.6, `ambush_patience` 0.5, `kill_zone_frac` 0.6, `ambush_max_hold_s` 20 (bounds +-50 %). Tested in
`CodexTactics.AI.PmcCommander.Attention`.

Statistics per team (`FPmcTeamStats`): kills, flanks attempted / succeeded (the point reached), suppressions + seconds, ambushes sprung, retreats, heals,
relocations, advances, covers, friendly-fire hits (`OnWeaponFiredNative` on a mate), stuck events (a walk with < 1 m of progress for 4 s is dropped),
orders issued. `OnFighterOrder` broadcasts every order taken (the arena's track).

`PmcCommanderSmoke` (L_MovementTest, own arena): three fighters facing away hold in AMBUSH crouched; turned round -> contact, one FLANKS (the point >= 60
deg off the frontal line through a mate) while a mate suppresses / holds the front, the flanker reaches his point, every order through the catalog, no
friendly fire. `PmcTeamSmoke`: the hostile fighter is no squad member, carries the tag, is seen / targeted / hurt by the squad and sees / targets / hurts it,
a 3 m wall hides both sides from each other, a hound hunts him and he takes it as a self-defence target, the body + ABP checks.

## 5. The self-play arena (`CodexTactics.PmcArena`)

```
& "C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<repo>/CodexTactics.uproject" /Game/Maps/L_FormationLabCombat `
  -game -nullrhi -nosound -nosplash -unattended -NoTelemetry "-LevelJson=L_PmcArena.json" -UseFixedTimeStep -FPS=60 -FixedSeed `
  "-ExecCmds=CodexTactics.PmcArena rounds=3 seed=7 weightsA=pmc_commander.json weightsB=pmc_commander.json opponent=pmc time=120" "-abslog=<repo>/Saved/Logs/PmcArena.log"
```

Args (keys case-insensitive; the night trainer's template `Map=.. Rounds=.. Seed=.. WeightsA=.. Opponent=Bot|Pmc WeightsB=..|None TimeLimit=.. Out=..`
of `docs/tools/night-trainer.md` 6 fits as it is): `rounds=N seed=S weightsA=<file> weightsB=<file>|None` (absolute, project-relative, or a name in
`Content/Data/AI`; missing = defaults) `opponent=pmc|player|bot` `time|timelimit=<s>` (120) `out=<project-relative result path>` (the tracks go to
`<out dir>/<out basename>/round_<n>.track.json`; without it `Saved/Arena/<run>.json` + `Saved/Arena/<run>/`) `map=<informational>` `squad=<men per side>`
(3) `distance=<cm>` (3000) `shot=0|1` `record=0|1` `spawns=random|fixed` `spawnmin=<m>` (35) `spawnmax=<m>` (80). Round n's seed = `seed + n - 1`; the attacker is the round seed's parity (even = A), so `Rounds=1
Seed=<round seed>` replays a round with its attacker. Spawns: `fixed` (the default on L_FormationLabCombat) = side A (team 1) at the player start, side B 30 m away at the first bearing with a navmesh path;
`random` (the default on any other map, or `spawns=random`) = per round from the round seed up to 48 navmesh spots anywhere in the navmesh bounds
on a random level (ground, +3 / +6 / +9 m: roofs, walkways, the cut; level = `SightRules::HeightLevel` over the ground under the player start),
then `PmcCommanderRules::PickSpawnPair`: pairs 35-80 m apart (planar), keyed by a seeded draw + a variety bias (+0.35 different levels, +0.15 one
above the ground), the first with a navmesh path BOTH ways wins (24 tries); the seed also decides which spot is A. Each squad then stands round
its spot on free navmesh (`SmokeUtils::FreeSpot`), facing the other. Same seed + map = the same pair; the log line `[PmcArena] spawns seed N: A
<xyz> level L, B <xyz> level L, D m apart`. Tested in `CodexTactics.AI.PmcCommander.SpawnPair`. **Depot arena** (the night trainer's default since
2026-11-03): map `/Game/Maps/Generated/L_Gen_Depot_1` + `-LevelJson=L_PmcArenaDepot.json` (L_PmcArena.json + the Depot's floors 300 / 600 / 900 +
drop links on), `time_limit_s` 240, `Spawns=Random` in the trainer's `arg_template`. Levels in the walks: a walk goal at the fighter's height that hangs over a drop, snaps > 2 m away or has no full path (a roof squad's
advance point over the street) is retried on the threat's level; an Advance whose planar point is his own spot while the threat is on another
level (> 2.5 m) goes for the threat's spot (up / down via stairs / ladders / drop links). Both only when the levels differ, the flat lab is
unchanged. **Cold is off in the arena** (fair, deterministic training): `cold_rate_mult` 0 multiplies the whole cold rate (the weather's too),
so no frostbite / drain - but ONLY when the level JSON really loads: in PowerShell QUOTE the argument (`"-LevelJson=L_PmcArenaDepot.json"`;
unquoted, PowerShell 5.1 splits it at the dot, the game reads `L_PmcArenaDepot`, falls back to DA_Level_level_01_outpost with cold, and the
Depot's Blizzard froze the roof squads prone - the "HP drain" of the first runs). The night trainer passes an argument list (no split).
Depot run (seed 7, 3 rounds, 240 s, quoted): contact in every round, spawn levels 0 / 3, 3 / 0, 3 / 0; round 1 B down to 1 survivor, round 2
1 kill each side (a ladder climbed), round 3 B down to 1; all three time-limit draws (no full elimination), no frostbite.
The game quits after the last round. `opponent=pmc`: team 2 with weights B, the player's squad parked (any number of rounds); `player`: the player's squad
commanded with weights B (one round: the fallen cannot be revived); `bot`: the player's squad driven by `UPlaytestBotSubsystem` (one round, development
builds; the bot does not yet see hostile operatives as threats - the squad's own Aggressive fire does the fighting). The attacker alternates per round
(by the round seed's parity): the defender waits in ambush, the attacker advances on the defender's spawn (`SetTeamAssault`). The lab's cheats are forced off, the fight
starts on the first contact (`combat_start ambush`, a skirmish), a round ends when a side is down or at the time limit (a draw).

**Objective rounds and scenarios** (task #53, 2026-11-07; the 2026-11-05 night learnt "don't fight" - every round a time-limit draw):
`Objective=1` (default) places a point per round - skirmish / fixed spawns: `objective_offset_frac` (0.2, x 0.75-1.25 seeded) of
the way from the defender's spawn to the attacker's, on the navmesh at the defender's level with a path from both spawns (half that,
then the spawn itself). The attacker (round seed parity, even = A) gets the point as his assault point and the `Attacker` role, the
other side `Defender` (`SetTeamObjective`); every step the arena counts live men within `objective_radius_m` (planar, +-3 m height) and
steps `FPmcObjectiveClock`: attackers on it and no defender = holding; `objective_hold_s` summed over stints = captured (user 2026-11-07: cumulative with a reset - contested / empty only pauses, the defenders retaking it alone resets; was: unbroken) (a defender back on it or
the attackers leaving resets it); per role the seconds of sole control. `PmcCommanderRules::ResolveRound`: a side down = elimination;
captured = `objective` (the attacker wins); the time limit = the defender wins with the point not held by the attackers at that moment
(`held`), else a `contested` draw; without an objective the old time-limit draw. Data: the level JSON's `pmc_arena` object (both
L_PmcArena.json and L_PmcArenaDepot.json: radius 6 m, hold 15 s, offset 0.2, defender_offset_m 8, passage 1-3 m, walls >= 3 m).
`Scenario=random|skirmish|chokepoint|street|roofs|blizzard` (random = `PickScenario(round seed)`, uniform; needs random spawns):
chokepoint = a ground spot with two tall walls 1-3 m apart and the passage free along its axis (`IsPassage` on 8 chest rays + rays
just under 3 m; up to 12 sites from 400 seeded samples), the attackers 35-80 m off along the axis, the defenders 8 m behind the point;
street = a ground spot free 10 m round in 7 of 8 directions; roofs = a navmesh spot >= 1 height level up; blizzard = a skirmish with
`ForceWeather(Blizzard, instant)` (sight x 0.5; the next round restores the level's weather; cold stays off by `cold_rate_mult` 0). A
site-less scenario falls back to skirmish (logged; the result names the real one + `scenario_requested`). Per round the result adds
`scenario, scenario_requested, weather, objective {enabled, result captured|held|contested|eliminated|none, x, y, z, radius_m,
hold_s, attacker_clock_s}`, per side `role, damage_dealt, damage_from_concealment` (each fighter's HP drop between samples goes to his
last hit's side within 3 s; concealed = the target's team did not see the shooter at the shot, before the shot's own demask lands),
`first_strike` (the round's first hit), `objective_hold_s` (sole control), `median_engagement_m` (its shots at the other side),
`shots`, `strikes_unobserved` (first hits on a target whose facing was > 45 deg off the bearing to the shooter), `stats.<X>.objective_moves`;
the track gets `scenario`, `objective` and shot fields `concealed / dist_m / unobserved`, an `objective` captured event. Smoke
**`PmcArenaObjectiveSmoke`** (combat lab, L_PmcArena.json): seed made even (A attacks), the defenders parked out of the fight (alive,
not commanded) - the attackers' Objective orders walk them onto the point and they capture it in ~18 s (end_reason objective, A wins,
objective_hold_s 15.0); `PmcArenaSmoke` checks the roles + stats on a real round. Tests `CodexTactics.AI.PmcArena.{ObjectiveClock,
RoundOutcome, Scenario}`.

**Result JSON** (`Out=`, else `Saved/Arena/<run>.json`, `<run>` = `YYYYMMDD-HHMMSS[-record]`), schema `codex.pmc_arena/1` = the trainer's contract
plus extras: `map, opponent (Pmc|Player|Bot), seed, rounds_requested, rounds_limited, time_limit_s, squad_size, run, record, weights_a, weights_b
(None for Bot), highlight_formula, started, finished (+ _utc), weights {A, B: file, name, loaded}, rounds [ {index, seed (the round's own), attacker,
winner A|B|draw, duration_s, end_reason elimination|time_limit, fight_started, lead_swings, highlight_score, track (relative to the result file), sides
{A, B: team pmc|player|bot, survivors, squad_size, hp_lost, hp_max, kills, flanks_attempted, flanks_succeeded, suppression_s, ambushes, retreats,
retreats_survived (fell back at least once and alive at the end), heals, friendly_fire, stuck_events}, survivors / hp_lost / kills {A, B} and stats {A, B}
(the arena's own richer counters: suppressions, relocations, advances, covers, orders)} ], summary {rounds, A_wins, B_wins, draws, avg_duration_s}`
and one log line `[PmcArena] rounds=N A_wins=.. B_wins=.. draws=.. avg_duration=..s opponent=.. -> <path>`. Checked against
`Scripts/Trainer/fixtures/sample_arena_result.json` (main): every contract key present; the extras are additive.

**Track** per round (`<result dir>/<result basename>/round_<n>.track.json`), schema `codex.pmc_arena_track/1` = the trainer's viewer contract:
`dt` 0.2, `units` cm, `fighters [ {id, side, team} ]`, `frames [ per dt: one [x, y] or null (down) per fighter in `fighters` order ]`, `events`
(`shot` from / side / to / hit, `kill` by / target / side, `heal` side, `tactic` who / side / team / decision (the commander's chosen order) / score (its
top score) - flanks, suppressions, ambushes, retreats, heals among the decisions); the arena's extra `states` = the same grid of `[yaw, stance, hp, state]`
(`state` = the order, "/cover" while at a wall, "player"). **highlight_score** (the juiciest rounds): kills x1 + a decided round +1 + the winner's last
man standing +2 + lead swings x1.5 (the sign of alive A - alive B changing) + successful flanks x1 + ambushes sprung x0.5 + heals x0.5 + duration sanity
(20-150 s +1, else -1); the formula is in the result's `highlight_formula`.

**Replay / capture.** The same weights + seed + `-UseFixedTimeStep -FPS=60 -FixedSeed` reproduce the spawns, looks, weapons and the commander's
decisions; run it RENDERED with `record=1` (the overview camera follows the fighters' centre every tick, HUD off) and the clip recorder in front:
```
UnrealEditor.exe <uproject> /Game/Maps/L_FormationLabCombat -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound -unattended "-LevelJson=L_PmcArena.json" `
  -UseFixedTimeStep -FPS=60 -FixedSeed "-ExecCmds=CodexTactics.ClipRecorder pmcarena nodirector,CodexTactics.PmcArena seed=7 rounds=1 record=1"
```
-> `Saved/Screenshots/Clips/pmcarena/f_NNNNN.png`, then `python Scripts/Tools/clip_assemble.py` (the LevelGenRun mp4 pipeline,
`Scripts/levelgen_record_run.ps1`) cuts the mp4. `shot=1` instead saves `Saved/Screenshots/PmcArena/round1_{contact,midfight,late}.png` at 8 / 15 / 26 s
of round 1. Known determinism limits: the Detour crowd avoidance, async navmesh work and physics hit results can drift a long round - compare the
replay's winner / duration with the original track before cutting a clip.

`PmcArenaSmoke` (registered, map `L_FormationLabCombat` + `L_PmcArena.json`): one pmc round, the squads meet, somebody loses health, the JSON (round
seed, end_reason, sides.A/B) and the track (frames x fighters, events, dt) validated. First real runs (seed 1 / 7): A wins 1-0 with one survivor, 5 kills
in 19-40 s, both sides flank and suppress, highlight 8.5-10.5; a contract-style run (`Map=.. Rounds=2 Seed=11 .. Out=Saved/Arena/contract_test.json`):
round 1 (seed 11, B attacks) A by elimination in 11.7 s, round 2 (seed 12, A attacks) a 60 s time-limit draw with 14 retreats - the attacking side
stalls at a defender in cover (the commander's advance stops at 60 % of the range; tuning / the trainer's job).

## 6. Follow-ups (not in the MVP)

- Learning Agents imitation / reinforcement learning on the arena (the trainer: weights files in, `Saved/Arena/*.json` out; later the player's own
  tracks as demonstrations).
- Ghost silhouettes of concealed hostile fighters, hearing, turn-based combat with PMC fighters, the bot and `USquadAutonomySubsystem` perceiving
  hostile operatives, team-aware grenades / panic, PMC corpse loot, the HUD plate on a PMC fighter, suppression of the player's men by PMC rounds
  (checked only generically). (Done 2026-11-03, section 7: fog-of-war hiding, minimap marks, level JSON `pmc_squads`, `CodexTactics.PmcSpawn`.)
- The jacket material's tint / snow parameters (the user's material work) and a proper IK retarget of the Manny-only bones.
- Routing the player's controller orders through the catalog (stance / posture / cover / suppress) and an `ISquadCommandSource` facade.

## 7. Playing against PMC (level encounters, 2026-11-03)

User request: "I want to play against the PMC squads myself tomorrow on the Depot". `UPmcEncounterSubsystem` (`AI/PmcEncounterSubsystem`) +
`PmcEncounterRules` (pure) + the postures in `UPmcSquadSubsystem`.

**Level JSON** (`Content/Data/LevelJson/<level>.json`, any level; a bad entry is skipped with a warning, never the level):
```json
"pmc_squads": [
  { "count": 3, "weights": "pmc_commander.json", "spawn": "random", "min_m": 40, "max_m": 90, "delay_s": 30, "posture": "defend" },
  { "count": 3, "weights": "pmc_commander.json", "spawn": "random", "min_m": 50, "max_m": 90, "delay_s": 120, "posture": "hunt" }
]
```
Defaults: count 3 (1-6), weights `pmc_commander.json` (a file in `Content/Data/AI`, or a path), spawn `random` (or `[x, y(, z)]` cm), min_m 40,
max_m 90 (from the player's squad centre), delay_s 20 (game seconds after the level start; paused in turn-based / game over), posture hunt, seed
0 (= 1000 + 101 x the entry index). `L_Gen_Depot_1.json` has the two entries above. **Spawn spot** (`SpawnEncounter`): up to 48 seeded navmesh
spots in the min..max ring round the squad on a random level (ground, +3 / +6 / +9 m: roofs, walkways), then `PickEncounterSpot` (seeded keys, +0.15
for a raised spot): the first one OUT OF THE SQUAD'S VIEW - no low line (`UCombatPressureSubsystem::IsVisibleToSquad`) and no eye line from any
man's eyes to a standing (165 cm) or crouching (110 cm) head there, not within touch reveal - with a full navmesh path both ways to the leader. The
fighters stand in a line across the squad's direction (a rank behind / a ring round the spot when a place is off the navmesh, on another level or
seen), facing the squad; a new hostile team id each; concealed at once. No spot -> retried 10 s later, 3 times. Log: `[PmcEncounter] team N: ...`.

**Postures** (`UPmcSquadSubsystem::SetTeamPosture`; until the team is ENGAGED = it has seen an enemy - sight, touch, a shot heard within 60 m - or the
player's team has seen one of its men; then the utility commander runs the fight, `[PmcEncounter] team N engaged`):
- `defend`: commanded from the start -> the commander's Ambush (crouched, a cover slot within 6 m of the spot).
- `hunt`: commanded with an assault point per leg (`HuntLegSeconds` 30 s): leg 0 the player start, then "radio intel" - the player's squad centre
  off by a seeded 7.5-15 m (`PmcEncounterRules::HuntSearchPoint`; a known last position wins); the commander advances on it by bounding overwatch.
- `patrol`: NOT commanded; walks (no sprint) between seeded points 6-15 m round the spot (`PatrolPoint`), a 4 s look round at each; on contact the
  walk stops and the commander takes over.

**Fight start / end.** A PMC fighter or the player's man opening fire is the ambush contact (`NotifyHostileContactIn`; combat_start `ambush` levels
like the Depot): an ambush skirmish. `HasLiveHostileOperatives` counts only ENGAGED teams' living fighters, so a squad still waiting unseen (the 120 s
hunters) never holds a finished fight open; the last engaged fighter down -> `[Wave] skirmish over: back to exploration`. The player's men fire at
hostile fighters in every fire posture but Passive (an operative counts as "has detected the squad"). The Depot has the lab cheats on (immortal
squad, infinite ammo; `Codex.Cheat.Immortal 0` / `Codex.Cheat.InfiniteAmmo 0` for a real fight).

**Testing in PIE** (editor, normal play): open `/Game/Maps/Generated/L_Gen_Depot_1`, Play (the level JSON loads by the map name) - the defend
squad comes 30 s in, the hunters 120 s in, somewhere 40-90 m away out of sight (the Output Log shows `[PmcEncounter]` lines). Any time, any map:
console (`~`) `CodexTactics.PmcSpawn [count=3] [weights=pmc_commander.json] [posture=hunt|defend|patrol] [min=40] [max=90] [seed=N]` (positional
in that order too: `CodexTactics.PmcSpawn 3 pmc_commander.json defend 30 60`); it never quits the session.

**Checks.** Tests `CodexTactics.AI.PmcEncounter.Parse / .Spot / .Search`. `PmcEncounterSmoke` (Depot + its JSON): two squads scheduled 30 / 120 s, the
first (defend) spawns 40-90 m away concealed, unseen and not in contact for 3 s, the squad put in sight -> revealed, the ambush skirmish, both sides
fire, the squad down, back to exploration, the fallen shown. `PmcEncounterHuntSmoke`: `PmcSpawn` hunters close in / make contact by themselves, then
the same fight. First runs: the defend squad down in 3-7 s against the immortal squad at 10 m; the hunters made contact 4 s in, down in 27 s.
The first spot check (low line only) let the fighters see the squad over a low wall at spawn - hence the eye-line test.

**Not done:** no ghost silhouette for a concealed fighter; no hearing of PMC steps; `USquadAutonomySubsystem` and the playtest bot still do not
perceive hostile operatives as threats (the squad's own fire does the fighting); turn-based combat ignores them; no HUD plate / health bar over a
PMC fighter; PMC corpses are not lootable; team-aware grenades; the radio-intel hunt is a deliberate cheat (no hearing model yet).

## 8. No target without honest sight, no shot without a clear line of fire (fix/pmc-fire-through-wall, 2026-11-08)

**Report (game designer, PIE on `L_Gen_Depot_1`, zone `Z_ST_south`):** a hostile PMC fighter lying PRONE in the open kept firing at the squad
standing behind a solid wall - the tracers went straight into the wall. He never hit anybody, but he kept firing.

**(a) How he acquired them - reproduced by `PmcFireLineSmoke` part A (`Codex.Combat.HonestFire 0`):** the prone eye (25 cm) and the 3D sight
trace (`CanOperativeSee`) were right, and so was the peek range (#32, it only stretches the peek cone's range). The leak was in the team sight
(`UPmcSquadSubsystem::UpdateSight`, `Observe`): a demasked shooter (`UTacticalSightSubsystem::IsDemasked` - every shot of the squad, 2 s) within
`GunfireHearingCm` 60 m of any fighter of the team, and a shooter flagged by `NotifyShotAt` (bug #43, the flash), counted as **seen** - no line
needed. A seen target is a target of the team: the shot line, the commander's `Suppress` order. The smoke: one demask behind the wall -> the
leader is `IsVisibleToTeam` true.

**(b) Why he fired with no line of fire - reproduced by part A:** the aimed fire was always gated (`EvaluateShotLine` traces from his stance's
muzzle - prone 25 cm - to the target and refuses a blocked line), but the **suppress order** (`RunSuppressFire`, the commander's `Suppress` at
the threat's spot) fired at a point with **no line check at all**, and it moved its point to the target's real position whenever the target
actor was not hidden - the player's men never are, so it followed them behind the wall. The smoke: a suppress order at the leader behind the
wall -> 20 of 20 rounds into the wall.

**The rules now (one model: the squad and the PMC operatives share the code; the armed enemies' own fire already traced its line):**
- **Honest sight** (`SightRules::ResolveContact`, `Observe`): a heard shot / a flash is **seen** only when an eye of the team has a clear line to
  the shooter's profile point, to the flash of his shot at one of them (`NotifyShotAt` now records it; the line ends a hand short of it, like
  the return fire's) or to his muzzle now; otherwise he is **known** (`Known` with his spot and the contact time: the commander's Advance /
  Hold-facing / the hunt use it) but `bSeenNow` stays false - no aimed shot, no suppress order. The eye rules (FOV, range, height, conditions)
  and touch are unchanged.
- **A line of fire for fire at a point** (`SuppressionRules::IsFireLineUseful`, `AOperativeCharacter::HasUsefulFireLine`): the line from his
  muzzle at his stance now (prone 25 cm, crouched 85, standing 140; in cover the cover's fire origin) to the point, bodies ignored, must be
  clear up to within `order.line_near_point_cm` 250 cm of the point (the cover the target hides behind may stop the rounds: that is the
  suppression) and for at least `order.line_min_clear_fraction` 0.5 of its length (`Content/Data/Combat/suppression.json`). Else no round:
  a suppress order ends (`ESuppressOrderStop::NoLineOfFire`, "NO LINE OF FIRE" over the player's men), a quick-fire burst at a spot ends.
- **The commander** builds `FPmcFighterView::bFireLine` (the same check to the threat's spot) and scores `Suppress` 0 without it - he picks a
  move (Advance / Flank / Relocate) or holds instead (`CodexTactics.AI.PmcCommander.NoSuppressWithoutFireLine`).
- **Last known spot:** a running suppress order / quick fire follows the target only while its side sees him (`IsSeenByOwnSide`); then it keeps
  his last seen spot (fire there is allowed while a line reaches it).
- A/B: `Codex.Combat.HonestFire 0` = the old rules. Unchanged on purpose: the player's view of a demasked hostile fighter (his concealment is
  lifted while he fires - the player's information), the monsters' perception of a demasked operative (hearing; they do not shoot).

**Checks:** unit `CodexTactics.Combat.FireLine.Useful`, `.HonestContact`, `CodexTactics.AI.PmcCommander.NoSuppressWithoutFireLine`; smoke
**`PmcFireLineSmoke`** (L_MovementTest arena: a 3 m x 8 m wall 4 m in front of the squad, an M16 fighter prone 20 m beyond it, the squad's
shots heard every 0.5 s): A (old rules) the leader seen by a heard shot, the suppress order puts 20 / 20 rounds into the wall; B (honest) the
leader known not seen, the same order drops at once (blocked 19.8 m of 24 m), 0 rounds, his commander 10 s against the heard squad - 0 rounds
into the wall (he held, prone); C the leader steps out past the wall's end, seen, suppressed, steps back - 0 rounds into the wall in the next
6 s, he is known not seen. `PmcUnderFireSmoke` (bug #43) keeps its fighter standing while he fires over the 1.2 m wall (the squad's return
fire put him down behind it, where his flash is hidden - with honest sight then only known) and checks that the squad's team saw him by his
flash during the fire.
