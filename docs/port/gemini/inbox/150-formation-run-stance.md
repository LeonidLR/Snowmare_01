# Gemini 150 (light; pro-subagent review; Lead reviews) - formation: a run order makes crouched followers stand and run (designer 2026-11-26)

Formation mode, followers walking CROUCHED; the player double-clicks = run order: the commander stands and runs, the followers keep
crouch-walking behind (and fall back). Wanted: a run (double-click / sprint) order of the leader in formation switches every follower to
STANDING + run / sprint as well (they keep their slots). When the leader goes back to walk / crouch (next single-click order or C key),
followers return to the stance they had or the leader's stance - check how the formation already copies the leader's stance and use that rule.
Prone followers: stand up and run too. Do not change Solo.
- Pure rule (follower stance + gait from the leader's order) -> unit test. Smoke: formation, all crouched, double-click 20 m ahead ->
  all followers STANDING and speed >= run within 1 s; then a single click -> walk again.
- Branch gemini/150-formation-run-stance from origin/main; FINISH `150 FINISH | ...`. Queue: right after 145, before 135b.
