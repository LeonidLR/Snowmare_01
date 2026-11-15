# 013 - Panic flee speed respects stance and wounds (designer 2026-11-15, board 062)
Bug: an operative in panic moves very fast - even wounded and prone he crawls away "like possessed".
Goal: panic flight speed = the normal speed of his CURRENT state (prone = crawl speed, crouched = crouch speed, injured-tier multipliers
applied), never above it. Panic may pick the fastest gait that state allows (e.g. standing -> run, not sprint unless the normal rules
allow sprint for him), but never a speed the same state would not have outside panic.
Branch codex/013-panic-speed (CodexTactics-cxnight, `git switch -c codex/013-panic-speed main`).

Read first: Source/CodexTactics/Private/Characters/PanicComponent.cpp, PanicRules.cpp/.h, the injured-tiers speed multipliers
(grep InjuredTier / injured_tiers in Source/ and Content/Data), the prone crawl speed (grep Crawl speed in OperativeCharacter*).
Do:
1. Find where panic sets the walk speed / gait (MaxWalkSpeed, speed overrides) and why it ignores stance and wounds - write it in
   the report with file:line.
2. Pure rule in PanicRules: PanicFleeSpeed(stance, injured tier, normal speeds) -> speed; unit tests in PanicRulesTest.cpp
   (prone wounded <= crawl, crouch <= crouch, standing healthy = run, badly wounded standing <= wounded run).
3. Use it in PanicComponent. No magic numbers: speeds come from the existing configs.
4. PanicSmoke: add a case - a prone, badly wounded operative panics -> measured speed <= his crawl speed * 1.05.
Allowed paths: PanicComponent.*, PanicRules.*, PanicRulesTest.cpp, PanicSmokeCommand.cpp, your report. Anything else -> ask in chat.
Checks: build.ps1; `test.ps1 -Filter CodexTactics.Characters.Panic`; `smoke_parallel.ps1 -Commands PanicSmoke,InjuredLocomotionSmoke,ProneCrawlSmoke`;
typesafe audit.
Report: docs/port/codex/outbox/013-panic-flee-speed.report.md.
