# Stealth lure (whistle / stone) and hound scent

> Any operative can WHISTLE (key 5) or THROW a stone (key 6) while exploring undetected; `UStealthLureSubsystem::EmitNoise` sends every enemy
> that hears it investigating. Hounds smell: `ScentRules` (sniff 2.5 s, follow, detect at close range). Design: `docs/design/STEALTH_LURE.md`.
>
> Key files:
> - `AI/StealthLureRules`, `AI/StealthLureSubsystem`, `AI/ScentRules`, `Characters/EnemyCharacter` (patrol search), data `enemy_perception.json`
> - Smokes `StealthLureSmoke`, `HoundScentSmoke` (map `L_PatrolTest`); tests `CodexTactics.AI.Stealth*`, `CodexTactics.AI.Scent.*`
>
> Moved verbatim from the old `docs/port/HANDOFF.md` (split of 2026-10-19; nothing was dropped). [HANDOFF](../port/HANDOFF.md) | [system docs index](README.md) | [CHANGELOG](../port/CHANGELOG.md)

## Stealth lure (old §5)

**Stealth lure (user decisions 2026-10-18, UE-only; design `docs/design/STEALTH_LURE.md`)** — any operative WHISTLES (`5` / action bar
`WHISTLE`, a noise at his feet, 12 m) or THROWS a stone (`6` / `THROW`: the grenade aim as a third mode `UGrenadeSubsystem::StartLureAim` /
`ThrowLureAtCursor`, 15 m standing by the grenade stance rule, the noise at the impact, 8 m); infinite, per-operative cooldown per kind (10 / 6 s);
only exploring undetected (`StealthLureRules::CheckAvailability`: Unable > Combat (any phase but Exploration / any combat mode) > Detected (an
enemy `IsHuntingSquad`: off patrol + target / alarm point / horde knowledge) > Cooldown). `UStealthLureSubsystem::EmitNoise`: EVERY enemy on
patrol / guard duty that hears it (`StealthLureRules::Hears`: radius x HearingOcclusionPerWall per wall like footsteps, `CountHearingWallsTo`,
x1.25 while already searching) investigates via the existing patrol search with a lure spec (`AEnemyCharacter::StartPatrolSearchWith(Location,
FPatrolSearchSpec)`: 60 s, sweep 4 m, the walk to the noise at 0.75 x its own speed - hounds fast, Brutes slow; the spec travels to leader /
escorts and round-trips in the save), then back to its route / post; never a sighting (no `NotifyHostileContact`). **Re-target rule (fix 2026-10-18):**
a NEWER noise wins over the point an enemy already searches / investigates (`StealthLureRules::ReactToNoise` -> `ENoiseReaction`: Retarget = origin
moved, the listen beat again, walk + clock restart; SameNoise = its mate pulled it in this instant; KeepScent = a hound following a scent within 3 m;
a sniffing hound / far scent drops the scent, nose held - only the close 2 m detection - until it reaches the noise, `bScentHeldByNoise`); the whole
patrol (leader + every escort, searching or not) moves to the newest noise. **Round walls:** a lure muffled on the straight line is still heard when
the listener's navmesh walk to it (`GetHearingPathLengthTo`) fits the open-air radius (`StealthLureRules::HearsAlongPath`; footsteps unchanged).
A leg also ends when the enemy stands >= 0.5 s within 2 m + 1.5 m of its goal (`StealthLureRules::HasArrived`: two enemies on one point). Stone = `ALureStoneActor`
(grenade arc, noise on impact, placeholder sphere). UI: action-bar buttons with cooldown / refusal tooltips, hearing circle while aiming the
stone or hovering WHISTLE + `HEARS` over the listeners (`GetPreviewListeners`), `?` over investigating enemies
(`ACodexTacticsHUD::DrawStealthLureMarkers`), feed `Whistle.` / `Stone thrown.`. Commands `Stealth.Whistle` / `Stealth.Throw` (F1 «Squad»), events
`StealthLure.Whistle` / `.Throw`. Data `enemy_perception.json` `"lure"` (`EnemyPerception::GetLure`, CVar `Codex.Lure.InvestigateSeconds`). Save:
`world_state.stealth_lure` cooldowns, patrol `lure` / `search_duration` / `search_sweep` / `search_approach`. Tests `CodexTactics.AI.StealthLure.*`;
smoke `StealthLureSmoke`. Stealth-kill hook: `IsInvestigatingLure` / `HasReachedSearchOrigin` / `GetSearchOrigin` (design doc §8).

## Hound scent (old §5)

**Hound scent (game designer 2026-10-18, user decisions 1 / 2 / 4; design `docs/design/STEALTH_LURE.md` §2a)** — smell is no
longer a flat 12 m instant detection: radius **6 m**; every wall between nose and operative x`smell_occlusion_per_wall` 0.3 (4 traces:
straight / +80 cm / 50 cm either side, the clearest counts = a doorway lets it through; `AEnemyCharacter::CountScentWallsTo`,
`ScentRules::Smells`); a scent only ALERTS: the hound stops and sniffs 2.5 s (HUD `~ SNIFF ?`, floating `SNIFF...`, feed «The hound
caught a scent.»), then follows slowly (0.8 x patrol pace, `~ SCENT ?`); detection by the nose only within 2 m in open air
(`ScentRules::IsCloseDetection`, `EPatrolAlertCause::Smell`) or by sight; scent lost (1 s) / trail too long (20 s) = a scent search at
the last scent point (20 s, 3 m; `FPatrolSearchSpec::bScent`, save `patrol.scent`) then back to route / post. State machine
`ScentRules::Step` (None / Sniffing / Following -> Detect / LoseScent), driven by `TickScentPerception` / `TickScentBehavior`; the
fight is unchanged. Data: `FROST_HOUND` keys `smell_*` / `sniff_seconds` / `scent_*` (`FEnemyPerceptionParams`). Tests
`CodexTactics.AI.Scent.*`; smoke `HoundScentSmoke` (L_PatrolTest). `GetLastPatrolBreakCause` / `HasBrokenPatrol` for smokes.

