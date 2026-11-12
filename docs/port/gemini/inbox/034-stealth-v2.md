# 034 - Stealth v2 (was Codex 008) - now YOURS (designer 2026-11-12: Codex removed). C++ on the paths below.

Spec = docs/port/codex/inbox/008-stealth-8dir-fast-lean.md (all 5 sections; do section 4 FIRST - the cover order must keep stealth,
then 5 - Shift+LMB look anchor, then 1-3). Base: branch `gemini/034-stealth-v2` from `interim/2026-11-12` (Codex's stealth 003 code
is there: OperativeCharacterStealth.cpp, OperativeAnimInstanceStealth.cpp, StealthModeSmokeCommand.cpp, StealthDetectionSmoke...).
Allowed paths: Source/CodexTactics/**/Characters/Operative*Stealth*, OperativeAnimInstance*.{h,cpp} (stealth layer only),
OperativeCharacterMovement / Stance (stealth branches only), Core/CodexTacticsPlayerController*.{h,cpp} + PlayerCommands (stealth
input / orders only), UI/ActionBarWidget (stealth only), Content/Data/Weapons/weapon_anim_sets.json (stealth block), the stealth
smokes + tests. Nothing else in Source without asking Claude in the chat first. No .uasset / maps / packs / Config.
Checks pasted into docs/port/gemini/outbox/034-stealth-v2.report.md: build.ps1; test.ps1 -Filter CodexTactics.Stealth and
CodexTactics.Animation; smokes StealthModeSmoke (extended per 008), StealthDetectionSmoke, InjuredLocomotionSmoke,
ProneCrawlSmoke, FormationLabSmoke, SoloStartSmoke; Jev audit --agent gemini; game-camera frames -> MAIN
Saved/Orchestrator/stealth/v2/. Commit after every working part; chat lines only with --topic gemini-034 and a hash.
The designer is waiting for this - it is your top priority now.
