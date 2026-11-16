# 021 - RetreatFacingSmoke flake: what happens in a FAIL run (Claude, 2026-11-16) - MEDIUM, diagnosis only
Why: 014 showed RetreatFacingSmoke F/P/F on main and also failing on the old base, so no bisect. Its FAIL:
"both fire at the pack again (10.0 s after facing it)". Find what the two operatives do instead of firing in a FAIL run.
Branch codex/021-retreat-facing-diag (from main), worktree CodexTactics-agents. NO gameplay edits - report only (temporary smoke-only
logs ok, removed before the commit).
Do:
1. Reproduce `-Commands RetreatFacingSmoke -NoRetry -Jobs 1` x4; paste result + FAIL lines (it is knownFail - read the log, not the table).
2. In the smoke, find the stage (grep "fire at the pack again"); add temporary logs every 0.5 s for both operatives: state, target,
   fire-gate reason (AOperativeCharacter::CanFireAt OutReason - the new fire gate a48e7540), facing residual, ammo, cover, LOS.
3. Compare a PASS and a FAIL run: which condition blocks the shots (no target / gate reason / LOS / retreat state stuck)? file:line.
4. Propose the smallest fix (smoke timing vs gameplay) - do not apply.
Report: docs/port/codex/outbox/021-retreat-facing-diagnosis.report.md. Budget: light-medium; deep parts -> "needs Claude: <where>".
