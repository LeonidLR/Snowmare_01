# Task 149 - demos to weekend trainer

Status: MERGE-READY for Lead review of the tooling. Steps 1 and 2 complete; step 3 prepared and verified with one short real calibration (with startup/adapter fixes described below). **Full weekend execution remains gate-blocked (G2/G3); no night-readiness pass is claimed.** No long worker run, scheduling, push or champion application. Resumed from WIP `ca5ea241`; passed tests and engine trials were not repeated.

## Dataset and measurable choice

Inputs: the 17 read-only recordings in `C:/Users/Zephyrus15Duo/Documents/Unreal Projects/CodexTactics/Saved/Demos`. All are legacy format: no 121 hit/damage/kill events and no notes sidecars are present. The actual scan contains 65 fights/legacy sessions: 48 completed (29 victories, 13 defeats, 6 draws), plus 17 unfinished sessions/tails. One unfinished session is lab-flagged and is excluded from score-profile support. Source SHA-256s, per-fight outcomes/durations/seeds/sectors and evidence are in [the frozen dataset](149-demos-to-trainer.dataset.json); [the full summary](149-demos-to-trainer.dataset.md) includes every recording.

| Tactic | Detections | Fights | Recordings | Eligible W/L/D | Win rate |
|---|---:|---:|---:|---:|---:|
| fallback_to_cover_after_contact | 72 | 34 | 13 | 17/6/4 | 63.0% |
| high_ground | 18 | 13 | 8 | 6/2/2 | 60.0% |
| distract_and_flank | 0 | 0 | 0 | 0/0/0 | unknown |
| prone_then_crawl_to_cover | 3 | 3 | 3 | 0/0/2 | 0.0% |
| first_in_cover_overwatch | 17 | 15 | 10 | 8/3/2 | 61.5% |
| flank_into_rear | 0 | 0 | 0 | 0/0/0 | unknown |
| approach_unseen | 0 | 0 | 0 | 0/0/0 | unknown |
| lure_then_snipe | 0 | 0 | 0 | 0/0/0 | unknown |
| unwatched_flank_seconds | 51 | 51 | 16 | 21/13/5 | 53.8% |
| scan_pitch_range | 0 | 0 | 0 | 0/0/0 | unknown |

Rates are descriptive associations among completed, non-lab, non-truncated fights; zero detections do not prove the tactic was absent. ABORTED session footers never erase completed round results or turn unfinished tails into defeats. Actor detections are counted separately from fights.

Choice: **(a), PMC doctrine scoring**, using explicitly labelled existing arena proxies. The 33 eligible fights with contact-to-cover support `retreats_survived` at weight 0.6875; the 15 with cover-overwatch support `behaviour.suppress_then_move` at 0.3125. Both caps are 2 and the total extra fitness is capped at 1 point. Frequency sets priority; win correlations do not set rewards. Missing, boolean or non-finite metrics receive no credit and remain reported as missing; crashes and explicit no-contact rounds receive no doctrine bonus. Legacy behaviour defaults are separated from observed measurements.

Retreat survival does not prove rapid cover response; suppress-then-move does not prove cover or elevation. High ground, scan pitch/coverage, stealth distraction and reacting to enemy flanks lack equivalent validated arena measurements, so they are excluded. No squad autonomy weights were changed. Drawn routes/comments are preserved as `should_have` intent and decoded via demo_notes.py only with explicit matching map/index geometry; they never become performed actions or successful tactic labels. This path is fixture-tested because there are no real notes yet.

## Weekend preparation

`weekend_trainer.json`: fixed seed 20261127, X6 sector spawn/objective boxes, 204 held-out pairs (34 per sector), 120-second rounds, up to two processes per pair. The dedicated run_weekend_trainer.ps1 prepares Saved-only champion/bounds/sector/config copies, runs the existing GA with the frozen profile, then compares its candidate with the frozen initial approved champion. Both evaluation arms are side A vs that same champion on side B; baseline is champion vs itself. Learning and held-out seeds use different streams.

The paired runner freezes hashes/config/plan, resumes healthy cached jobs, preserves partial results on STOP/deadline, stops after crashes, and writes morning.md/json with wins/draws, attacker captures, kills, coverage, discordant paired wins and three replayable example rounds. Full nights require the existing gate pass within 24h. Nothing calls apply_champion.ps1. The inclusion boxes restrict arena spawns/objectives, not fighter movement via new boundary walls; these are PMC self-play fights on X6 geometry, not interactive player-sector rounds.

After Lead merge/sync and a fresh training gate on the intended isolated worker checkout:

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/Trainer/run_weekend_trainer.ps1 `
  -Dataset docs/port/codex/outbox/149-demos-to-trainer.dataset.json `
  -Champion Content/Data/AI/pmc_commander.json -RunId weekend-20261127 `
  -TrainingHours 2 -EvaluationHours 10
```

Use the actual approved champion path if different. A configured round count is not a throughput guarantee: incomplete runs exit nonzero and report their real count. Lead should schedule a dedicated sufficient window; the shared worker_night_loop.ps1 two-hour A/B queue window cannot be assumed sufficient for learning plus 408 evaluation rounds. No shared infrastructure script was edited. Both machines can use this on separate prepared checkouts when the designer is away; do not share a run directory.

## Short real trial

One calibration of three pairs / six successful real rounds, 20 seconds each, sectors 1/2/3. Both arms deliberately use identical game weights (A/A); this verifies the execution/report/scoring path and does not show improved AI. The trial ran only in this lane, with copied existing main binaries (BuildId 55116800, hashes in the evidence) and local read-only asset-pack junctions. No compilation or worker run was needed.

Initial cold startup attempts hit a 100-second process limit and exposed missing asset-pack links; one raw result also revealed both weights.loaded flags were false. The existing adapter had passed project-relative weight paths, but LoadWeightsFile checks them from the engine working directory before Content/Data/AI. Fixed the adapter to pass absolute paths without spaces/commas/quotes, and made weekend configs reject results unless both load flags are true. Preserved failed attempts under Saved/Trainer/task149-real-trial; corrected continuation under task149-real-trial-fixed uses a 180-second process limit and removes forced per-line log flushing. No failed attempt is counted as successful evidence.

| Arm | Completed | Wins | Draws | Captures | Kills | Crashes |
|---|---:|---:|---:|---:|---:|---:|
| baseline | 3 | 0 | 1 | 0 | 1.0 | 0 |
| candidate | 3 | 0 | 1 | 0 | 1.0 | 0 |

All 6 results confirm both requested weight files loaded. Doctrine bonuses: p0000_baseline=0.5000, p0000_candidate=0.5000, p0001_candidate=0.0000, p0001_baseline=0.0000, p0002_baseline=0.3438, p0002_candidate=0.3438.

Three example candidate rounds:

- Sector 1, seed 1599545990, won=0.0, kills=0.0, duration=20.0s: `Saved/Trainer/task149-real-trial-fixed/jobs/p0000_candidate.rounds.json`.
- Sector 2, seed 232260523, won=0.5, kills=0.0, duration=20.0s: `Saved/Trainer/task149-real-trial-fixed/jobs/p0001_candidate.rounds.json`.
- Sector 3, seed 1927705920, won=0.0, kills=1.0, duration=20.0s: `Saved/Trainer/task149-real-trial-fixed/jobs/p0002_candidate.rounds.json`.

Frozen compact proof, canonical rows, raw-result hashes, input manifest and binary hashes: [trial evidence](149-demos-to-trainer.trial.json). Raw results/logs/tracks and morning.md/json remain under `Saved/Trainer/task149-real-trial-fixed/`.

## Metrics the weekend can rely on, and the blocked gate

The unit suite's `TRAINING GATE: BLOCKED` output is an intentional negative fixture (including missing/invalid probes); it is not a live gate pass. The fixture `gate_probe_20261119_dead_metrics.json` also deliberately contains zero overwatch kills and null guard share. No persistent live `gate_blocked.json` survived in this lane. To make the current limitation reviewable without another engine run, [offline metric diagnostics](149-demos-to-trainer.metrics.json) evaluate the six saved calibration rows with the frozen trial config. These rows represent only **three independent seeds**, because the arms are A/A.

| Measurement | Saved real evidence | Weekend use |
|---|---|---|
| `retreats_survived` | 1, 0, 1 per arm | Available, varying doctrine proxy; reward weight 0.6875, cap 2 |
| `behaviour.suppress_then_move` | 1, 0, 0 per arm; explicitly observed | Available, varying doctrine proxy; reward weight 0.3125, cap 2 |
| Kills, survivors, HP lost | Kills 0/0/1, survivors 2/3/1, HP lost 292/0/352 per arm | Available, varying combat outcomes |
| Suppression, first strike, ambush count | Suppression 11.5/9/1.7 seconds; first strike 0/1/0; ambushes 0/3/0 | Available counters; ambush count does not establish ambush success |
| Moves, time in contact | Moves 4/0/6; contact 14.8/2.8/11.3 seconds | Available varying behaviour counters |
| Winner, role, end reason, duration | Both arms: two losses and one draw; all rounds end at 20-second limit | Reportable; the short trial does not exercise a win or full-round duration distribution |
| Attacker captures / `objective_win` | Role/end reason present in all six; zero captures and objective wins | Coverage is present; positive capture and reward liveness remain unproved |
| `damage_dealt` | Missing under that key in all six canonical rows | G2 fails this name; fitness instead consumes measured `opp_hp_lost` (163/124/340 per arm). This is a gate/fitness field mismatch, not evidence of no damage |
| `ambush_success`, `flanks_completed`, `flanks_succeeded`, `heals`, `damage_from_concealment`, `strikes_unobserved` | All zero in this sample | G2-blocking reward/descriptor metrics; cannot treat them as productive learning signals yet |
| `friendly_fire`, `stuck_events` | All zero | Clean penalties are allowed by G2; positive violation detection was not exercised |
| High ground, scan pitch, watched flanks, stealth distraction, guard share, overwatch kills | No validated productive arena signal here | Excluded from the demo reward; do not infer correctness or absence from defaults/zeros |

The offline G2 check fails on the seven zero metrics listed above (including `objective_win`) plus missing `damage_dealt`. GA preparation leaves QD configuration present, and the existing gate inspects those descriptor metrics even in GA mode. None of these failures were bypassed or silently removed from the existing fitness. The new doctrine terms themselves have observed, varying values, with bonuses 0.5 / 0 / 0.34375 per arm.

**G3 has no baseline for the current freeze commit.** Three short A/A pairs cannot satisfy the required >=30 rounds per scenario. No baseline was fabricated. G1 was not rechecked tonight; no sweep input was supplied to the offline diagnostic, so its missing-sweep failure is an input limitation rather than a newly observed smoke regression.

Before scheduling full nights, Lead must prepare the demo config and validate the gate against that actual config, a current matching smoke sweep, a real freeze baseline and a suitable live probe. The current gate-pass artifact checks only pass status/age in the dedicated wrapper; it is not bound to a config hash or freeze SHA. Recommended follow-up: align gate damage names with the scoring formula, check only active search descriptors, bind gate evidence to config/freeze, and establish liveness for retained rewards. Do not zero rewards or manufacture evidence merely to clear G2. The shared night loop needs a dedicated sufficient time window; changes to it are proposed here only. No long baseline/probe campaign was run during this task.

## Files and verification

- Tools: demo_dataset.py + test_demo_dataset.py. Frozen real dataset JSON and summary Markdown in this outbox.
- Trainer: doctrine.py; prepare_demo_trainer.py; weekend_trainer.py/json; run_weekend_trainer.ps1; adapter/fitness/gate/trainer integration; test_demo_trainer.py; existing adapter-path expectation updated in test_core.py.
- Handoff/tool documentation: HANDOFF.md, CHANGELOG.md, TANDEM.md, docs/tools/night-trainer.md.
- Prior-run verification, retained from the WIP handoff and not repeated: `python -m unittest discover -s Scripts/Tools -p test_demo*.py`: **43 PASS**.
- Prior-run verification: `python -m unittest discover -s Scripts/Trainer/tests`: **134 PASS**, including doctrine/missing metrics, weight-loading rejection, sector argument propagation, 204-pair balance, capture semantics and three-pair mock/resume integration. Gate BLOCKED output in the suite is the expected negative fixture, not a claimed live gate pass.
- Prior-run PowerShell parser check for run_weekend_trainer.ps1: PASS; dataset equality and original recording hash checks passed. This resume packages saved trial proof and offline diagnostics; it makes no implementation changes requiring those tests to run again.
- Real short trial: **3 pairs, 6 rounds, both weight files loaded, zero crashes**. No long-run convergence or >=200-round gameplay result claimed tonight.
- `git diff --cached --check`: PASS. Resume pre-commit TypeSafe audit: protected-path boundaries PASS, Jev risk **0.0/4** for the seven staged documentation/evidence files.
- No C++/uproject edits, so builds/automation smokes were not run. No maps, .uasset, existing Content JSON, Config, VFX, shared infra, main or play checkout files were edited; no push/merge/application. Existing untracked Content entries were left untouched on resume.

Night readiness limits: refresh the existing full-night gate (including doctrine metric liveness), supply the current approved champion, sync a complete checkout with matching binaries/assets, and schedule a sufficient worker window. Long-run strength/convergence, sectors 4/5/6 live execution, real designer notes and performance against new full-event human fights were not checked tonight.

149 FINISH | dataset + tested bounded PMC doctrine proxies + weekend preparation; short real A/A calibration complete; Lead owns review/merge/scheduling and any champion application.
