# Gemini 118 (Hydrogen) - Cutter killed mid-pounce hangs in the air (designer bug 2026-11-22) - AFTER 105b

Branch gemini/118-cutter-airborne from origin/main, worktree C:/Work/Lanes/CodexTactics-118. Small, one cause, prove it.
Known (read-only diagnosis 11-22, verify lines): EnemyCharacterHealth.cpp ~151 bCutterLeap = Cutter && (IsJumpAttacking() || IsFalling());
~152-157 bAirborneDeath -> capsule blocks WorldStatic only; ~223 skips TryActivateDeathRagdoll; the corpse should fall -> Landed()
(~364-377) -> FreezeCorpse; FreezeCorpse bails while IsCorpseFalling() (~289-293, ~307). Log seen: "died ... at Z=153.96", no ragdoll line.
Two suspects: (1) dressed Showcase floors / SovietHouses meshes are not WorldStatic -> never lands; (2) death in Windup/Impact ON the
ground: IsJumpAttacking true but CMC Walking -> Landed never fires.
Do:
1. Repro smoke (new, small, Source/CodexTacticsDev/Private/Debug/): spawn a Cutter on /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit,
   kill it (a) mid-leap in the air, (b) in Windup on the ground; FAIL if the corpse Z is > 30 cm above the floor trace after 3 s.
   Report which case fails BEFORE any fix.
2. Fix at the cause, minimal: e.g. airborne-death capsule also blocks WorldDynamic, and/or freeze at once when CMC is not falling.
3. Unit test for the pure part (the "airborne death" decision), smoke PASS both cases on worker slot2 -Jobs 1, EnemySmoke still PASS.
Checklist #1104. Report docs/port/gemini/outbox/118-cutter-airborne.report.md + "118 FINISH | ..." in chat. Never push, never touch main / maps / .uasset.
