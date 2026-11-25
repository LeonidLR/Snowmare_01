# G027 - PMC operator stuck at the TOP of a ladder under fire (designer 2026-11-25, X6 tunnel / bridges) - after G025

Designer: a PMC operator using the ladders at the bridge tunnel got stuck at the very top - "could not decide" to go down or up while he
was being shot. Expected: a fighter on a ladder under fire commits: finishes the current climb direction (or the shorter way off the
ladder) and leaves it; no oscillation, no idle on the top rung. Find it: ladder traversal state vs suppression / cover-reaction / retreat
orders re-targeting every tick (PMC commander re-plans, R1 fall-back cover, under-fire reaction cancelling the ladder move, top dismount
blocked). Repro smoke: PMC climbing an X6 ladder, hits at 50 % / 90 % / at the top -> off the ladder within 3 s, never > 2 direction
flips. Unit test for the "commit on ladder" rule. Ladder*, PmcCommander / PmcArena smokes PASS. BUILD ONLY ON THE WORKER, foreground,
PROGRESS lines. Branch grok/G027-ladder-commit. Report docs/port/grok/outbox/G027-ladder-commit.report.md + "G027 FINISH | ...". Never push.
