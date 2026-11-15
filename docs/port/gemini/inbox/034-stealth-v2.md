# 034 - Stealth v2 (was Codex 008) - now YOURS (designer 2026-11-12: Codex removed). C++ on the paths below.

## Allowed paths
- Source/CodexTactics/**/Characters/Operative*Stealth*
- Source/CodexTactics/**/Characters/OperativeAnimInstance*.{h,cpp}
- Source/CodexTactics/**/Characters/OperativeCharacter*.{h,cpp}
- Source/CodexTactics/**/Characters/SquadSubsystem*.{h,cpp}
- Source/CodexTactics/**/Characters/StealthModeRules*.{h,cpp}
- Source/CodexTactics/**/Combat/TacticalSightSubsystem*.{h,cpp}
- Source/CodexTactics/**/Combat/PerceptionRules*.{h,cpp}
- Source/CodexTactics/**/AI/PmcSquadSubsystem*.{h,cpp}
- Source/CodexTactics/**/Characters/EnemyCharacterPerception*.{h,cpp}
- Source/CodexTactics/**/Characters/MarksmanEnemyCharacter*.{h,cpp}
- Source/CodexTactics/**/Core/CodexTacticsPlayerController*.{h,cpp}
- Source/CodexTactics/**/Core/PlayerCommands*.{h,cpp}
- Source/CodexTactics/**/Tactics/CoverGhostActor*.{h,cpp}
- Source/CodexTactics/**/UI/ActionBarWidget*.{h,cpp}
- Source/CodexTactics/**/UI/CodexTacticsHUD*.{h,cpp}
- Content/Data/AI/stealth_mode.json
- Content/Data/Weapons/weapon_anim_sets.json
- Source/CodexTacticsDev/Private/Debug/StealthModeSmokeCommand.cpp
- Source/CodexTacticsTests/Private/Characters/StealthModeRulesTest.cpp
- docs/port/gemini/inbox/034-*.md
- docs/port/gemini/outbox/034-*.md

## Spec / Description
Spec = docs/port/codex/inbox/008-stealth-8dir-fast-lean.md (all 5 sections; do section 4 FIRST - the cover order must keep stealth,
then 5 - Shift+LMB look anchor, then 1-3). Base: branch `gemini/034-stealth-v2` from `interim/2026-11-12` (Codex's stealth 003 code
is there: OperativeCharacterStealth.cpp, OperativeAnimInstanceStealth.cpp, StealthModeSmokeCommand.cpp, StealthDetectionSmoke...).
Allowed paths: Source/CodexTactics/**/Characters/Operative*Stealth*, OperativeAnimInstance*.{h,cpp} (stealth layer only),
OperativeCharacterMovement / Stance (stealth branches only), Core/CodexTacticsPlayerController*.{h,cpp} + PlayerCommands (stealth
input / orders only), UI/ActionBarWidget (stealth only), Content/Data/Weapons/weapon_anim_sets.json (stealth block), the stealth
smokes + tests. Nothing else in Source without asking Claude in the chat first. No .uasset / maps / packs / Config.
Allowed paths ADDED (Claude 2026-11-12, chat #297) - stealth branches only, behaviour outside stealth byte-identical:
Content/Data/AI/stealth_mode.json; Characters/OperativeCharacter.h; Characters/OperativeCharacterCover.cpp;
Characters/OperativeCharacterAim.cpp; Characters/OperativeCharacterCornerAim.cpp; Characters/StealthModeRules.{h,cpp} + tests;
Tactics/CoverGhostActor.{h,cpp}; UI/CodexTacticsHUD.cpp (look marker + line only); Characters/SquadSubsystem.cpp (stealth reset on
formation gather / ExitSoloMode only); Characters/EnemyCharacterPerception.cpp, Characters/MarksmanEnemyCharacter.cpp,
AI/PmcSquadSubsystem.cpp, Combat/TacticalSightSubsystem.cpp, PerceptionRules.{h,cpp} - ONLY the stealth detection / noise
multipliers via StealthModeRules (one call site each), with unit tests; no other perception logic.
Checks pasted into docs/port/gemini/outbox/034-stealth-v2.report.md: build.ps1; test.ps1 -Filter CodexTactics.Stealth and
CodexTactics.Animation; smokes StealthModeSmoke (extended per 008), StealthDetectionSmoke, InjuredLocomotionSmoke,
ProneCrawlSmoke, FormationLabSmoke, SoloStartSmoke; Jev audit --agent gemini; game-camera frames -> MAIN
Saved/Orchestrator/stealth/v2/. Commit after every working part; chat lines only with --topic gemini-034 and a hash.
The designer is waiting for this - it is your top priority now.

## ADDED + CORRECTED (designer 2026-11-12): NO stealth in FORMATION mode
The designer does NOT want stealth in formation at all (the earlier "make it work in formation" note is cancelled).
- In formation mode the STEALTH button is disabled (greyed, tooltip "Стелс - только в соло") and key 7 does nothing; stealth is
  available only in SOLO mode (and for operatives outside the formation).
- If an operative is in stealth and the squad gathers into formation (B / formation order), stealth ends for everyone joining it.
- Smoke: in formation the toggle is refused (no state change, the button is disabled); after B from solo-stealth nobody stays in
  stealth.
