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
