# 080 - Enemies (marksmen above all) jitter left-right on seeing the squad (designer bug B17-5, 2026-11-17) - MEDIUM, C++
Base: your 077 branch's newest commit after 077 is DONE (same PMC code; do NOT start before 077's FINISH). Branch gemini/080-enemy-jitter.
Worktree CodexTactics-gemday.
Designer (playtest of main 0cc08c4b): "some enemies, the marksmen above all, on seeing my operatives start to vibrate hard - microscopic
turns right-left, left-right, as if they got stuck and keep turning."
Do:
1. PLAN: reproduce (PmcEncounterSmoke / a marksman fixture; log the actor yaw per frame for 3 s after first sight - a jitter = sign
   changes of the yaw step > N per second). Find WHO sets the rotation each frame (target pick switching between two targets, cover facing
   vs. aim facing, movement facing vs. aim, controller focus vs. SetActorRotation, AI focus + anim turn-in-place) - quote file:line.
2. Fix the cause (one owner of the facing per frame, a hysteresis / minimum hold time on a target switch, a dead band) - numbers in the
   enemy / PMC JSON config. Add a smoke check: yaw sign flips per second under a limit for a marksman and a PMC rifleman that see the squad.
Allowed paths: Source/CodexTactics/**/Pmc*, Source/CodexTactics/**/Enemy*.{h,cpp} (NOT EnemyAnimInstance / ABPs - ask), their JSON configs,
tests, a smoke under Source/CodexTacticsDev/Private/Debug/ (new file or Pmc*Smoke checks), plan + report.
Gate: the new check 3/3, PmcStalemateSmoke, PmcEncounterSmoke, EnemyArchetypeSmoke, EnemyPrioritySmoke, unit tests, gemini_gate PASS.
