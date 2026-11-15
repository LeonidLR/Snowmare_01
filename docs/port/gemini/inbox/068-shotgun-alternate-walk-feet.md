# 068 - Shotgun alternate walk: feet slide on the ABP legs (Claude's unfinished WIP, 2026-11-15)
Суть: два бойца с дробовиком идут по-разному (не близнецы), и ни у одного ноги не скользят по снегу.
Реестр: weapon-anim-layer
Base: origin/wip/2026-11-15-pause (526aad44, NOT built by its author until tonight); branch gemini/068-shotgun-alternate;
worktree: CodexTactics-interim (`git switch -c gemini/068-shotgun-alternate origin/wip/2026-11-15-pause` AFTER 067 is committed).
Note: this base also carries 30 mantle .uasset copies + setup_mantle_animation.py - do not touch them.

## What the WIP does
Designer: two shotgunners must not walk like twins. Rank 0 / a lone carrier walks on the M16 ABP legs, rank 1 on the shotgun pack walk
(`weapon_anim_sets.json` legs_abp_walk "alternate"; `WeaponAnimLayerRules::AlternateWalkUsesAbpLegs`;
`OperativeAnimInstanceShotgun.cpp`; unit test `CodexTactics.Animation.WeaponLayer.AlternateWalk` passes).
## What fails (Claude ran it 2026-11-15 19:01, ShotgunSmoke, fixed step 60 FPS)
```
Smoke ok  : 2b: walking ... lower body follows the pack (full-body set) (max change 158.81) ...
Smoke FAIL: 2b: walking, no foot sliding: the planted foot drifts 136.2 cm/s on the ground (78 % of his 174 cm/s; limit 35 %, the M16 (ABP) legs)
Smoke FAIL: 2b: sprinting ... lower body follows the pack (full-body set) (max change 0.00) ...
Smoke FAIL: 2b: sprinting, no foot sliding: the planted foot drifts 371.7 cm/s (57 % of his 649 cm/s; limit 35 %, the M16 (ABP) legs)
```
ShotgunCoverSmoke PASS. Note the sprint lower-body change 0.00 = legs frozen while sprinting.
## Goal
1. Find WHY (diagnose first, write it in the plan): is the smoke's carrier rank 0 so it now gets ABP legs while the check still expects
   the pack? are the ABP legs driven with the wrong speed / no locomotion state while the shotgun layer is on? is the lower-body blend
   mask wrong for the alternate so the legs pose freezes at sprint?
2. Fix the cause in the anim instance / rules / data - NOT by raising the 35 % limit. Sprint must animate the legs for both ranks.
3. Update ShotgunSmoke 2b so it checks BOTH carriers (rank 0 = ABP legs, rank 1 = pack), each with the no-slide limit 35 %.
## Allowed paths
- Source/CodexTactics/Private/Characters/OperativeAnimInstanceShotgun.cpp, Source/CodexTactics/Public/Characters/OperativeAnimInstance.h
- Source/CodexTactics/Private/Characters/WeaponAnimLayerRules.cpp, Source/CodexTactics/Public/Characters/WeaponAnimLayerRules.h
- Content/Data/Weapons/weapon_anim_sets.json
- Source/CodexTacticsTests/Private/Characters/WeaponAnimLayerTest.cpp
- Source/CodexTacticsDev/Private/Debug/ShotgunSmokeCommand.cpp
- docs/port/gemini/outbox/068-*
No .uasset / ABP edits: if the fix needs the Animation Blueprint, STOP and write exactly what must change in the report (the designer
does ABPs).
## Steps
1. PLAN commit with the diagnosis (log lines that prove it) -> chat line. If no answer in 20 min, go on.
2. Fix + tests. 3. Smoke. 4. Gate + report.
## Checks (paste outputs)
- build.ps1; `test.ps1 -Filter CodexTactics.Animation.WeaponLayer`
- `smoke_parallel.ps1 -Commands ShotgunSmoke,ShotgunCoverSmoke,PistolSmoke,WeaponSelectorSmoke`
- FormationJitterSmoke fails on main too (known, D-1 follower wobble) - run it, report it, do not fix it here.
- `gemini_gate.ps1 -Task 068 -SonnetReview`
## Report (docs/port/gemini/outbox/068-shotgun-alternate.report.md, in Russian)
| Claim | Evidence |
|---|---|
Plus: drift % for each carrier walk / jog / sprint before and after.
