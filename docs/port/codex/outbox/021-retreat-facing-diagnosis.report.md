# 021 RetreatFacingSmoke diagnosis

Status: bounded diagnosis complete (four baseline FAILs, four diagnostic FAILs); original smoke source restored. Original-source rebuild succeeded (exit 0, 42.82 s). The Engineer cannot acquire a target because the visible pack remains outside his weapon reach; the Commander does fire again. No gameplay fix applied. No instrumented PASS was reproduced within the authorized four-run bound.

## Scope and provenance

Diagnosis only on `codex/021-retreat-facing-diag`, tested base `554b8c9dc88836216be170f6b657ca8aceef40e8`, workspace `CodexTactics-agents`. The parent checkpointed the draft report at `592280aecf30cfd05c3deacaf30493763e6ea18e`; gameplay source remains unchanged. Existing untracked imported content/configuration left untouched. Baseline binaries were the restored 019 build at source `74e129a2`; `git diff --stat 74e129a2 HEAD -- Source` is empty, establishing tracked source equivalence. Each shell sets `CODEX_AGENT=codex` and `PYTHONIOENCODING=utf-8`.

Routing: `2 | gpt-6.1-sol | bounded diagnosis complete; missing instrumented PASS needs Claude`; confidence 0.87. Diagnosis kept light-medium; deep follow-up belongs to Claude. No push, merge, or gameplay/header edits.

## Four unmodified serial baselines

Each run used `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands RetreatFacingSmoke -NoRetry -Jobs 1`. Default clock: `-UseFixedTimeStep -FPS=60 -FixedSeed`; default formation start. Read engine RESULT, not wrapper exit: `Scripts/test_map.json:67` marks this smoke `knownFail`.

| Run | Engine verdict | Wrapper exit | Engine log | Wrapper capture |
|---|---|---:|---|---|
| 1 | FAIL | 0 | `Saved/Logs/codex021-base1.log` | `Saved/Logs/codex021-base1-wrapper.txt` |
| 2 | FAIL | 0 | `Saved/Logs/codex021-base2.log` | `Saved/Logs/codex021-base2-wrapper.txt` |
| 3 | FAIL | 0 | `Saved/Logs/codex021-base3.log` | `Saved/Logs/codex021-base3-wrapper.txt` |
| 4 | FAIL | 0 | `Saved/Logs/codex021-base4.log` | `Saved/Logs/codex021-base4-wrapper.txt` |

All four contain exactly this failed assertion and final result:

```text
Smoke FAIL: both fire at the pack again (10.0 s after facing it)
Smoke RESULT: FAIL
```

Each first fired at 0.7 s, ran back and stopped at 3.8 s with the pack 32.4 m behind, and turned away up to 171 degrees during the run. Both facing checks pass at 0 degrees off the pack. At that check Commander is `turning to the threat 0, target in sights 1`; Engineer is `turning to the threat 1, target in sights 0`. The explicit player-facing check also passes. Thus the ordinary logs already separate successful retreat/body-facing from the later refire assertion; they do not identify the final blocking condition, ammo, actual stored target, or fire-gate reason.

## Historical PASS context (not an instrumented paired comparison)

`Saved/Logs/codex014-run2.log` is an earlier actual PASS: same 0.7 s initial fire, 3.8 s retreat, 32.4 m pack separation, and the same Commander/Engineer facing flags above; both fire again at **8.8 s** after facing. It predates task021, but its source b3d28df6 includes fire-gate commit a48e7540; it is not a pre-fire-gate control. No diagnostic variables were sampled there. This does not prove that merely extending the 10 s timeout is correct.

## Temporary probe and four diagnostic runs

Only `Source/CodexTacticsDev/Private/Debug/RetreatFacingSmokeCommand.cpp` was temporarily instrumented. Original bytes: `Saved/Logs/codex021-smoke-original.bin`; probe diff: `Saved/Logs/codex021-probe.diff`. Stage 3/4 samples every 0.5 s recorded both operatives' movement/sprint/own-move/stance/threat-turn/facing flags, speed, actual `GetCurrentCombatTarget()`, pre-call `GetLastFireGateBlock()`, gate OutReason, residual, clip/reserve/reload/cover, last-shot versus baseline, and shot-line verdicts. Existing process-local `Codex.Debug.FireTrace` was enabled by the temporary smoke.

`CanFireAt` is NONCONST and explicitly requests weapon/cover/wounded raises when holding a shot (`OperativeCharacter.h:376–382`); it may alter observed behavior and settle state. It was called only with an actual stored target, recording the stored block first, using that target's actor location (the nonblind real-time firing expression at `OperativeCharacterCombat.cpp:844`). Engineer had no current target at every sample: gate/residual were `no-target/not-called` / -1, retaining the last stored block. No hypothetical nearest-target CanFireAt calls were made. Commander samples include mutating calls and are not pure observation. Per-pack `DescribeShotLine`/`IsSeenByOwnSide` are read-only diagnostic queries; range rejection precedes the physical LOS trace.

The first probe build failed on private `Stance` access; only the probe was corrected to public `GetStance()` (`OperativeCharacter.h:466`). Failed log: `Saved/Logs/codex021-build-probe-first.log`. Corrected build succeeded, exit 0, 29.05 s: `Saved/Logs/codex021-build-probe.log` and `codex021-build-probe-wrapper.txt`. Shared Build.bat contention caused the long waits; no other process was stopped. Each diagnostic used the same default command and gameplay/startup parameters as baseline, with no `-Multiprocess` override.

| Run | Engine verdict / wrapper exit | Engineer nearest pack at stage 4 t=9.80 | Logs in Saved/Logs |
|---|---|---:|---|
| 1 | FAIL / 0 | 20.255 m | `codex021-diag1.log`, `codex021-diag1-wrapper.txt` |
| 2 | FAIL / 0 | 20.307 m | `codex021-diag2.log`, `codex021-diag2-wrapper.txt` |
| 3 | FAIL / 0 | 20.101 m | `codex021-diag3.log`, `codex021-diag3-wrapper.txt` |
| 4 | FAIL / 0 | 20.667 m | `codex021-diag4.log`, `codex021-diag4-wrapper.txt` |

All four failed only the refire assertion. Each has 25 samples per operative: Engineer's actual target is None in all 100 samples; all 300 per-pack Engineer shot-line queries report **out of range**, with `seen=1` in all 300. No sampled Engineer line is visibility-rejected or physically blocked. This is an early reach refusal, not proof that the untraced physical ray is clear.

Representative actual diagnostic run 1, stage 4 t=9.80 (values copied from the two `[021]` lines):

| Field | Commander | Engineer |
|---|---|---|
| move / sprint / ownMove / speed | 0 / 0 / 0 / 0.0 | 0 / 0 / 0 / 0.0 |
| stance / cover / reload | 0 / 0 / 0 | 0 / 0 / 0 |
| threat turn / facing target | 0 / 1 | 1 / 0 |
| actual target | BP_Enemy_Frostbitten_C_2 | None |
| stored gate before probe | fire gate: open | fire gate: open |
| CanFireAt OutReason / allowed | fire gate: open / 1 | no-target/not-called / 0 (sentinel, not a verdict) |
| residual | 0.00 deg | -1 (not evaluated) |
| clip / reserve | 23 / 60 | 7 / 40 |
| lastShot / ShotBefore | 19.733 / 3.683 | 3.300 / 3.300 |
| actual target shot line | SHOT: ok, clear | no-target/not-called |

```text
[021-line] stage=4 t=9.80 op=Engineer enemy=BP_Enemy_Frostbitten_C_2 distance=2025.5 seen=1 {no shot: out of range}
Smoke FAIL: both fire at the pack again (10.0 s after facing it)
Smoke RESULT: FAIL
```

Engineer first fires at 13.60 m (`[FireTrace] ... fired`, clip 7); after the retreat his first no-target trace has nearest pack member 27.67 m away, and the first stage-3 sample is 27.58 m away. The Commander initially has a transient gate turn hold (47.34 deg residual at stage 3 t=0.05), then fires by stage 3 t=0.55 (`lastShot=7.800 > before=3.683`). That transient hold does not account for the final failure. Engineer remains stopped, supplied with ammo, out of cover, and without an actual target; `turnThreat=1` is not a stuck sprint/movement order. A stored open gate cannot make an out-of-range candidate become a target.

## Condition, smallest proposal, and limits

The fixture selects the first two operatives by sorted SquadIndex (`RetreatFacingSmokeCommand.cpp:135,174–176`): runtime logs identify Commander and Engineer. The initial pack is 14 m ahead (`:185`), then the fixture orders a 26 m retreat (`:215`). Configured Engineer loadout starts with `shotgun`, with no M16 (`Content/Data/Weapons/weapons_tuning.json:115–127`); the D-1 shotgun has `attack_range_m:15`, an 8-round clip, and reserve 40 (`:256–266`). Weapon id and MaxRange were not explicitly sampled at runtime; identifying the D-1 here is a configuration-supported inference, consistent with the observed 8→7 clip, reserve 40, and 1.15 s fire interval. The runtime **out-of-range verdict itself is directly observed**.

`OperativeCharacterCombat.cpp:143–149` rejects distances beyond `CurrentWeapon->Stats.Range.MaxRangeCm`. When no candidates remain, `:481–486` clears CurrentCombatTarget; `:808–809` returns no target before the gate at `:926`. The final smoke assertion (`RetreatFacingSmokeCommand.cpp:284–290`) requires a new shot from each operative relative to the snapshot at `:250`. Commander meets it; Engineer does not. Near deadline the Engineer's nearest visible target is still 20.10–20.67 m away, beyond the configured 15 m shotgun reach. Thus this reproduced failure is a **smoke fixture/loadout-and-distance mismatch**, not evidence of a broken fire gate, ammo shortage, cover obstruction, or retreat state that failed to stop.

Smallest proposed correction, not applied: make this smoke's two test operatives explicitly carry a suitable long-range weapon before the initial fight, or select a pair whose equipped weapons cover the fixture's post-retreat separation. Keep the retreat/facing/refire checks and legitimate shotgun range intact. An explicit smoke loadout is more deterministic than assuming the production roster's first two weapons. Merely increasing the 10 s deadline still relies on enemies eventually walking into shotgun reach and does not directly isolate the behavior under test. No gameplay or weapon tuning change is justified by these samples.

There is no instrumented PASS/FAIL paired comparison: all four authorized diagnostic runs failed. The historical ordinary PASS at 8.8 s supplies context only; its unsampled target/range state cannot establish why it passed, and no causal claim about a loadout change is made.

Needs Claude: if deeper history or a paired success explanation is required, inspect the historical `codex014-run2.log` context against `FindShootTarget`/`EvaluateShotLine` at `OperativeCharacterCombat.cpp:101–149,460–495`, the smoke's pair/distance setup at `RetreatFacingSmokeCommand.cpp:135,174–185,215`, and loadouts/range at `weapons_tuning.json:115–127,256–266`. No more tests were run beyond the bounded four diagnostic runs. No fix applied.

## Cleanup

Exact original smoke bytes restored from `codex021-smoke-original.bin`; `git diff --stat -- Source` is empty. Original-source rebuild succeeded, exit 0, 42.82 s; wrapper: `Saved/Logs/codex021-build-restored-wrapper.txt`; full log: `Saved/Logs/codex021-build-restored.log`. `git diff --check` passed. Only this report remains modified. Existing untracked imported assets/configuration left untouched. Parent verified the representative failing sample and source restoration before the required boundary audit and final report commit.
