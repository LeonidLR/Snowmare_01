# Stealth lure: whistle and stone throw

Status: implemented 2026-10-18 (branch `agents/work`). UE-only feature; the Godot archive has no reference.
Source of the rules: the user's decisions of 2026-10-18 in `production/session-state/active.md` («Queued 2026-10-18»).

## 1. What the player does

| Action | Key | Action bar | Effect |
|---|---|---|---|
| **Whistle** | `5` | `WHISTLE [5]` | The selected operative whistles. The noise is at his own feet. |
| **Throw a stone** (or a bolt, or a spent cartridge) | `6`, then LMB on the ground | `THROW [6]` | The grenade-style aim starts: a range ring, an arc, and the **hearing circle** at the aim point. A click throws, RMB or Esc cancels, `6` again cancels. The noise is at the impact point. |

- Both work for **any operative**. They are **infinite**: there is nothing to pick up and no ammo.
- Each operative has his own **cooldown per kind**: the whistle 10 s, the throw 6 s. The cooldown starts when he whistles or throws.
- Both work **only while the squad explores undetected**. Once a fight starts (real time, tactical pause or turn-based), or an enemy hunts the squad, both are refused. The buttons are disabled (`WHISTLE --`) and the tooltip gives the reason. A key press posts the reason in the feed. If a fight starts during the stone aim, the aim ends.
- The noise is **not a sighting**. It never reveals the operative by itself. If an enemy can already see him, the usual detection applies.

## 2. What the enemies do

- **Every** enemy on patrol or guard duty that hears the noise goes to look. There is no "only one" choice: a hound and a marksman both come, and a horde in range all comes. Luring a horde is therefore pointless.
- **Hearing** works like footsteps. The open-air radius is 12 m for the whistle and 8 m for the stone. Each wall between the enemy's ear and the noise multiplies the radius by the archetype's `hearing_occlusion_per_wall` (0.5). At most 3 walls count, the same as `PerceptionRules::OccludedRadius`. An enemy that is already searching hears 1.25 times farther, which is the search perception multiplier that applies to every patrol sense.
- **Round walls** (fix 2026-10-18): the noise also goes round a wall and through a doorway. When the straight line is muffled, the enemy still hears it as in the open if its navmesh walk to the noise fits the open-air radius (x1.25 while searching). `StealthLureRules::HearsAlongPath`, `AEnemyCharacter::GetHearingPathLengthTo`. A wall without a gap nearby still muffles it: the frostbitten 8 m beyond a 12 m wide wall in StealthLureSmoke would have to walk ~15 m. Footstep hearing is unchanged (straight line only).
- **A newer noise wins** (re-target rule, fix 2026-10-18, `StealthLureRules::ReactToNoise`): an enemy that already searches or investigates (a stone, a whistle, a trap blast, a lost scent) and hears a new lure moves its search there. Its origin moves, it listens again for `listen_seconds`, walks to the new point (a whistle: towards the whistler on its navmesh; a stone: the impact point) and its search clock restarts. A second throw elsewhere and a second whistle from the same spot later do the same. The same rule applies to every noise that starts a patrol search (trap, campfire, lost leader). Exceptions: the same noise within 0.1 s and 50 cm (a patrol mate already pulled it in) changes nothing, and a hound that follows a scent closer than 3 m (`ScentHoldsNoiseCm`) keeps following (see §2a). Stronger stimuli keep their priority: sight and footsteps still break the patrol, and an enemy that hunts the squad ignores lures.
  *User report 2026-10-18*: a stone first, then the whistle, and the enemies kept turning at the stone. Root cause (his log and `CodexTactics.LureProbe replay` on a copy of his L_PatrolTest): after the stone the hound and the marksman stood 7.5 m and 10.8 m from the whistler with one wall between. Searching, the radius was 15 m x 0.5 = 7.5 m, so neither heard it (`0 enemies heard it`), while their walk through the doorway was only 10.9 m and 14.1 m. Without the stone the patrol passed in line with the doorway, so the whistle alone worked. In another try only the marksman heard it, and his hound escort kept sweeping the stone because a patrol mate that was already searching was never moved.
- **Investigation** reuses the existing patrol SEARCH (`AEnemyCharacter::StartPatrolSearchWith` with a lure spec):
  0. It first **stops and listens** for `listen_seconds` (0.75 s), turned towards the noise, then sets off (fix 2026-10-18, see below).
  1. The enemy walks to the noise at 75 % of its own normal speed, and never below its patrol pace. Hounds therefore come fast and Brutes slowly. The smoke measured 405 cm/s for a hound and 180 cm/s for a Brute.
     **Where it walks to** (`AEnemyCharacter::ResolveSearchPoint`, `StealthLureRules::ChooseInvestigatePoint`): the noise is projected onto the enemy's own navmesh. A whistle's noise is the whistler's feet, which can lie in his capsule, against a wall or off the navmesh. If a full path leads there, the enemy goes to that point, round walls and through doorways. If the path is partial (no way round), it goes to the path's end, as close as it gets. Off the navmesh it goes to the raw point. It has arrived within **2 m** (`LureArriveCm`), because the whistler's body may stand on the point. A refused move request is asked again on the next frame, not after a 1 s wait. After 2 refusals the goal is resolved again, to a reachable point within 3 m. After 5 refusals, or the 10 s leg timeout, it searches from where it stands and logs `[Stealth] <enemy> cannot get to ...`. Each resolution logs `[Stealth] <enemy> walks to <goal> for the noise at <noise> (reachable / partial path / off the navmesh, N cm off it)`.
     A leg also ends when the enemy has stood still for 0.5 s within 2 m + 1.5 m of its goal (`StealthLureRules::HasArrived`): two enemies walking to the same point block each other there, and the second one used to push for 10 s at 2.08 m.
     *User report 2026-10-18*: the THROW worked, but at the WHISTLE the enemies only turned on the spot. Before the fix the walk went to the raw noise point with no projection and no partial-goal handling. A refused or failing move left the enemy standing until its 10 s leg timed out, and then it started the look-around turn there. The smokes' open-ground and doorway cases walked correctly before the fix as well, and `CodexTactics.LureProbe` (dev) prints a listener's approach on any map. The user should re-check his case.
  2. It sweeps random reachable points within **4 m** of the noise and looks around at each one, for **60 s**.
  3. It then goes back. A patrol returns to the nearest waypoint of its route, a guard walks back to its post, and an escort rejoins its leader.
- The whole patrol searches together: the leader and every escort of the patrol of an enemy that heard the noise come too, whether they were already searching or not (since 2026-10-18 they also move to a newer noise).
- During the investigation the enemy's sight, hearing and smell are heightened (×1.25), as in any search. A **hound** that comes within its smell radius (6 m, 7.5 m while searching) catches the operative's scent. Since 2026-10-18 the scent only alerts it, and walls block it (see §2a). A whistle behind a wall therefore works on hounds too: the hound comes round the wall, sniffs, and the whistler has time to slip away.
- Enemies that already hunt the squad, dead enemies, and enemies held by a turn-based fight ignore lures.

## 2a. Hound scent (game designer 2026-10-18; user decisions 1, 2 and 4)

The problem was that a frost hound smelled an operative through a wall and detected him at once, before the player could react. Smell was a flat 12 m radius that ignored walls. It now works as follows (exploration and stealth only; the hound's combat behaviour is unchanged):

1. **Walls block scent.** Every world-blocking surface between the hound's nose and the operative multiplies the smell radius by `smell_occlusion_per_wall` (0.3). One wall leaves 1.8 m, two leave about 0.5 m. This is the hearing model (`PerceptionRules::OccludedRadius`, at most 3 walls). The game traces four lines: a straight one, one over a low obstacle (+80 cm), and one 50 cm to each side of the operative. The clearest trace counts (`ScentRules::CombineTraceWalls`), so an open doorway or gap near the line lets the scent through as open air. Pawns do not block scent.
2. **Smell alerts, it does not detect.** When the hound catches a scent it **stops and sniffs** for `sniff_seconds` (2.5 s). It stands still and does not turn. The player sees a green `~ SNIFF ?` marker over it, a floating `SNIFF...` and the feed line «The hound caught a scent.». Then it **follows the scent slowly** to where it smells the operative, at `scent_follow_speed_fraction` (0.8) × its patrol pace, about 150 cm/s against its ~540 cm/s run. The marker then reads `~ SCENT ?`. It detects the operative only when it is **within `smell_detect_m` (2 m) in open air**, with no wall in between, or when it **sees** him under the usual sight rules.
3. **Losing the scent.** If the operative moves out of reach, the scent counts as lost after `scent_lost_seconds` (1 s). This happens at the end of the sniff or while the hound follows, and also when the hound has followed for `scent_follow_max_seconds` (20 s). The hound then investigates the last scent point. This is a patrol search with a scent spec (`IsInvestigatingScent`, the amber «?»): `scent_search_seconds` (20 s) within `scent_sweep_radius_m` (3 m). Then it returns to its route or post, like after a lure. If it catches the scent again during that search, it follows at once without a second sniff.
4. **Radius 6 m** (was 12 m), because cold air carries scent poorly. A hound that came to a whistle (12 m) therefore no longer smells the whistler at once.

Stances change nothing for the nose. No stance effect was added, and wind is not modelled yet.

The player can back off during the sniff, throw a stone elsewhere, or set an ambush where the hound will arrive.

**A lure while the hound is on a scent** (decision 2026-10-18, re-target rule): while it **sniffs**, a whistle or a stone it hears wins. It drops the scent (`[Stealth] <hound> drops the scent`), listens, and walks to the noise. On its way there its nose is held: it does not catch a new scent, but the close detection (2 m, open air) still works. At the noise the nose works again. The same applies when it **follows** a scent whose point is more than 3 m away. If it follows a scent **within 3 m** (`StealthLureRules::ScentHoldsNoiseCm`), it is about to find the operative and ignores the noise (`[Stealth] <hound> keeps following a fresh scent`). Its own lost-scent search is a search like any other, so a newer noise re-targets it. The nose hold is runtime only and is not saved, like the sniff and follow phases.

| Key (`enemy_perception.json`, `FROST_HOUND`) | Value | Meaning |
|---|---|---|
| `smell_radius_m` | 6 | open-air scent radius (×1.25 while searching; `Codex.Perception.SmellScale`) |
| `smell_occlusion_per_wall` | 0.3 | radius × this per wall |
| `smell_detect_m` | 2 | the nose alone detects this close, in open air only (not scaled while searching) |
| `sniff_seconds` | 2.5 | stands and sniffs before it follows |
| `scent_lost_seconds` | 1 | no scent this long = lost |
| `scent_follow_max_seconds` | 20 | gives up a trail it never closes on |
| `scent_follow_speed_fraction` | 0.8 | follow pace × its patrol walk speed |
| `scent_search_seconds` | 20 | a lost scent is investigated this long |
| `scent_sweep_radius_m` | 3 | ... within this radius of the last scent point |

Code: pure rules `AI/ScentRules.*` (`Smells`, `IsCloseDetection`, `Step` = the None → Sniffing → Following state machine with Detect / LoseScent, `GetFollowSpeed`; tests `CodexTactics.AI.Scent.*`). In the world, `AEnemyCharacter::TickScentPerception` is called from the patrol perception and `TickScentBehavior` drives the sniff and the follow before the route, post, escort or search. Other parts: `CountScentWallsTo`, `GetScentPhase` / `IsOnScent` / `IsInvestigatingScent`, the HUD marker in `DrawStealthLureMarkers`. The save stores `patrol.scent` with the search spec. The sniff and follow phases last a few seconds and are not saved. Log lines: `[Stealth] scent caught / followed / close / lost by <hound>`. Smoke: `HoundScentSmoke` (L_PatrolTest). StealthLureSmoke now keeps the hound's nose on.

## 3. UI

- **Action bar**: `WHISTLE [5]` and `THROW [6]` (steel blue) sit after the vehicle button. During a cooldown the label reads `WHISTLE 7s`. When refused the label reads `--` and the tooltip gives the reason. During the stone aim the THROW button is bright.
- **Hearing circle**: during the stone aim the 8 m ring at the aim point is drawn bright cyan, and the throw-range ring is dim. Hovering over WHISTLE draws the 12 m ring around the selected operative.
- **Highlight**: the enemies that would hear the noise, with walls already counted, get a cyan `HEARS` box over their heads (`UStealthLureSubsystem::GetPreviewListeners`, drawn by `ACodexTacticsHUD::DrawStealthLureMarkers`).
- **«?»**: every enemy that is investigating a lure gets an amber `?` box over its head until its search ends. A floating `? NOISE` appears when it starts.
- **Feed**: `Whistle.`, `Stone thrown.`, plus refusal lines such as `Whistle again in 7 s.` and `The squad has been spotted - nobody will fall for it now.` and `Only while exploring - useless once the fight is on.`
- **F1 Controls**: two rows in the «Squad» section: `5` Whistle and `6` Throw a stone.
- **Commands**: `Stealth.Whistle` is reported when he whistled. `Stealth.Throw` is reported when the stone flies. Events: `StealthLure.Whistle` and `StealthLure.Throw` (the Subject is the operative, the Location is the noise point).

## 4. Numbers (`Content/Data/AI/enemy_perception.json`, section `"lure"`)

| Key | Value | Meaning |
|---|---|---|
| `whistle_radius_m` | 12 | open-air hearing radius of a whistle |
| `throw_radius_m` | 8 | open-air hearing radius of the stone's impact |
| `throw_range_m` | 15 | farthest throw while standing (crouched 75 % = 11.25 m, prone 50 % = 7.5 m; the grenade stance rule) |
| `whistle_cooldown_seconds` | 10 | per operative |
| `throw_cooldown_seconds` | 6 | per operative |
| `investigate_seconds` | 60 | how long an enemy investigates |
| `investigate_sweep_radius_m` | 4 | the sweep points lie this close to the noise (the trap search uses 12 m) |
| `approach_speed_fraction` | 0.75 | speed of the walk to the noise as a fraction of the enemy's normal speed (never below its patrol pace) |
| `listen_seconds` | 0.75 | the enemy stands and listens (turned to the noise) this long before it walks there (fix 2026-10-18) |

Console: `Codex.Perception.HearingScale` scales both radii, like every other hearing radius. `Codex.Lure.InvestigateSeconds` (-1 = data) overrides the investigation time. `CodexTactics.DumpPerception` logs the values in force.

## 5. Tutorial and levels

- **Demo 01**: beat A0 locks `Stealth.*` together with the other families that are not taught yet. The A4 ambush lesson (2026-10-19 b) teaches it: once a patrol walked into the squad's wire, the card `A4_lure` ("LURE - Whistle [5] from cover to call him to you, or throw a stone [6] ...") unlocks it and completes on the event `StealthLure.Heard` (raised by `EmitNoise` for every enemy it sends investigating: Subject = the enemy, Target = the lurer). On the lake the routes pass 12-16 m from the overlook: the stone works from there, the whistle needs the operative closer (dev probe `CodexTactics.LakeLureProbe`). The test `CodexTactics.Tutorial.Lock.ProgressiveDemo` checks every registry id, so a new command must appear in the A0 lock.
- **Other levels** (no tutorial, or the tutorial off): always available while exploring undetected.
- **Tactical pause**: the game has no exploration pause (Space toggles the pause only inside a wave). Because the lure is refused in every fight, a throw cannot be planned in the pause. This is intentional and documented here.

## 6. Save / load

- Cooldowns are saved in `world_state.stealth_lure` as `{ "<operative name>": { "whistle_s", "throw_s" } }` (remaining seconds; only cooldowns that are still running).
- An enemy's investigation round-trips with its patrol snapshot: `searching`, `search_origin` and `search_elapsed` as before, plus `lure`, `search_duration`, `search_sweep` and `search_approach`. After a load it is still investigating the same point, it still shows its «?», and its clock is not reset.
- A stone in flight is not saved. The cooldown is already counted, and the noise is lost. This is an accepted edge case.

## 7. Code map

| Piece | File |
|---|---|
| Pure rules (radius / walls / gate / cooldown / labels / approach pace) | `Source/CodexTactics/{Public,Private}/AI/StealthLureRules.*` — tests `CodexTactics.AI.StealthLure.*` |
| World logic (availability, Whistle, ThrowAt, EmitNoise, preview, save) | `AI/StealthLureSubsystem.*` (`UTickableWorldSubsystem`) |
| The stone (flight on the grenade arc, noise on impact) | `AI/LureStoneActor.*` (placeholder: a 7 cm engine sphere; `UStealthLureSubsystem::StoneClass` / BP `OnLanded` hook) |
| Investigation | `AEnemyCharacter::StartPatrolSearchWith` (returns `ENoiseReaction`; `ApplySearchNoise` per patrol member) + `FPatrolSearchSpec`, `IsInvestigatingLure`, `IsHuntingSquad`, `CountHearingWallsTo`, `GetHearingPathLengthTo` (`Characters/EnemyCharacter.*`) |
| Re-target / round-wall / arrival rules | `StealthLureRules::ReactToNoise`, `HearsAlongPath`, `HasArrived` - tests `CodexTactics.AI.StealthLure.Retarget`, `.PathHearing`, `.InvestigatePoint` |
| Stone aim (third aim mode next to the grenade and the flare) | `UGrenadeSubsystem::StartLureAim` / `ThrowLureAtCursor`, `AGrenadeAimActor::SetLureStyle` / `ShowRing` (`Combat/GrenadeSubsystem.*`) |
| Keys | `ACodexTacticsPlayerController::StealthWhistleKey` / `StealthThrowKey` (`CodexTacticsPlayerControllerItems.cpp`), stone click in `...Selection.cpp`, Enhanced Input `IA_StealthWhistle` (5) / `IA_StealthThrow` (6) |
| UI | `UActionBarWidget::RefreshStealthButtons`, `ACodexTacticsHUD::DrawStealthLureMarkers` |
| Data | `EnemyPerception::GetLure` / `LureFromJson` (`Data/EnemyPerception.*`) |
| Smoke | `Source/CodexTacticsDev/Private/Debug/StealthLureSmokeCommand.cpp` (`StealthLureSmoke` on L_PatrolTest; stage F = the user's stone-then-whistle case behind a doorway wall, G = two throws in a row; `Shot` takes the rendered stills `Saved/Screenshots/stealth_lure_*.png`); `HoundScentSmoke` stage E (a whistle during the sniff) and B (a whistle during a close follow) |
| Dev probe | `CodexTactics.LureProbe [throw]` (a noise 10 m behind the nearest enemy) / `replay [stone=X,Y whistle=X,Y]` (a stone at the user's point once the patrol is near, then the whistle while they search it; logs walls / walking path / the approach) |

## 8. Future: stealth kill hook (not part of this feature)

A stealth kill needs an enemy that is distracted and has its back turned. The lure produces exactly that:

- `AEnemyCharacter::IsInvestigatingLure()`: it is busy with the noise. `HasReachedSearchOrigin()`: it is at the noise and looking around. `GetSearchOrigin()`: the noise point.
- While it walks to the noise it faces away from where it came from. So an operative coming from behind the patrol's previous position approaches its back.
- Suggested rule for later: a crouched operative within ~1.5 m behind (outside its field of view) of an enemy where `IsInvestigatingLure()` is true gets a `Stealth.Kill` command. The kill is silent and does not raise `NotifyHostileContact`. The investigation spec could carry a "distracted" flag that halves its sight while it looks around.

## 9. Placeholders / requests

- Throw trail / impact puff / impact click and the whistle sound are placeholders. The stone is a plain engine sphere with no trail, no sound and no VFX. This is TANDEM request #23 to Gemini.
