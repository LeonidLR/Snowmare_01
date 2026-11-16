# 020 — PMC same-order churn diagnosis

Diagnosis only, 2026-11-16. Worktree `CodexTactics-agents`, branch `codex/020-pmc-order-churn`, HEAD `4ea7be9d07aaacaf4b815d2be2cefa9504b85659`. No gameplay/source edits or temporary logging were made. Used the existing freshly restored binaries supplied for this task; no rebuild was necessary. Imported untracked content/config were preserved. No push, merge, or main-checkout changes.

## Finding

The dominant reproduced loop loses an existing failed-order cooldown when another candidate fails. Team 1 Operator Vance re-takes Objective/Flank every 0.5 s after that walk ends short: the next decision first refuses FallBack, which overwrites the single remembered failure. Objective/Flank immediately becomes eligible again despite its promised 5 s cooldown. This explains 13 of the 15 counted same-order repeats in current run 3, already more than its limit of 7. The re-takes are `best`, not early preempts; score noise is not the enabling cause.

## Four serial reproductions

Executed separately four times: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands PmcStalemateSmoke -NoRetry -Jobs 1`, with a distinct ResultJson per invocation. Default fixed step `-UseFixedTimeStep -FPS=60 -FixedSeed`; no retries. Each actual game log was copied immediately before the next invocation could replace the shared smoke log.

| Run | Wrapper result line | Round duration (rounded), outcome | Same / printed limit | Quiet s | Failed orders | Track under Saved/Arena |
|---|---|---|---|---|---|---|
| 1 | PmcStalemateSmoke FAIL 113.8s | 180.0, A/time_limit | 19 / 19 | 50.2 | 296 | 20261116-093322/round_1.track.json |
| 2 | PmcStalemateSmoke PASS 86.3s | 107.5, A/elimination | 11 / 11 | 22.7 | 38 | 20261116-093518/round_1.track.json |
| 3 | PmcStalemateSmoke FAIL 78.6s | 63.2, B/elimination | 15 / 7 | 13.2 | 86 | 20261116-093651/round_1.track.json |
| 4 | PmcStalemateSmoke FAIL 81.1s | 81.5, A/elimination | 21 / 9 | 14.9 | 45 | 20261116-093808/round_1.track.json |

Exact assertion/result messages (numeric decimal punctuation normalized from wrapper locale only):

```text
run1 Smoke FAIL: the fight resumes within 45 s after every quiet spell (longest 50.2 s)
run1 Smoke RESULT: FAIL
run2 Smoke RESULT: PASS
run3 Smoke FAIL: the same order re-decided within 2 s at most 7 times (once per fighter-minute; 15)
run3 Smoke RESULT: FAIL
run4 Smoke FAIL: the same order re-decided within 2 s at most 9 times (once per fighter-minute; 21)
run4 Smoke RESULT: FAIL
```

All three FAILs reached completed gameplay assertions and wrote round tracks; none was an infrastructure timeout/startup failure. Run 1 fails quiet time only: its 19 repeats meet the printed limit. Runs 3 and 4 fail the requested churn assertion. All four had contact and zero early preempts. Orders/fighter-minute were 8.00, 10.60, 11.39, 12.52 respectively, below 20. Existing startup Python toolset import errors do not prevent these completed smoke results.

Evidence retained: `Saved/Logs/codex020-run{1,2,3,4}.log` (actual engine logs), `codex020-run{1,2,3,4}-wrapper.log`, and `codex020-run{1,2,3,4}.json`. Raw result/track JSON remains at the timestamped Saved/Arena locations above.

## Counter and decision path

- `Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp:178`: strict threshold `< 2.f`.
- `PmcArenaCommand.cpp:1612`: listens to successful `OnFighterOrder` broadcasts. Lines 1623–1633 compare the immediately preceding order for that fighter, on world-clock age; same enum and age < 2 s increments QuickSameOrder. A target or goal change is not required or tested. Failed attempts alone do not increment it.
- `PmcArenaCommand.cpp:2205`: allowed count is `max(2*squad, ceil(2*squad*Flow.Duration/60))`, not always 6. With squad 3 the floor is 6, and longer rounds permit more. The earlier brief's 6 applied to its short round. Internal unrounded duration is used; `duration_s` is rounded to tenths at line 2037. Run 1's observed limit is 19 even though its exported duration rounds to 180.0. No threshold adjustment is proposed.
- `Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:1090`: a pending walk that is no longer running and is short of goal records failure (400 cm tolerance, defined at line 48).
- `PmcSquadSubsystem.cpp:1396`: commitment copies only `State.FailedOrder` and its age. `:1400` avoids reissuing a same order while running. `:1409` executes, then `:1410` permits two immediate retries for nonstatic candidates.
- `PmcSquadSubsystem.cpp:1992`: `MarkFailed` unconditionally replaces `State.FailedOrder` and `FailedAt`. `:1413` likewise replaces `Commitment.Failed` with the newly refused candidate. The prior failure's remaining cooldown is lost.
- `Source/CodexTactics/Private/AI/PmcCommanderRules.cpp:287` and `:293`: only that single enum is filtered during 5 s failure cooldown. `:305` applies minimum hold/margin only when running. Once the walk ended, another `best` pick is possible immediately.
- `PmcSquadSubsystem.cpp:1427`: successful pick is broadcast with `Scored[0].Score`. This is the top raw score, not necessarily the chosen order's score after cooldown/retry. Tables below correctly call it top score. The existing trace cannot prove target identity changes; none is needed to explain the failure-slot overwrite.

Design reference: `docs/systems/pmc-squad-ai.md`, section 11, states minimum active hold 3 s and failed-order cooldown 5 s, with failed walks not re-decided every decision. The single-last-failure implementation does not retain that protection across two distinct failures.

## All repeated decisions in the churn-FAIL run 3

From `Saved/Arena/20261116-093651/round_1.track.json`, filtering `type=tactic`, keyed by `(team, who)`, consecutive same enum with interval < 2 s. All 15 rows match the smoke count. Times are arena-relative track seconds; the smoke itself uses world-clock ages. All intervals below are 0.50 s. `why` is the existing recorded reason; score is the rounded top raw score broadcast, as described above.

| Team / fighter | Order | Previous → current t | Reason previous → current | Top score previous → current |
|---|---|---|---|---|
| 1 / Operator Vance | Objective | 12.5 → 13.0 | preempt → best | 1.20 → 1.20 |
| 1 / Operator Vance | Objective | 13.0 → 13.5 | best → best | 1.20 → 1.20 |
| 1 / Operator Vance | Objective | 13.5 → 14.0 | best → best | 1.20 → 1.19 |
| 1 / Operator Vance | Objective | 14.0 → 14.5 | best → best | 1.19 → 1.18 |
| 1 / Operator Vance | Objective | 14.5 → 15.0 | best → best | 1.18 → 1.17 |
| 1 / Operator Vance | Objective | 15.0 → 15.5 | best → best | 1.17 → 1.17 |
| 1 / Operator Vance | Objective | 15.5 → 16.0 | best → best | 1.17 → 1.16 |
| 1 / Operator Vance | Flank | 28.5 → 29.0 | best → best | 0.91 → 0.94 |
| 1 / Operator Vance | Flank | 29.0 → 29.5 | best → best | 0.94 → 1.00 |
| 1 / Operator Vance | Flank | 29.5 → 30.0 | best → best | 1.00 → 1.00 |
| 1 / Operator Vance | Flank | 30.0 → 30.5 | best → best | 1.00 → 1.00 |
| 1 / Operator Vance | Flank | 30.5 → 31.0 | best → best | 1.00 → 1.02 |
| 1 / Operator Vance | Flank | 31.0 → 31.5 | best → best | 1.02 → 1.03 |
| 2 / Operator Lind | FallBack | 32.5 → 33.0 | best → best | 0.97 → 0.97 |
| 1 / Operator Kessler | Reload | 60.5 → 61.0 | preempt → best | 0.80 → 0.80 |

Concrete run 3 log sequence (`Saved/Logs/codex020-run3.log`): line 1742 Vance Objective score 1.20; 1744 Objective failed ended-short; 1754 FallBack failed could-not-be-taken; 1755 Objective score 1.20 again. It repeats at 1757–1759, then 1761/1767/1769. First two re-takes have identical rounded score and identical threat distance 898 cm: tiny score noise is unnecessary. For Flank, lines 1920/1922/1925/1927 show score 0.91 → ended-short → FallBack refusal → Flank 0.94. Subsequent half-second repeats continue through 31.5 s. These 7 Objective + 6 Flank repetitions alone exceed the permitted 7.

The other two rows are different: Lind FallBack at 32.5/33.0 (log lines 1988/1993), and Kessler Reload at 60.5/61.0. They are not proven to share the overwrite cause. In the inspected commander path, a nonrunning order can be taken again without successful-completion cooldown; Reload running status is `bIsReloading` (`PmcSquadSubsystem.cpp:993`). Needs Claude: whether those individual completions/refusals warrant order-specific debounce, using the owning order execution code. No protected animation/combat or movement systems were inspected to pursue them.

Older corroboration, explicitly not current revision: `Saved/Logs/codex016-run1.log` from revision `401bb22c` and track `Saved/Arena/20261116-084001/round_1.track.json` have 13 same-order repeats versus floor 6. Filtering that old track finds five Objective and eight Flank repeats, all team 1 Vance at 0.5 s, with the same failed-walk / failed-FallBack sequence. Current runs 3/4 establish the issue still occurs independently.

## Smallest proposed fix — not applied

Retain a failed-until timestamp per order kind per fighter, and pass every still-cooling order into `PickCommitted`, including candidates refused in the immediate retry loop. A refusal must add an exclusion instead of replacing the previous one. Keep the existing 5 s value initially; increasing it or adjusting switch margins cannot recover a cooldown whose record was overwritten. Scope is the PMC failure memory/commitment filter, not movement behavior or target choice. A focused regression should simulate Objective failing, FallBack failing during retry, then verify both remain excluded until their individual expiries; repeat with Flank.

Needs Claude: why Vance's accepted walks end short, any independent Reload/FallBack repeat behavior, and run 1's 50.2 s quiet spell. These are outside this light-medium diagnosis and no speculative behavior change is proposed for them. The overwrite cause above is sufficient to violate the churn limit without resolving those deeper owners.

Verification boundary: no temporary source code was added, so nothing needed removal/restoration and no restoration rebuild was required. Only this report was authored; logs/results are generated diagnostics. Parent reviewed the causal code snippets and current run 3 log before the required boundary audit and report commit.

Routing: `2 | gpt-6.1-sol | pass (diagnosis)`; router confidence .45, route=false. No local model used.
