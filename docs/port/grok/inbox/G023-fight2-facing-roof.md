# G023 - designer's 2nd sector fight (X6 sector 2, 2026-11-25): facing + roof firing - after G022 and Codex 120

1. ROOF CANNOT SHOOT: an operative who climbed to the high ground (roof) could not fire at the PMC at all - only grenades worked.
   Find why (fire gate / LOS trace from the roof hits the parapet or the roof edge; aim pitch limit; cover-state from the roof; target
   "not visible" from that height; eye height vs parapet). Fix at the cause; repro smoke: operative on an X6 roof, PMC 20-40 m away on the
   ground in plain view -> he fires and hits (and still does NOT shoot through a full wall).
2. GRENADE THROW FACING: he throws backhand to the side, not toward the enemy. Before the throw montage the operative turns (smoothly,
   fast) to the throw direction (target point); same for the PMC thrower (G014). Smoke: thrower yaw vs throw direction <= 20 deg at release.
3. FIRST AID FACING: while treating a wounded ally the medic sometimes faces away. He must face the wounded (yaw to the patient's chest
   <= 30 deg for the whole montage), incl. when the patient is down behind / beside him. Related to G022 (same first-aid code) - fix both
   cleanly together if it is the same place.
Recording to look at: the newest Saved/Demos/2026-11-25_L_Gen_Depot_1_X6_*.jsonl (sector 2).
Unit tests for any pure rule; smokes for 1-3 on the worker; FirstAid*, Grenade*, FireGate*, CoverSmoke still PASS. BUILD ONLY ON THE WORKER.
Branch grok/G023-fight2. Report docs/port/grok/outbox/G023-fight2.report.md + "G023 FINISH | ..." in chat. Never push, never touch main / maps / .uasset.
