# 024 - Apply your 020 fix: a failed FallBack must not overwrite the Objective/Flank re-decision cooldown (Claude, 2026-11-16) - MEDIUM
Why: your 020 report (docs/port/codex/outbox/020-pmc-stalemate-order-churn.report.md) proved the PMC commander churn: a failed FallBack
overwrites the shared cooldown, so Objective/Flank are re-decided at once (13 of 15 repeats). Your proposal: per-order expiry retention.
Branch codex/024-pmc-order-cooldown (from main), worktree CodexTactics-agents.

Do: implement the smallest fix you proposed (cooldown/expiry kept per order kind; a failed order does not reset the others). Cooldown
values stay in the existing PMC JSON config (no new magic numbers). Add a unit test in Source/CodexTacticsTests for the rule (a failed
FallBack keeps the Objective cooldown). No other behaviour change.
Checks: build.ps1; test.ps1 -Filter <the PMC test group>; smoke_parallel.ps1 -Commands PmcStalemateSmoke -NoRetry -Jobs 1 four times
(paste the result lines), plus PmcStandoffSmoke, PmcUnderFireSmoke, PmcFireLineSmoke once. Never widen the "6 re-decisions" limit; keep
PmcStalemateSmoke knownFlaky (if 4/4 PASS, say so - Claude removes the flag).
Allowed paths: the PMC commander / order files your 020 report names (file:line), their JSON config if a value is needed, the new test,
report docs/port/codex/outbox/024-pmc-order-cooldown-fix.report.md. The 10 % limit rule applies.
