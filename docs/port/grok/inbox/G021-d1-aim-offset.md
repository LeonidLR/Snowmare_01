# G021 - D-1 shotgun fires to the RIGHT of a rushing zombie (designer bug 2026-11-22) - AFTER G017

Branch grok/G021-d1-aim from origin/main, your worktree. Diagnosis (read-only, 11-22, verify line numbers - code moved since):
1. (likely) D-1 is a skeletal weapon mesh -> ApplyWeaponVisual sets HandAsset null (OperativeCharacterSniper.cpp ~211/242), so
   WeaponMesh->GetStaticMesh() is null and every barrel measurement falls back to "barrel = body forward": UpdateCombatFacing
   (OperativeCharacterAim.cpp ~330, BarrelYawOffset eases to 0), GetPoseAimResidualDeg (Aim.cpp ~497), fire gate in cover
   (OperativeCharacterFireGate.cpp ~71). A pose that holds the bore off the body forward -> constant one-side miss.
2. Gate cone 30 deg + RInterpTo 12 -> ~12 deg transient overshoot (secondary).
3. Miss tracers +-140 cm random (Combat.cpp ~1179) - random side, not the bug.
Do:
1. Repro first: extend ShotgunSmokeCommand (or FireGateSmokeCommand): zombie rushes from 10 m, on each OnWeaponFiredNative log the yaw
   between the real bore and muzzle->target; fail > 8 deg. Report the measured offset BEFORE the fix.
2. Fix: GetAimWeaponComponent() (skeletal if it has an asset and is visible, else static); barrel =
   Comp->GetComponentTransform().TransformVectorNoScale(CurrentWeapon->HandMeshMuzzleOffset.GetSafeNormal()) (D-1 bore +Y,
   weapons_tuning.json muzzle_offset [0,4000,0]); use it at the 3 sites. Pure helper + unit tests CodexTactics.Combat.FireGate.*.
3. Prove: the repro smoke PASS + FireGateSmoke, ShotgunSmoke, CoverSmoke on worker slot2. Rifle behaviour unchanged (static mesh path).
Report docs/port/grok/outbox/G021-d1-aim.report.md + "G021 FINISH | ..." in chat. Never push, never touch main / maps / .uasset.
