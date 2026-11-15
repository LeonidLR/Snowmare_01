# 067 - Ragdoll: bodies sink into the ground + impulse too strong (designer 2026-11-15 evening)
Суть: убитые не проваливаются наполовину в снег и не улетают «как из пушки» от нескольких стрелков или РПГ.
Реестр: death-ragdoll
Base: origin/main (58a7e34e or newer); branch gemini/067-ragdoll-tunnel; worktree: CodexTactics-interim
(`git -C ..\CodexTactics-interim switch -c gemini/067-ragdoll-tunnel origin/main` - your 059b branch stays as it is, Claude reviews it).

## Goal (both observable in DeathRagdollSmoke + a new case)
A. **Tunnelling.** The designer's screenshot: a zombie killed at speed lies half inside the snow and stays stuck after settling.
   1. Turn on CCD for ragdoll bodies while simulating (`BodyInstance.bUseCCD` on the physics asset bodies / `SetUseCCD(true)` on the
      mesh) and switch it off again at the settle freeze.
   2. Clamp the linear speed right after the death impulse: no body faster than `max_launch_speed_cm_s` (600) for normal damage and
      `max_launch_speed_explosive_cm_s` (900) for Explosive (both new keys in `death_ragdoll.json` + `FDeathRagdollConfig`).
   3. Check the freeze path: `CorpseSubsystem.cpp` ~line 370-377 snaps the enemy mesh relative Z to `-HalfHeight + 15` when physics is
      switched off. That is the main suspect for "stuck half in the ground": a settled ragdoll pose must NOT be moved by a capsule-based
      offset. Find out (log the pelvis world Z vs the floor trace before / after the freeze) and fix it so the settled body stays where
      physics left it; if the snap is still needed for the FrostHound root, limit it to that case and to the floor trace, not HalfHeight.
   4. After the freeze, trace down from the pelvis: if the pelvis is more than 20 cm below the floor, lift the whole mesh so the pelvis
      is on the floor (safety net, logged as a warning once per corpse).
B. **Impulse too strong.** Several shooters or one RPG launch bodies. Impulse is applied once at activation (`CorpseSubsystem.cpp:216`),
   DamageFactor goes up to x3.
   1. Lower `impulse_scales.Explosive` and `max_impulse_magnitude` in the JSON (propose numbers in the plan; the speed clamp from A.2 is
      the hard limit, the scale is the feel).
   2. Make sure the impulse is applied only ONCE per death even when several hits land in the same frame (several shooters).
## Do not touch
- Anything Spitter / CryoDrone (they do not exist - leave the JSON overrides alone, do not mention them).
- OperativeCharacterAim.cpp, FireArcRules.*, anything shotgun / mantle (other tasks).
## Allowed paths
- Source/CodexTactics/Private/Characters/CorpseSubsystem.cpp, Source/CodexTactics/Public/Characters/CorpseSubsystem.h
- Source/CodexTactics/Public/Combat/DeathRagdollTypes.h, Source/CodexTactics/Public/Combat/DeathRagdollRules.h,
  Source/CodexTactics/Private/Combat/DeathRagdollRules.cpp
- Content/Data/Combat/death_ragdoll.json
- Source/CodexTacticsTests/Private/Combat/DeathRagdollRulesTest.cpp
- Source/CodexTacticsDev/Private/Debug/DeathRagdollSmokeCommand.cpp
- docs/port/gemini/outbox/067-*
Anything else -> ask in the chat first.
## Steps
1. PLAN commit (`docs/port/gemini/outbox/067-ragdoll-tunnel.plan.md`: steps, files, the numbers you propose, what you found at line
   370) -> chat line with the hash. Claude answers in the chat; if no answer in 20 min, go on with the plan.
2. Pure rules first (speed clamp, pelvis-below-floor correction) in DeathRagdollRules + unit tests.
3. Wire into CorpseSubsystem. 4. Smoke: extend DeathRagdollSmoke with (a) a kill while the target runs at full speed - after settle the
   pelvis is within 20 cm of the floor trace; (b) 3 shooters + 1 explosive on one target - max body speed after the impulse <= the clamp.
5. Gate + report.
## Checks (paste the outputs into the report)
- `powershell -ExecutionPolicy Bypass -File Scripts/build.ps1`
- `powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Filter CodexTactics.Combat.DeathRagdoll`
- `powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands DeathRagdollSmoke,KnockdownSmoke,DeathCamSmoke`
- `powershell -ExecutionPolicy Bypass -File Scripts/Tools/gemini_gate.ps1 -Task 067 -SonnetReview`
## Report (docs/port/gemini/outbox/067-ragdoll-tunnel.report.md, in Russian)
| Claim | Evidence (commit hash / pasted output / file:line) |
|---|---|
Plus: the pelvis Z numbers before / after the fix, the max launch speeds measured, what you could not verify.
