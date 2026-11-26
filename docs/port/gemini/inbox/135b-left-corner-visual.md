# Gemini 135b (Lead reviews) - left corner: the BODY still stands ~1 m from the edge (designer 2026-11-26 15:13, play a12dea10 = 135 merged)

Screenshot: Medic-Sapper at a LEFT wall end, standing, idle in cover after the peek - body ~1 m from the edge.
Play log (CodexTactics-play/Saved/Logs/CodexTactics.log): `corner peek ordered at the left corner (edge -34 cm along the wall ...)`,
`settled in the cover ... (0 cm off the slot)`, cover set 'riffle' (judged indoor), peek stance AS_Cover_StandL_Aim_Idle, then
`[CornerAim] ReturnNoTargets` -> back to the idle cover pose. So the CAPSULE slot is right (34 cm) but the MESH is not: the idle / return pose
of the left side (pack L / 'riffle' set) puts the body back ~70-100 cm along the wall (clip root offset or mesh-on-capsule offset not
compensated outside the lean). CornerSlotSmoke measured the capsule, not the body.
Fix: measure the body (outer shoulder / pelvis bone, world) to the wall end in EVERY corner state on both sides: peek aim idle, return to
idle cover, crouch, both cover sets (outdoor 'm4' and indoor 'riffle'). Target: outer shoulder <= 25 cm from the edge, same left and right,
no clipping into the wall. Extend CornerSlotSmoke to log + assert the bone distance in those states (left and right).
Branch gemini/135b-left-corner-visual from origin/main; FINISH `135b FINISH | ...`. Queue: right after 145, before 141.

## 135b part 2 - body still does not turn to a Shift+click look to the RIGHT (designer 2026-11-26, 2 screenshots, same Medic-Sapper at the left wall end)
Shift+click to his right: the vision cone + look line swing right (to ~90 deg), "Watch sector locked!", but the body / rifle keep facing
along the wall. 135 part B passed in CoverIndoorSmoke on the AimYaw VARIABLE only - the pose does not follow. Check that the AO actually
blends in this state (corner hold, standing, after ReturnNoTargets) and, beyond the AO range, that the actor / body yaw turns (step out of the
wall hug if needed). Smoke must assert the world yaw of the spine_03 / weapon bone vs the look yaw (<= 15 deg), right and left, both cover sets.
