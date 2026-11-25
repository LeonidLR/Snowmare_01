# Codex 121 - session recording misses what the trainer needs (designer's first recorded sector fight 2026-11-25)

Designer's decisions in Saved/Demos/2026-11-25_L_Gen_Depot_1_X6_094811.jsonl (sector 1): (1) at first fire contact fell back behind
cover; (2) took the high ground - one operative climbed a roof (C_1 max z 992); (3) one operative drew fire while a second flanked the
PMC in STEALTH and killed an enemy that nobody covered. The recording shows orders / snaps only: no kill / death / damage events
(footer rounds 0, ABORTED), no stealth / posture flag beyond 'st', orders tagged "reposition" / "explore_move" instead of the intent,
no line-of-sight / under-fire facts per operative. The trainer cannot learn these three skills from it.
Do (Source/CodexTactics recording code, SessionRecordingRules / DemoRecord - read first; keep the format additive, v stays readable):
1. events: shot_hit / damage (who, whom, dmg, dist), kill / down (killer, victim, weapon, dist, was_victim_aware, victim_covered_by_ally),
   stealth_on/off per operative, cover_enter/leave, climb (roof / ladder / mantle with height).
2. snap per op: stealth flag, in_cover, height above ground, seen_by_enemy count, under_fire (s since last incoming shot).
3. offline tagger Scripts/Tools/demo_tactics.py: from a .jsonl -> a short table of detected tactics with times: fallback_to_cover_after_contact
   (cover within N s of first contact), high_ground (op >= 1 storey above the nearest enemy while engaging), distract_and_flank (ally A
   under fire / firing while ally B in stealth moves > 90 deg around the target and gets the kill), plus kill list. Thresholds as constants.
   Run it on the 11-25 demo (old format: what can be detected) and on a new short PIE recording (new format: all three must show).
4. unit tests for the pure tagger rules; the recording smoke(s) still PASS. BUILD ON THE WORKER. Branch codex/121-recording-tactics.
Report + "121 FINISH | ..." in chat. Never push.
