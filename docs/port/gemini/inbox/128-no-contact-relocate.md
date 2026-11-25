# Gemini 128 - sector rounds: no contact for too long -> move the PMC squad to ~40 m (designer 2026-11-25) - after 127

Designer: "I wandered the sector for several minutes and could not find anyone to fight." Round loop (G019 + your 122:
TrainingSectorSubsystem / TrainingSectorRules, L_Gen_Depot_1_X6.sectors.json).
Rule: if neither side has SEEN the other (no perception contact, no shot fired) for no_contact_relocate_s (data, default 90 s) since the
round start or since the last contact, relocate the living PMC squad (teleport; no new units, keep HP / ammo / rockets-fired) to a point:
- inside the active sector box, on navmesh, on the ground (not on props / roofs - reuse your 122 ground check);
- at relocate_distance_m from the player squad centroid (data, default 40, accept 35..45);
- preferably WITHOUT direct line of sight to any player operative (no face-to-face surprise), next to cover if possible; if no such point,
  the best one by distance; log what was picked;
- never closer than min_start_post_distance_m to a player; the PMC then gets its normal engage / patrol-toward order so contact follows.
At most relocate_max_per_round (data, default 3). Record a round event "pmc_relocated" (time, distance, LOS yes/no) in the session recording.
Unit tests for the point choice (distance band, LOS preference, fallback); SectorRoundsSmoke option: force no contact -> relocation after
the delay, distance 35..45 m, no LOS at the moment of the teleport. Router first; Neon review of the Hydrogen diff before FINISH. BUILD ONLY ON THE WORKER.
Report docs/port/gemini/outbox/128-no-contact-relocate.report.md + "128 FINISH | ...". Never push.
