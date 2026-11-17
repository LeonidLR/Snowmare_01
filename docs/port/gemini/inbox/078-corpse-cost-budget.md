# 078 - Corpse cost over budget: 0.92 / 1.70 ms against 0.5 ms (Claude, 2026-11-17) - MEDIUM, profiling (your role) + C++
Base: main. Branch gemini/078-corpse-cost. Worktree CodexTactics-gemday (after 077 is committed, or between 077 steps while a build runs - one branch at a time).
Why: Codex 037 (docs/port/codex/outbox/037-red-smokes-triage.report.md, CorpsePersistSmoke section) measured the corpse cost smoke at
0.92 / 1.70 ms per frame, the budget is 0.5 ms. Retained ragdoll corpses (task 005 / 056 / 067: ragdoll at death, settle, freeze, kept bodies)
are the likely cost. Find the smoke that measures it (grep "CorpseCost" in Source/CodexTacticsDev and the smoke registry).
Do:
1. PLAN: reproduce (3 runs alone, -Jobs 1, and 1 run under load with 4 jobs), then profile WHERE the time goes (stat / Insights trace /
   scoped cycle counters already in CorpseSubsystem - quote numbers per function). Decide: real cost vs. measurement under load.
2. If real: reduce it without changing the look - e.g. frozen bodies stop simulating and stop ticking, pose snapshot to a static pose,
   significance by distance / off-screen, a cap of fully simulated bodies (numbers in the corpse JSON config). Never change the death look
   the designer approved (ragdoll at death, settle, freeze, Brute barely pushed back).
3. If it is load noise: show the numbers; propose (do not apply) a fair measurement in the report - never raise the 0.5 ms budget yourself.
Tests: CorpsePersistSmoke x3, the corpse cost smoke x3 alone, DeathRagdollSmoke, KnockdownSmoke, MilitaryZombieSmoke, OptimizedCharacterSmoke.
Allowed paths: Source/CodexTactics/**/Corpse*.{h,cpp}, the corpse / ragdoll JSON config, Source/CodexTacticsTests/**/Corpse*, the corpse cost
smoke file (read state only, never loosen), plan + report. EnemyAnimInstance / ABPs / .uasset: NO - ask in the chat.
