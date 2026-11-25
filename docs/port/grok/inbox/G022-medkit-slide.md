# G022 - after a medkit the operative SLIDES (no leg animation) - designer bug 2026-11-25, BEFORE G021 (designer is playing now)

Designer, main, /Game/Maps/KitPilot/L_Gen_StoryShowcase_Kit: an operative was wounded, the designer gave him a medkit; the first-aid
Repro 2/2: the designer repeated it on a second operative - same result, every medkit use.
Clue (fight 2): the sliding ends once the operative takes cover against a wall / peeks a corner - that state change resets whatever the medkit left stuck.
animation played, after it he moves without stepping - the capsule slides, locomotion does not play.
Note: the designer has an UNCOMMITTED ABP_Operative.uasset in main - first repro on the COMMITTED ABP (your worktree); if it does not
repro there, report that (then it is his ABP edit, not code) and stop.
Start points: Source/CodexTactics/Private/Characters/OperativeAnimInstanceFirstAid.cpp (Montage_Stop / blend-out ~27, ~78, ~102),
OperativeAnimInstance.cpp (locomotion speed / state inputs), the first-aid entry in OperativeCharacter* (movement mode, root motion,
bIsHealing / injured flags reset). Suspects: a flag that gates locomotion (healing / using-item / injured-pace) not cleared when the
montage ends or blends out; root-motion mode left on; slot weight stuck at 1.
Do: 1) repro smoke (wound -> medkit -> move 5 m; FAIL if speed > 50 cm/s while the locomotion state / ground speed input is idle, or
the first-aid slot weight > 0.1 after the montage end); 2) fix the cause; 3) unit test for the pure part; worker smokes FirstAid*,
the new repro, MovementSmoke. BUILD ONLY ON THE WORKER (Scripts/build.ps1 -Remote worker).
Report docs/port/grok/outbox/G022-medkit-slide.report.md + "G022 FINISH | ..." in chat. Never push, never touch main / maps / .uasset.
