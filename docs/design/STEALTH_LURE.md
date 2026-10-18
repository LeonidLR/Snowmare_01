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
- **Investigation** reuses the existing patrol SEARCH (`AEnemyCharacter::StartPatrolSearchWith` with a lure spec):
  1. The enemy walks to the noise at 75 % of its own normal speed, and never below its patrol pace. Hounds therefore come fast and Brutes slowly. The smoke measured 405 cm/s for a hound and 180 cm/s for a Brute.
  2. It sweeps random reachable points within **4 m** of the noise and looks around at each one, for **60 s**.
  3. It then goes back. A patrol returns to the nearest waypoint of its route, a guard walks back to its post, and an escort rejoins its leader.
- The whole patrol searches together: the leader and the escorts of an enemy that heard the noise come too. This is the existing patrol rule.
- During the investigation the enemy's sight, hearing and smell are heightened (×1.25), as in any search. A **hound** that comes within its smell radius (12 m, 15 m while searching) smells the operative. This is the existing smell rule. In practice the whistle cannot lure a hound without the hound smelling the whistler, because the whistle radius equals the smell radius. **Use the stone on hounds.**
- Enemies that already hunt the squad, dead enemies, and enemies held by a turn-based fight ignore lures.

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

Console: `Codex.Perception.HearingScale` scales both radii, like every other hearing radius. `Codex.Lure.InvestigateSeconds` (-1 = data) overrides the investigation time. `CodexTactics.DumpPerception` logs the values in force.

## 5. Tutorial and levels

- **Demo 01**: beat A0 locks `Stealth.*` together with the other families that are not taught yet. There is no lesson for it yet, so it stays locked until the safety unlock-all: a wave or horde fight, or the tutorial switched off. A future lesson unlocks it with `"unlock": ["Stealth.*"]`. The test `CodexTactics.Tutorial.Lock.ProgressiveDemo` checks every registry id, so a new command must appear in the A0 lock.
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
| Investigation | `AEnemyCharacter::StartPatrolSearchWith` + `FPatrolSearchSpec`, `IsInvestigatingLure`, `IsHuntingSquad`, `CountHearingWallsTo` (`Characters/EnemyCharacter.*`) |
| Stone aim (third aim mode next to the grenade and the flare) | `UGrenadeSubsystem::StartLureAim` / `ThrowLureAtCursor`, `AGrenadeAimActor::SetLureStyle` / `ShowRing` (`Combat/GrenadeSubsystem.*`) |
| Keys | `ACodexTacticsPlayerController::StealthWhistleKey` / `StealthThrowKey` (`CodexTacticsPlayerControllerItems.cpp`), stone click in `...Selection.cpp`, Enhanced Input `IA_StealthWhistle` (5) / `IA_StealthThrow` (6) |
| UI | `UActionBarWidget::RefreshStealthButtons`, `ACodexTacticsHUD::DrawStealthLureMarkers` |
| Data | `EnemyPerception::GetLure` / `LureFromJson` (`Data/EnemyPerception.*`) |
| Smoke | `Source/CodexTacticsDev/Private/Debug/StealthLureSmokeCommand.cpp` (`StealthLureSmoke` on L_PatrolTest; `Shot` takes the rendered stills `Saved/Screenshots/stealth_lure_*.png`) |

## 8. Future: stealth kill hook (not part of this feature)

A stealth kill needs an enemy that is distracted and has its back turned. The lure produces exactly that:

- `AEnemyCharacter::IsInvestigatingLure()`: it is busy with the noise. `HasReachedSearchOrigin()`: it is at the noise and looking around. `GetSearchOrigin()`: the noise point.
- While it walks to the noise it faces away from where it came from. So an operative coming from behind the patrol's previous position approaches its back.
- Suggested rule for later: a crouched operative within ~1.5 m behind (outside its field of view) of an enemy where `IsInvestigatingLure()` is true gets a `Stealth.Kill` command. The kill is silent and does not raise `NotifyHostileContact`. The investigation spec could carry a "distracted" flag that halves its sight while it looks around.

## 9. Placeholders / requests

- Throw trail / impact puff / impact click and the whistle sound are placeholders. The stone is a plain engine sphere with no trail, no sound and no VFX. This is TANDEM request #23 to Gemini.
