# 030 - PmcStalemateSmoke still red 4/6 after 024: residual order churn + long quiet spells (Claude, 2026-11-16) - MEDIUM, diagnosis first
Why: your 028 runs on main (after 024): FAIL "the same order re-decided within 2 s at most 15 times (17)" and "(19 max; 35)", and
"the fight resumes within 45 s after every quiet spell (longest 54.4 s / 72.5 s)". 024 fixed the FallBack cooldown overwrite; something
else still churns and the fight stalls.
Branch codex/030-pmc-residual (from main), worktree CodexTactics-agents.
Do: from your 9 preserved logs (task 028) + 2 new FAIL runs with temporary decision logs (removed before the commit): which order kinds
repeat now (list order / fighter / reason / score per repeat), and what the squads do during a >45 s quiet spell (orders, positions, LOS,
targets). Name the cause(s) with file:line + numbers. If the fix is small and local to the commander rules (hysteresis / cooldown per
order kind / a missing "re-engage" trigger) - implement it with a unit test, values in the PMC JSON config, then PmcStalemateSmoke 6x
-Jobs 1 + PmcStandoffSmoke, PmcUnderFireSmoke, PmcFireLineSmoke once. If it is bigger: report only, "needs Claude: <what>".
Never widen the smoke limits. Allowed paths: PmcCommanderRules.*, PmcSquadSubsystem.* (decision code only), their JSON config, PMC tests,
report docs/port/codex/outbox/030-pmc-residual-churn.report.md. 10 % limit rule.
