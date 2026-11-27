# Task 149b — trainer metrics, gate identity and freeze baseline

**BLOCKER for training / merge-ready claim.** The damage-field defect and unbound gate artifact are fixed and tested. The live sector gate still fails G1 and G2; G3 is scheduled, not completed. No champion was applied and no evolution was started.

Code freeze: `77f2e9f55857669deab099b144e77840d47e9425`, branch `codex/149b-trainer-metrics`, from `6698cfb6`. The following documentation/evidence commit does not move the worker's pinned code freeze. A later merged freeze SHA needs its own matching gate/baseline; these artifacts cannot authorize it.

Read CLAUDE.md, TANDEM.md, HANDOFF.md, active session state and the specified 149 defect section. No AGENTS.md was present in this lane or its filesystem ancestors.

## Metric findings

Twelve real sector rounds used the weekend's **120-second limit**, rather than the former 20-second calibration limit: 12 completed, zero crashes, both requested weights confirmed loaded in every raw result. Durations were 20.1–120 seconds; all ended by elimination, with six A wins and six B wins. There were four candidate flank attempts and no recorded arrivals. A separate, explicitly labelled two-round full-map tactical diagnostic completed without crashes and confirmed an objective capture.

The canonical rows, every G2 metric's values/range/coverage, raw-result hashes, binary hashes, smoke results, gate result and registered schedule are in [frozen evidence](149b-trainer-metrics.evidence.json). The full-map diagnostic has a different config hash and is **not substituted into the sector gate**.

| Metric | Sector probe (12 rounds) | Producer / conclusion and required exercise |
|---|---:|---|
| `ambush_success` | 0 | Producer exists: PmcArenaCommand.cpp hit listener increments `AmbushHits` only for a first hit on a victim satisfying `PmcArenaBehaviourRules::IsAmbushHit`; round finalization copies it to behaviour and `ToJson` emits it. This requires the shooter at a tactical hiding spot and the target near its threat/exit point. Still 0 in the two genuine full-map tactical rounds. **Positive liveness unresolved**, not proved merely rare; no emitter fix is claimed. Needs a hiding-spot/exit encounter, not a generic ambush order. |
| `flanks_completed` | 0 | Behaviour is explicitly copied from `Stats.FlanksSucceeded` at round finalization. Same arrival event as the next row, not an independent missing producer. **Positive liveness unresolved.** |
| `flanks_succeeded` | 0 | `UPmcSquadSubsystem::UpdateTeamRunning` increments once when a live fighter with a Flank order reaches the goal on its floor; the arena emits the statistic. Four sector attempts and two auxiliary tactical attempts produced no completion. This does not justify counting attempts as successes. Needs a flank-arrival encounter with a surviving flanker; short samples do not establish that the planner completes it. |
| `heals` | 3 (two positive rounds) | The squad increments on successful catalog Heal execution, including arrival after an approach; arena writes `Stats.Heals`. Observed 1 in `blizzard-000` and 2 in `roofs-001`. **Conditional / rare in the former 20-second probe, not dead emission.** Full rounds let a wounded patient survive and a medic act. |
| `damage_from_concealment` | 87 HP (five positive rounds) | Shot listener records target-team visibility; HP-drop sampling credits the last hit's side and concealed flag; round writer emits it. **Live / varying.** Full skirmish, blizzard and movement fights exercise it. It does not mean every shot in an ambush order was concealed. |
| `strikes_unobserved` | 11 (seven positive rounds) | First-hit listener checks the target's facing against the shooter (>45 degrees off); finalizer and writer retain the counter. **Live / varying.** Movement/contact encounters exercise it; it is not a generic stealth-success label. |
| `objective_win` | 0; auxiliary full-map tactical = 1 | Adapter derives this from winner + role + end reason. All sector rounds ended by elimination, which correctly earns no objective bonus. Full-map tactical seed 149000 captured as attacker at 40.3 seconds (`objective_win=1`); the other diagnostic round ended by elimination (`0`). **Live and scenario dependent**, not missing emission. A capture/held-to-limit encounter is needed. |

No C++ writer that unconditionally drops these seven fields was found. Four metrics now have positive real evidence (including the separately labelled objective diagnostic). Ambush success and flank completion remain **unresolved acceptance blockers**; a wired source path alone is not a liveness proof. No C++ change or C++ unit-test claim is made for them.

All other retained productive G2 metrics were observed, nonzero and varying in the sector probe, including canonical `damage_dealt` (2,885 total, range 4–455), first strike, suppression, retreats survived, moves, contact time and both doctrine proxies. `friendly_fire` and `stuck_events` were zero with complete coverage; the existing clean-penalty rule permits those zeros. No metric, reward, descriptor or G2 variance check was removed or weakened; GA still checks the configured QD descriptor metrics.

## Scenario gap uncovered by full rounds

Requested chokepoint and tactical rounds in sectors 1/2 fell back to skirmish. The actual distribution was skirmish 6, street 2, roofs 2, blizzard 2; **zero actual chokepoint/tactical rounds**. Raw logs explicitly report no chokepoint site, and tactical needs two route ends but finds one.

`PlaceTacticalScenario` only accepts the level start and mission goals inside the arena inclusion box. The current Saved sector copies preserve the original full-map goal setup; the tested boxes do not provide the two endpoints needed for a tactical route. Removing the box for the auxiliary diagnostic produced two actual tactical rounds, and one capture. That establishes a suitable diagnostic scenario but does not repair weekend sector coverage or prove ambush success. Existing Content JSON and maps were not changed. This scenario-coverage issue and the unresolved flank/ambush signals must be addressed before claiming weekend readiness.

## Canonical damage and gate identity

`damage_dealt` is now the single canonical gate/fitness field. The adapter previously omitted the arena's already-emitted side field, while fitness read `opp_hp_lost`. New rows use the attributed arena measurement; old raw documents and old saved canonical rows without it still read the opponent-HP-loss proxy. An explicit new zero wins over the proxy; invalid new measurements get no legacy credit. The proxy can include unattributed damage, so this intentionally makes newly scored evidence use the measured field rather than promising old/new score equality. Tests cover raw parsing, saved legacy rows, conflicting values, explicit zero and invalid/non-finite values.

Gate artifacts contain the semantic JSON config SHA-256, full checkout freeze SHA, and creation time. Production CLI training and both weekend entry points reject a different config, different SHA, unbound legacy artifact, future timestamp or age over 24 hours. Resume validates the **effective `run.json` config and mock status**, not just the supplied config/flag. Weekend preparation happens before identity validation. G3 additionally requires the baseline identity to match, excludes crashes, and enforces every prepared `gate_scenarios` entry at >=30 healthy actual rounds.

## Verification and live gate

- `python -m unittest discover -s Scripts/Trainer/tests`: **142/142 PASS**. The suite's negative-fixture BLOCKED output is not a live gate pass. New tests include damage compatibility, config/freeze/age rejection, effective resume config, crash exclusion, scenario propagation and campaign output/counting of placement fallbacks.
- `Scripts/test.ps1 -Smart -Base origin/main`: completed; scripts/docs changes map to **no engine unit filters and no mandatory mapped smokes**. No Source/uproject change, so no build was required by lane rules. Source trees at the code freeze, `6698cfb6` and the worker's prior binary build `fafb5f3a` are identical. Existing binaries were copied read-only into the lanes; BuildId `55116800`, hashes in evidence, local/worker runtime/dev hashes match.
- `Scripts/Tools/test_night_loop_window.ps1`: **PASS**, testing the production time resolver for evening start, legacy morning window, boundary equality and same-day window. PowerShell parser checks for the weekend wrapper, loop and scheduled queue item passed.
- Live daytime `metric_campaign.py --rounds 30` negative check: refused before starting an engine (`Baseline starts only in trainer window 16:30-08:00`). Pre-commit TypeSafe protected-path audit PASS, documentation/evidence risk 0.0/4; `git diff --cached --check` PASS.
- Explicit supplemental worker smoke batch on `/Game/Maps/Generated/L_Gen_Depot_1_X6`, `-ForceMap`, jobs 2, `-NoRetry`: the archived default map invocation refused before starting engines; the explicit X6 run below executed all seven names. No failure was reclassified as known-fail.

| Full smoke name | Result | Evidence |
|---|---|---|
| `CodexTactics.PmcCrossroadsSmoke` | FAIL | Defender-on-position share 0.67; first-shot crossfire 3.7 degrees (<45). |
| `CodexTactics.PmcAlleySmoke` | FAIL | Defender-on-position share 0.67; crossfire 10.2 degrees (<45). |
| `CodexTactics.PmcRoofDefenceSmoke` | FAIL | Guard share 0.00; one shared-cover pair. |
| `CodexTactics.PmcStalemateSmoke` | PASS | First attempt. |
| `CodexTactics.PmcArenaObjectiveSmoke` | PASS | First attempt. |
| `CodexTactics.PmcArenaTacticalSmoke` | PASS | First attempt. |
| `CodexTactics.PmcCommanderSmoke` | FAIL | Could not find an empty arena on X6 (23,660 candidates tried). |

Live `night_trainer.py --check-gate` against the actual prepared config, 12 canonical sector rows and that sweep: **G1 FAIL; G2 FAIL on ambush_success, flanks_completed, flanks_succeeded, objective_win; G3 FAIL pending baseline.** It wrote `Saved/Trainer/gate_blocked.json` in the worker lane. No gate-pass artifact was manufactured. The auxiliary capture is evidence of objective production, not a claim that the configured sector probe passed G2.

## Baseline schedule and worker boundary

Verified Windows task **`CodexTactics 149b Baseline 20261127`**, state Ready, next run **2026-11-27 16:30 worker local time**. It invokes the existing night-loop A/B queue with `-QueueOnly -TrainerFrom 16:30 -TrainerUntil 07:45 -Until 08:00`, normal worker lock/heartbeat, two arena processes. The 07:45 deadline and existing five-minute child cutoff leave the worker free before the 08:00 day gates.

Queue: worker `C:/Work/CodexTactics/Saved/Worker/ab_queue/00-149b-baseline.ps1`. It runs only the isolated worker lane `C:/Work/Lanes/CodexTactics-149b`, refuses a moved SHA, uses the frozen approved champion vs itself, requests >=30 **actual healthy rounds per scenario**, and writes `Saved/Trainer/149b-freeze-baseline/evidence.json`. Seeds alternate attacker roles and cycle the six sector copies. Placement fallback is counted under its actual scenario; up to three seed sets are attempted to fill deficits. Partial evidence survives deadline/crash; insufficient scenarios stay insufficient. The queue then writes a fresh matching gate diagnostic; it never evolves or applies a champion.

**G3 is not complete now.** The discovered route/site coverage gaps can still leave its scenario minima unmet tonight; that outcome must remain a blocker, not be filled with relabelled fallback rounds. The baseline task and its exact queue source/hash are captured in the evidence JSON. Do not remove/move the pinned worker lane before this queued run. The target source SHA is the code freeze above, not an unspecified future Lead merge.

The only shared infrastructure edit is `worker_night_loop.ps1`: its old `TrainerAt` resolved 16:30 on the next-morning deadline's date, so a 16:30 queue could never enter that window. The small resolver fix plus optional `-QueueOnly` enables this scheduled queue and exits when drained, allowing the normal guard to resume. Existing default loops/windows keep their behavior. No build/sync/merge/test/smoke infrastructure scripts were edited.

## Files / boundaries

Implementation: adapter/fitness/shared metric accessor, gate/G3 identity and compatibility validator, CLI/weekend entry points, prepared scenario requirements, `metric_campaign.py`, the small night-loop change and focused tests. Documentation: night-trainer guide, handoff/changelog/tandem and this report/evidence.

Local writes stayed in the requested lane, except authorized AgentChat posts to main Saved. Worker writes were the isolated mirrored lane, normal Saved trainer/queue/log outputs, and the explicitly requested baseline scheduled task; creating its worktree also registered normal worker Git worktree metadata. No main/play working-tree edits or checkout, no maps/.uasset/existing Content JSON/Config/VFX changes, no designer editor interaction, no push, no merge, no champion application. No pre-existing files were deleted.

Remaining acceptance work: positive controlled ambush/arrival evidence; a viable sector tactical/chokepoint scenario; green G1 placement smokes; and the scheduled baseline's real >=30/scenario result. **149b stops after this handoff; readiness is not claimed.**
