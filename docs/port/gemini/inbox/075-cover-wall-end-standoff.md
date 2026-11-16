# 075 - CoverSmoke: operative ends 54 cm from the wall end instead of 28 (Claude, 2026-11-16) - MEDIUM, C++
Base: main. Branch gemini/075-cover-standoff.
Why: Codex 019 (docs/port/codex/outbox/019-coversmoke-standoff-flake.report.md) proved the CoverSmoke flake is a real gameplay end
spot: sometimes the operative settles STILL at 54.3 cm from the real wall end (config stand-off 28 cm, tolerance 15) for 6+ s, ~1 run in 3
(more under load). The check is CoverSmokeCommand.cpp:440; the corner shimmy stops ~30 cm along its path.
Do: reproduce with logs (CoverSmoke -NoRetry -Jobs 1 x5); log the shimmy target, the computed wall-end point and why the shimmy stops
early in a FAIL run (frame-rate dependent step? an arrival radius? a nav projection snapping back? a timer?); fix the cause so the end
spot is the configured stand-off at any frame rate. Never widen the 28 cm / 15 cm. Then CoverSmoke 6/6 PASS.
Allowed paths: the cover / corner shimmy code you prove is at fault (OperativeCharacterCover*.cpp / CornerAim / CoverFacingRules - not
OperativeCharacterAim.cpp or the fire gate; for those write "needs Claude: <what>"), its JSON config, a unit test, report.
Checks: build, test.ps1 -Filter CodexTactics.Tactics, CoverSmoke x6, CornerHoldSmoke, CornerEntrySmoke, CoverCrouchSmoke, FireGateSmoke once.
