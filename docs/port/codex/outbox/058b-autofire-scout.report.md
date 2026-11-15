# 058b automatic cover-fire scout
Code-only, no logs/builds/smokes/implementation changes; final main HEAD a3d3f51a (advanced during inspection).
Aliases: C = Source/CodexTactics/Private/Characters/; T = Source/CodexTactics/Private/Tactics/; D = Source/CodexTacticsDev/Private/Debug/.

## Automatic firing paths / missing decision
- C/OperativeCharacterCombat.cpp:584 ProcessCombatShooting -> :591 RunCombatShooting; :692 caches MayExposeNow. Quick-fire and ordered suppression branch out separately (:698,703).
- :745-746 selects auto-fire/auto-target eligibility; :789 calls FindShootTarget, then :839 only steps out when bMayExpose AND !IsCornerShotTarget. If suppressed, this test is skipped: no sector refusal replaces it.
- :865-894 aligns actor to wall, handles corner hold/duck/snap; this checks facing relative to cover, not whether the target is reachable from that cover pose. :910 BeginCoverShot -> :914 blind guard -> :918 ShootAtTarget is the shared ordinary firing sink.
- C/OperativeCharacterCover.cpp:612-629 BeginCoverShot has no target argument/sector validation: suppressed or BlindFire mode sets blind flags/hold timer; otherwise starts lean/corner aim. It is a pose side-effect function, not a target policy gate.
- C/OperativeCharacterCombat.cpp:1074-1085 applies suppression/blind accuracy factors; changing spread cannot fix shooting outside the cover pose's sector.

## Target selection and blind variants
- C/OperativeCharacterCombat.cpp:389 FindShootTarget: manual priority :395-405; explicit silhouette blind order :414-435; locked target :454-456; ordinary candidate evaluation :472-474; current/nearest retention and flank switches :499-543. No DecideCoverFireAtPoint filter found there.
- When visible candidates are empty, :490-494 EvaluateReturnFire can return a blind muzzle-flash candidate; EvaluateReturnFire is :303-333. A common sink gate must also check Shot.AimPoint for blind candidates, not the actor's current location.
- FindBestCombatTarget (:339-369), CanHitEnemy (:382-386) and EvaluateShotLine (:101) are shared line/target queries. Avoid inserting suppression-specific side effects or broad behaviour changes there in the first fix.
- Other BeginCoverShot callers: C/OperativeCharacterQuickFire.cpp:235 (explicit quick fire), OperativeCharacterSuppression.cpp:353 (ordered suppression), OperativeCharacterTargetedShots.cpp:131 (special targeted shots), OperativeCharacterCover.cpp:984 PlayCoverShot (grid presentation). These are distinct from the ordinary auto-fire sink; do not claim one gate fixes every caller.

## Lowest-risk recommendation
- Replace the old decision at Combat.cpp:836-845 with DecideCoverFireAtPoint(AimAt, bMayExpose), computing the existing AimAt expression first (Shot.AimPoint when blind, otherwise Target location). Keep the existing open-shot/aim-cone/cover-alignment/hold/cooldown checks after this policy gate.
- CoverPose: continue existing blind/lean logic. StepOut: use existing BeginOpenShotFromCover(Target) and its wait result; do not fire until the normal open aim-cone test succeeds. Refuse: return a clear fire-decision reason before BeginCoverShot/ShootAtTarget; stay in cover, spend no ammo, do not auto-expose.
- This conservative Refuse behaviour may temporarily hold fire on a retained out-of-sector target despite another reachable target. It has less risk than immediately changing target selection or issuing movement while suppressed.
- Optional second part: filter only ordinary auto candidates before Visible.Add (:472-474) when DecideCoverFireAtPoint says Refuse, allowing an in-sector alternative. Revalidate at the firing sink regardless; cover state/suppression can change. Preserve manual priority/locked target semantics and gate return-fire fallback too.
- Policy source: C/OperativeCharacterCornerAim.cpp:311-320 delegates corner/low-cover geometry; T/CoverFacingRules.cpp:163-176 returns CoverPose when pose reaches, otherwise StepOut if exposure allowed, Refuse if not. It also limits over-the-top fire to FrontalArcDeg/2, missing from the old IsCornerShotTarget-only auto decision.
- Ordered precedent: C/OperativeCharacterSuppression.cpp:163-164 rejects unreachable order; :313-327 switches policy, steps out or cancels Refuse. Auto-fire should wait rather than copy CancelSuppressFire/message side effects. QuickFire.cpp:201 uses StepOut with exposure explicitly true; it is not the suppressed-auto case.

## Coverage / missing regression
- D/SuppressionSmokeCommand.cpp:888-992 stages 61-65: side target 70-110 degrees, ordered SUPPRESS steps out before shot (:938-941), suppressed order refused/no round (:945-971), frontal over-box ordered fire still works (:979-992). This covers ordered suppression, not idle auto acquisition at the side.
- D/CornerHoldSmokeCommand.cpp:1036-1039 checks open-side stream; :1167-1168 flank rush in open stance; :1183 every shot's real barrel stays within cone. Useful auto-fire/step-out regression; not a dedicated suppressed side-target refusal assertion.
- D/CoverSmokeCommand.cpp:569-571 toggles blind mode; :836-868 corner shots/sustained aim; :1050 no corner lean/blind shots in open-side case. Useful cover regression, not proof of the missing suppressed-auto policy case.
- Source/CodexTacticsTests/Private/Tactics/CoverFacingTest.cpp:606-620 tests low-cover frontal and high-corner policy, including exposure disallowed; pure rule tests do not prove auto-fire calls it.
- Add a focused automatic case without OrderSuppress/quick/manual target: suppressed at box + visible side target => no rounds/ammo loss, remains in cover; frontal target => blind auto-fire allowed; suppression lifted + side target => step out and align before first shot. Add alternative-target case only if filtering is implemented.

## Risks / open questions
No runtime failure reproduced. Confirm whether Refuse should preserve the current combat target for facing/threat memory (recommended first fix) or immediately select an alternative; keep cover holding, rage, return-fire flash and manual-order precedence unchanged unless separately specified.
