# G025 - look-fix / corner peek polish: position, aim offset both ways, torso drift (designer 2026-11-25) - after G026 and G022

Designer: "the look-fix (Shift-click) mechanic is one of the best we made - polish it with animation." Three parts (X6 fight 11-25):
1. CORNER POSITION BUG: at a wall corner ("at the corner - covering it") the Commander sinks INTO the wall and stands well to the RIGHT of
   the corner edge. Expected: shoulder at the corner edge, body on the face side, no capsule / mesh penetration, both hands (L/R), stand
   and crouch. Find the corner slot placement (CornerPeek / corner hold, cover slot offsets, WallHug, mesh offset in cover); fix the cause.
   Smoke: corner slots on X6 blocks, both hands, both stances -> <= 25 cm from the edge, penetration <= 2 cm.
2. LOOK BOTH WAYS WITH AIM OFFSET: a Shift-click look at a corner only turns him one way. Expected: the torso rotates toward the look point
   through an aim offset; when the look direction crosses to the other side of the corner he switches to the other hand's pose. Use the
   CoverIndoor pack AOs: Content/Animations/CoverIndoor/UE4/AO_CoverIndoor_{StandL,StandR,CrouchL,CrouchR,CrouchL_Over,CrouchR_Over}
   (operatives; .../Pmc/ for PMC). C++: L/R side choice from look direction vs corner normal with hysteresis, AO yaw / pitch inputs on the
   operative AnimInstance, clamped to the AO range; ABP_Operative graph: blend the matching AO in the corner state. The designer COMMITTED
   his ABP_Operative (main 74f1d128) and OK'd an agent editing it - edit it in your branch, keep his changes, list every graph change.
3. TORSO DRIFT ("gimbal"): after several position changes, a climb and look changes, the Commander walks SIDEWAYS - even with a neutral
   look (Shift-click on himself) he walks straight but the body stays half-turned. Some yaw state is not reset: look-lock yaw, aim / barrel
   yaw offset (UpdateCombatFacing BarrelYawOffset), AO yaw input, mesh relative rotation after a mantle / cover exit, controller desired
   rotation. Find it; every exit path (neutral look, mantle end, cover exit, order change) returns the torso to the move direction within
   0.5 s. Smoke: sequence cover -> climb -> look L -> look R -> neutral -> walk 10 m: mesh-forward vs velocity yaw <= 10 deg after 0.5 s.
Tests: unit tests for side choice / yaw clamp / reset rule; screenshots both sides stand + crouch. CoverIndoorSmoke, FormationWallHugSmoke,
MantleSmoke paths PASS. BUILD ONLY ON THE WORKER, foreground, PROGRESS lines. Branch grok/G025-look-polish from origin/main.
Report docs/port/grok/outbox/G025-look-polish.report.md + "G025 FINISH | ...". Never push.

CLARIFIED BY THE DESIGNER (11-25, read this as the spec for part 2):
- A plain click on the corner-peek silhouette -> the CURRENT pose (he peeks straight ahead), unchanged.
- AFTER that, Shift-clicks that move the look point -> switch to the CoverIndoor pack animation and drive its aim offset with BOTH yaw and
  PITCH: right-up, right-down, left-up, left-down are different poses (the AO blend space axes). Look at a window above -> torso / arms
  aim up; at the ground nearby -> down. Back to the plain pose only on a new plain click / leaving the corner.
4. FORMATION SCAN PITCH: while the squad moves in formation the operatives scan only left-right (yaw). Add vertical scanning: when a scan
   target is a window / roof edge / higher floor in range, the head / torso aim offset also pitches up (and down for a lower level), within
   the AO range; plain ground scans stay level. Find the formation scan code (sector / wall scan look targets) and the AnimInstance AO pitch
   input. Smoke or a scripted check: formation walk past an X6 building -> recorded AO pitch > +10 deg at least once toward its upper
   windows, ~0 on open ground. Screenshot in the report.
