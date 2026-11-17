# 077 - PMC order lifecycle: completed orders, heal approach, silent Suppress, cover stop, firing readiness (Claude, 2026-11-17) - MEDIUM-LARGE, C++
Base: main. Branch gemini/077-pmc-order-lifecycle. Worktree CodexTactics-gemday.
Why: PmcStalemateSmoke / PmcEncounterHuntSmoke / RpgPmcSmoke are red or flaky; the PMC squad "churns" (re-issues the same order, stands
silent, hunters never close in). Codex 030 already DIAGNOSED it with log lines: docs/port/codex/outbox/030-pmc-residual-churn.report.md
(read it fully first - causes 1-6 with PmcSquadSubsystem.cpp line numbers) and 020 / 024 (the per-kind failed-order cooldown, merged).
Architecture: docs/port/PMC_AI_ARCHITECTURE*.md (or grep "utility commander") - shared operative body + order catalog + utility commander.
Do:
1. PLAN first (rule 4): for each 030 cause - the fix in one sentence, the file/function, how you will prove it (log line / smoke check).
2. Fix the order lifecycle, one cause per commit:
   a) a completed FallBack / TakeCover is reported DONE to the commander (no re-issue of the same order on the same spot);
   b) the heal approach ends when its move ends (no "Heal running" without a patient in reach; cancel rules);
   c) Suppress started from sight memory is an emitted order the commander sees (no silent Order replacement);
   d) a short interrupted cover approach ("stops to take the shot") is not read as a completed TakeCover;
   e) firing readiness: after an order ends the fighter re-engages a visible target (no quiet spells with a target in sight and range).
   Numbers (ranges, timeouts) go to the PMC JSON config, not C++ literals.
3. Unit tests for each pure rule (CodexTactics.Pmc.*). Smokes: PmcStalemateSmoke x3, PmcEncounterHuntSmoke x3, RpgPmcSmoke x3,
   plus PmcEncounterSmoke and the other Pmc* smokes once (find names in the smoke registry; full names).
Allowed paths: Source/CodexTactics/**/Pmc*.{h,cpp} (AI folder), its JSON config under Content/Data/**/pmc*.json, Source/CodexTacticsTests/**/Pmc*,
Source/CodexTacticsDev/Private/Debug/Pmc*Smoke*.cpp and RpgPmcSmokeCommand.cpp (only to read state / add checks - never loosen a limit),
the plan + report. Anything else (OperativeCharacter*, Squad*, weapons) -> ask in the chat first.
Done = the three smokes 3/3 green on your branch, all Pmc* smokes green, unit tests green, gemini_gate PASS, report with a before/after table.
If a cause needs a change outside the allowed paths: write "needs Claude" with the evidence and do the other causes.
