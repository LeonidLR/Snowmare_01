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

Not done (follow-ups): hostile fighters are never hidden / ghosted on the screen the way monsters are (fog-of-war rendering), no hearing of them, the
minimap does not mark them as enemies, turn-based combat ignores them, hostile rounds suppress the player's men only through the generic suppression
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
  -game -nullrhi -nosound -nosplash -unattended -NoTelemetry -LevelJson=L_PmcArena.json -UseFixedTimeStep -FPS=60 -FixedSeed `
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
drop links on), `time_limit_s` 240, `Spawns=Random` in the trainer's `arg_template`. A walk goal at the fighter's height that hangs over a drop or has no full path (a roof squad's advance point over the street)
is retried on the threat's level (`ExecuteOrder` Walk, only when the two levels differ by > 2.5 m). First Depot run (seed 7, 2 rounds, 240 s):
round 1 (B on a +9 m roof, 37 m) a draw without contact - B's advance from the roof stuck 13x; round 2 (A on a +9 m roof, 52 m) contact, a ladder
climbed, 1 kill, 3 flanks, suppression, still a time-limit draw (A's roof advance stuck 13x). Open: the stuck walks down from the high roofs.
The game quits after the last round. `opponent=pmc`: team 2 with weights B, the player's squad parked (any number of rounds); `player`: the player's squad
commanded with weights B (one round: the fallen cannot be revived); `bot`: the player's squad driven by `UPlaytestBotSubsystem` (one round, development
builds; the bot does not yet see hostile operatives as threats - the squad's own Aggressive fire does the fighting). The attacker alternates per round
(by the round seed's parity): the defender waits in ambush, the attacker advances on the defender's spawn (`SetTeamAssault`). The lab's cheats are forced off, the fight
starts on the first contact (`combat_start ambush`, a skirmish), a round ends when a side is down or at the time limit (a draw).

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
UnrealEditor.exe <uproject> /Game/Maps/L_FormationLabCombat -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound -unattended -LevelJson=L_PmcArena.json `
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
- Fog-of-war rendering of hostile fighters (hide / ghost like monsters), hearing, minimap enemy marks, turn-based combat with PMC fighters, the bot and
  `USquadAutonomySubsystem` perceiving hostile operatives, team-aware grenades / panic, PMC corpse loot, PMC squads placed in level JSON (a `pmc_squad`
  encounter entry), the HUD plate on a PMC fighter, suppression of the player's men by PMC rounds (checked only generically).
- The jacket material's tint / snow parameters (the user's material work) and a proper IK retarget of the Manny-only bones.
- Routing the player's controller orders through the catalog (stance / posture / cover / suppress) and an `ISquadCommandSource` facade.
