# FormationJitterSmoke - diagnosis (no fix committed)

Branch `claude-api/formation-jitter`. Verdict: **cause NOT proven within budget; diagnosis only. No code, config, JSON or smoke change is committed.**

## Reproduction
Build up to date (2 s). `smoke_parallel.ps1 -NoRetry -ExtraArgs -Multiprocess -Jobs 1 -Commands FormationJitterSmoke`: FAIL 69 s, then FAIL again in 4 more runs (see below).
`the other follower is no worse with the D-1 man in the squad (hips 27.2 / shoulders 24.0 deg/s; all-M16 17.0 / 12.6)`, identical to Codex 037 to the decimal, so the run is deterministic (fixed seed / fixed step).
Failing check: `FormationJitterSmokeCommand.cpp:689` (limit = 1.25x + 5 deg/s). The leader check (:692) and the D-1 follower's own checks pass.

## What the numbers say
Variant A = D-1 Engineer (layer on), B = D-1 layer off, C = all M16, E = D-1 Engineer leads. The "other follower" is Medic-Sapper (slot P, wedge flank) in every variant.
Medic hips/shoulders/capsule wobble deg/s: A 27.2/24.0/23.9, B 15.3/9.4/8.9, C 17.0/12.6/12.2, D (look filter off) 36.5/30.9/30.2, E 15.3/9.7/8.3.
Only A (shotgun layer ON on the *Engineer*) is high. Medic speed is the same in all (197-199 +- 49 cm/s), slot jumps 0.43/s in all, formation label sequence identical in all (9 changes, 0.28/s). So it is not speed, spacing or the formation shape/label.

Per-frame CSV (`Saved/Logs/FormationJitter-A.csv`) shows exactly where A's Medic wobble comes from: a limit cycle in his *formation look* of period ~0.65 s at the wedge (t 21.5-22.4 s, plus a single swing at 11.5 s):
`look 94.4 (held 0.30 s) -> -129.5 (0.30 s) -> 95.8 -> ...`, body capsule yaw `-130.6 -> 155 -> 100 ... -> 169 -> -134 -> 154 -> 99`, i.e. ~230 deg spins each way within 0.3 s (hips follow, up to -152 deg). Smoke log: `look jump Medic-Sapper: -131 -> 94 (t 21.50)`, `94 -> -129 (t 21.80)`, `-133 -> 96 (t 22.15)`.
With `Codex.Formation.Debug 3` the same moment reads `look Medic-Sapper P: 95 -> ClearLook 105 (facing -131, held 105, wedge walk, 2 walls)`, then `95 -> ClearLook 145 (facing 141 .. 107, held 145)`, then raw -129 at facing 96. So the raw look alternates between the wedge flank's wanted yaw (~95) and a ClearLook edge pick (105/145/-129) ~225 deg away, and each side is accepted by `FormationLookFilterRules::Step` because both hold >= `DefaultHoldSeconds` 0.15 s (`FormationLookFilterRules.h`, 35 deg / 0.15 s are constexpr, not JSON).
Relevant code: `FormationSectorRules::ClearLook` (`FormationSectorRules.cpp:164`, `Swept()` scores use the body's *current* yaw, `Ahead` uses the member's velocity x `look.anticipate_s`) called from `SquadSubsystemFormationGuard.cpp:397-448`, applied at `SquadSubsystemFormation.cpp:141-150`. The body yaw and velocity feed back into the pick (Swept by CurrentYaw, Ahead by velocity), which is the loop.

## What was NOT established (why no fix)
* The coupling from the D-1 *layer* on the Engineer to the *Medic's* ClearLook was not found. Formation code has no weapon/anim reference (`grep weapon|shotgun` in `SquadSubsystemFormation*.cpp`, `Formation/*.cpp`: none).
* The CSVs show the three variants are already chaotic-divergent from the first move: standing slots differ by 1-5 cm at t=5 s (Medic y -2093.9 A / -2094.4 B / -2089.7 C), the *leader's* yaw differs by 55 deg between A and B at t=8.35 s, Engineer yaw 72/77/83 at t=6.7 s. A sub-centimetre difference (D-1 layer pose) therefore reaches the Medic's wall-look decision, and the A/B/C/E spread (15-27) may largely be which side of the ClearLook cycle the Medic is on at each wedge turn rather than a D-1-specific force.
* Two JSON experiments (reverted, nothing committed; `Content/Data/FormationLab/formation_config.json`): `look.hold_deg` 60 -> 120 and `look.anticipate_s` 0.5 -> 0. A stayed 27.2/24.0 and 27.5/24.3 while C moved 17.0 -> 18.6 / 11.3. So neither the held-look hysteresis nor the anticipated position is what produces A's cycle, and the config numbers I tried cannot be tuned to turn this check green without luck.

## Suggested next step
Log, per frame for the Medic in A vs C over t 21-22.5 s: `Wanted`, `Centers[]` (wall directions from Pos and Ahead), `Held`, returned ClearLook, filter `Applied/Pending`. That will show which wall direction flips `Blocked(95)`. A fix would be a hysteresis on `Lookable(Wanted)` (the first return of `ClearLook`) or a longer filter hold for >120 deg jumps made JSON-tunable (`look.filter_hold_s`), plus a unit test in `CodexTactics.Formation.LookFilter`. Not done: needs a build per experiment (10+ min queue) and changes the protected-looking `FormationLookFilterRules` contract tested by FormationLabSmoke's wall-facing gate (0.3 s hold broke it, per the header comment).

## Gate
No source change, so no gate was run beyond the reproduction above (build OK; FormationJitterSmoke FAIL x5 incl. the two JSON experiments and one Debug-3 run, same assertion each time). `typesafe_triage` not applicable (empty diff).

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>

## Fix
`FormationLookFilterRules::Step` now has a second, stricter tier for big look jumps (the Medic's limit cycle between about 95 and -130 deg):
- A look change of at least `look.big_jump_deg` (default 120) from the applied look must hold `look.big_hold_s` (default 0.6 s) before it is taken. Changes of 35 to 120 deg keep the 0.15 s hold; changes of 35 deg or less pass at once, as before.
- Hysteresis: after a big jump is accepted, a jump straight back to the previous look (within 35 deg of it) is rejected for the next `look.big_hold_s`. The filter keeps `Previous` and `SinceBigJumpSeconds` for this.
- Both numbers are in `Content/Data/FormationLab/formation_config.json` next to `look.anticipate_s`. They are read through `FFormationConfig` (`LookBigJumpDeg`, `LookBigHoldS`) and `FormationTuning::Get()` in `SquadSubsystemFormation.cpp`. The key count in FormationTuningTest goes from 83 to 85.
- Tests in `FormationLookFilterTest.cpp`:
  - The existing test is adapted: its 150 deg "lasting jump" is now a medium one, 89 deg.
  - New `CodexTactics.Formation.LookFilter.BigJump`: a 0.3 s flip-flop is rejected, a stable big jump is taken after about 0.6 s, an immediate return is rejected, and small and medium changes behave as before.
- The TS golden files and the web page do not list the two new keys. `FromValues` falls back to the defaults, and the parity tests pass.

## Gate (Fix)
- `Scripts/build.ps1`: Succeeded.
- FormationJitterSmoke x3 (`-Jobs 1`): PASS x3 (76 s, 70 s, 64 s). It was FAIL x5 before the fix, so this one smoke went from red to green.
- The 34 smokes in smokeRegistry whose name contains Formation, Wall or Corner, other than FormationJitterSmoke (`-NoRetry -ExtraArgs -Multiprocess`): 34 run, 0 failed, 0 PASS*. None is red, so there is no main comparison to make. No smoke limit was loosened.
- `Scripts/test.ps1 -Filter CodexTactics.Formation`: 213 tests, 0 failed.
- `typesafe_triage.py --audit-diff --agent claude`: boundary PASS, risk 1.9 / 4.

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
