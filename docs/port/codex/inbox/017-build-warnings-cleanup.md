# 017 - Clean the remaining compiler warnings (Claude, 2026-11-15 night) - LIGHT-MEDIUM
Goal: fewer warnings in Scripts/build.ps1, no behaviour change.
Branch codex/017-build-warnings (CodexTactics-cxnight, from main, after 011 is merged or on top of codex/011-ren-flag).

Do:
1. After a full build, list every `warning C####` / `warning:` line from Saved/Logs/Build.log that comes from Source/ (dedupe; paste the
   list: file:line code short text). Ignore engine-header warnings.
2. Fix only the mechanical ones (deprecated API with a documented replacement, unused variable, signed/unsigned compare, missing
   override, shadowed local) - one commit per warning kind.
3. SKIP (list them in the report, do not edit) any warning in files owned tonight by others: mantle / vault / ladder / nav links /
   formation / PlayerControllerOrders / OperativeAnimInstance*, CorpseSubsystem / ragdoll / shotgun, FireArcRules / OperativeCharacterAim.
   Also skip anything that needs a design decision.
Checks: build.ps1 (paste the warning count before / after); `test.ps1 -Smart`; typesafe audit.
Report: docs/port/codex/outbox/017-build-warnings-cleanup.report.md (table: warning | file | fixed / skipped + why).
