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

## ADDED (designer 2026-11-12): stealth does not work in FORMATION mode
Designer: "in formation mode stealth does not work" (details pending). Required behaviour (defaults until he says otherwise):
- STEALTH / key 7 with the squad in formation (or the leader selected in formation) puts ALL formation members into stealth.
- The formation moves at the stealth pace (Slow; Fast on a double click), every member uses the stealth 8-direction clips; nobody
  stands up or runs; followers keep their formation look sectors (torso) while the legs follow the move (like the wounded follower).
- Cover orders / regroup (B) inside stealth keep stealth (same as section 4); a member breaks stealth only by firing or by a
  confirmed detection, and the others stay in stealth.
- The Shift+LMB look anchor (section 5) applies to the leader; followers keep their sectors.
Find why it fails today (formation move path / follower stance / speed bypassing the stealth branch), fix it on your 034 branch,
extend StealthModeSmoke with a formation case (3 members, toggle, move, cover order -> all in stealth, crouched, speed <= stealth).
