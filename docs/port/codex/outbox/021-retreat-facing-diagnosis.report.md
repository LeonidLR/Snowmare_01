# 021 RetreatFacingSmoke diagnosis — draft, probe pending

Status: baseline complete; temporary probe build queued behind another Build.bat. No gameplay fix applied. Instrumented comparison and restoration/rebuild are PENDING; do not treat this draft as task completion.

## Scope and provenance

Diagnosis only on `codex/021-retreat-facing-diag`, HEAD `554b8c9dc88836216be170f6b657ca8aceef40e8`, workspace `CodexTactics-agents`. Existing untracked imported content/configuration left untouched. Baseline binaries were the restored 019 build at source `74e129a2`; `git diff --stat 74e129a2 HEAD -- Source` is empty, establishing tracked source equivalence. Each shell sets `CODEX_AGENT=codex` and `PYTHONIOENCODING=utf-8`.

Routing: `2 | gpt-6.1-sol | in progress`; confidence 0.87. Diagnosis kept light-medium; deep follow-up belongs to Claude. No push, merge, or gameplay/header edits.

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

## Temporary probe (PENDING build/run)

Only `Source/CodexTacticsDev/Private/Debug/RetreatFacingSmokeCommand.cpp` is temporarily instrumented. Original bytes are saved at `Saved/Logs/codex021-smoke-original.bin`; temporary diff at `Saved/Logs/codex021-probe.diff`. Stage3/4 samples every 0.5 s include both operatives' movement/sprint/own-move/stance/threat-turn/facing flags, speed, actual `GetCurrentCombatTarget()`, pre-call `GetLastFireGateBlock()`, gate OutReason, residual, clip/reserve/reload/cover, last-shot versus baseline, and shot-line verdicts. Existing process-local `Codex.Debug.FireTrace` is enabled by the temporary smoke.

`CanFireAt` is NONCONST and explicitly requests weapon/cover/wounded raises when holding a shot (`OperativeCharacter.h:376–382`); it may alter observed behavior and settle state. The probe calls it only when an actual stored combat target exists, records the stored block first, and uses that target's actor location (the nonblind real-time firing expression at `OperativeCharacterCombat.cpp:844`). With no current target, gate and residual are `no-target/not-called` / -1, with the last stored block retained; no hypothetical nearest-target CanFireAt call is made. Per-pack `DescribeShotLine`/`IsSeenByOwnSide` are read-only diagnostic queries. Shot-line checks reject invisibility before physical trace, so “not visible to the squad” must not be called a measured wall/LOS obstruction.

First temporary build failed because the probe accessed private `Stance`; corrected only the probe to public `GetStance()` (`OperativeCharacter.h:466`). First failed log: `Saved/Logs/codex021-build-probe-first.log`. Corrected build is queued; no valid probe result yet.

## Condition, proposal, and follow-up (PENDING)

Current evidence establishes refire failure despite successful stopping and pack-facing; it does not yet establish no-target versus fire-gate versus ammo/LOS. Do not choose a gameplay fix or increase the timeout from baseline evidence alone. Instrumented runs are bounded to four; if all fail, label the missing paired PASS explicitly.

Relevant narrow inspection locations: `OperativeCharacterCombat.cpp:114–117` (squad visibility refusal), `:481–486` (no visible candidates clears current target), `:808–809` (no-target return), `:844` (actual nonblind aim point), `:926` (actual fire-gate call). Public diagnostics: `OperativeCharacter.h:382,387,397,2250`.

Needs Claude: exact condition-specific follow-up and smallest smoke-versus-gameplay proposal after probe evidence; missing instrumented PASS if bounded reproduction does not produce one. No fix applied.

## Cleanup (PENDING)

Restore exact original smoke bytes, prove no Source diff, rebuild originals successfully, and replace this pending section with verification. Existing imported untracked assets/configuration must remain untouched.
