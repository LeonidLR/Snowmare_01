# 019 - CoverSmoke wall-end stand-off diagnosis

Branch: `codex/019-coversmoke-flake`; tested main/base: `74e129a2`.

**needs Claude: `Source/CodexTacticsDev/Private/Debug/CoverSmokeCommand.cpp:440` observes a persistent wrong final stand-off: 54.30 cm instead of configured 28 cm (existing tolerance 15 cm), stationary from smoke time 1.5 through 8.1 seconds.** This is not demonstrated to be an early check before settlement. No gameplay fix attempted, and no tolerance widened. Only this report remains changed.

## Baseline reproduction

Fresh base build PASS (2.93 s). Exact requested runner settings used, including `-NoRetry`, `-Jobs 1` for serial; loaded batch used default four concurrent processes, with the six requested command names.

| Run | CoverSmoke | First CoverSmoke FAIL | Other loaded smokes |
| --- | --- | --- | --- |
| Serial 1 | PASS | - | - |
| Serial 2 | FAIL | stand-off 51 cm vs 28 cm | - |
| Serial 3 | PASS | - | - |
| Loaded 1 | FAIL | stand-off 72 cm vs 28 cm | FireGate / QuickFire / Suppression / CornerHold / Pistol all PASS |
| Loaded 2 | PASS | - | All five PASS |

Evidence: `Saved/Logs/codex019-base-serial{1,2,3}.log`, `codex019-base-load{1,2}-<Smoke>.log`; runner tables in matching `.txt` files. Actual `Smoke RESULT` lines checked, not just wrapper exit codes.

## Narrow diagnosis and temporary probes

Original stage 6 (`CoverSmokeCommand.cpp:423`) waits at least 1.5 smoke seconds and while `bShimmying` up to 8 seconds. It does not require position stability. The check at line 440 compares the physical wall-end distance with `CornerStandOff(Left, false, config)` and retains its 15 cm tolerance.

Added temporary smoke-only `UE_LOG` sampling at quarter-second thresholds from the left-flank order through the check: elapsed smoke time, wall-end distance, velocity, shimmy / in-cover / corner flags, facing and location. Three loaded diagnostic batches did not reproduce the stand-off failure: their traces reached 29.21 cm. The third had an unrelated later `unknown threat, moving to his right (right after the cover entry)` clip check failure (`anim_M4_cvr_std_idle_L`); this was not mistaken for a stand-off failure.

To distinguish delayed settlement from a wrong stationary endpoint, the diagnostic probe then held the same stage until its existing 8-second bound, continuing the same sight refresh and quarter-second sampling. Three serial observations produced F/P/P. The first reproduced the wrong stand-off and never converged:

```text
t=1.50 edge=54.30 speed=0.00 shimmy=0 cover=1 corner=1 facing=0
t=2.00 edge=54.30 speed=0.00 shimmy=0 cover=1 corner=1 facing=0
t=7.80 edge=54.30 speed=0.00 shimmy=0 cover=1 corner=1 facing=0
t=8.10 edge=54.30 speed=0.00 shimmy=0 cover=1 corner=1 facing=0
Smoke FAIL: ... standing the _R stand-off from the real wall end (54 cm, stand-off 28; the clip leans 40 cm out)
```

Actor location was unchanged across those samples: `X=-366.60, Y=-6263.36, Z=92.15`. The two passing comparisons stayed at 29.21 cm through the same window. Failing observation also later hit the unrelated cover-entry clip assertion. Evidence: `Saved/Logs/codex019-observe-serial{1,2,3}.log` and wrappers; original-wait diagnostic logs `codex019-diag-load{1,2,3}.log` and wrappers.

A wait for zero velocity / entered cover / corner alone would accept the bad endpoint: all those conditions already hold. Position stability also holds. A reliable correction therefore needs gameplay investigation, which the task explicitly reserves to Claude. Read-only triage pointer: `OperativeCharacterCover.cpp:883` sets `bAtCoverCorner` using `CoverFacingRules::IsAtCorner(CoverSlot, CoverFacing, Config)`; this is a pointer for investigation, **not a proven culprit**. No wider gameplay files read or edited.

Temporary diagnostics and the observation hold were completely removed; the smoke source matches main. Both diagnostic builds PASS (25.23 s and 32.05 s); restored-source build PASS (52.75 s) before commit. There is no smoke fix or 5/5 + 2/2 success claim: the task's gameplay handoff condition was reached. Boundary audit and diff whitespace check run before report commit.

Routing: `3 | main session | diagnosis pass / needs Claude for gameplay`. Router confidence .50, route=false. No local model used.
