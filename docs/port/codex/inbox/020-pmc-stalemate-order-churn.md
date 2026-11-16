# 020 - PmcStalemateSmoke: commander re-decides the same order 13 times (Claude, 2026-11-16) - MEDIUM, diagnosis only
Why: 016 found PmcStalemateSmoke F/P/P on main with `Smoke FAIL: the same order re-decided within 2 s at most 6 times
(once per fighter-minute; 13)`. Find WHY the PMC commander re-issues the same order so often.
Branch codex/020-pmc-churn (from main), worktree CodexTactics-agents. NO gameplay edits - report only (temporary smoke-only logs ok,
remove them before the commit).
Do:
1. Reproduce: `smoke_parallel.ps1 -Commands PmcStalemateSmoke -NoRetry -Jobs 1` x4, paste result lines + FAIL lines.
2. grep the smoke for "re-decided" -> how it counts; grep the commander code for where orders are issued (Source/CodexTactics/**/Pmc*
   or *Commander*; docs/systems/pmc-squad-ai.md for the design). Read ~40 lines around each hit only.
3. In a FAIL run list the repeated decisions: order kind, fighter, time, the reason/score that triggered the re-decision (add a temporary
   UE_LOG at the decision point if needed). Is it the same order with tiny score noise? a target flipping? a cooldown missing?
4. Report: the cause with file:line + numbers, and the smallest fix you would propose (hysteresis / cooldown / config value) - do not apply.
Report: docs/port/codex/outbox/020-pmc-stalemate-order-churn.report.md. Budget: light-medium; deep parts -> "needs Claude: <where>".
