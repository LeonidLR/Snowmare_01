# Codex 147 (Lead reviews) - sector round: a hiding last enemy = tactical defeat for the PMC ("fled") (designer 2026-11-26)

Designer: several times he killed 2 of 3 PMC, the 3rd ran off; he spends 2-10 min searching the whole map. Tiring. Wanted: if too long without
a firefight and the enemy cannot be found, the PMC gets a TACTICAL DEFEAT (it fled) and the round ends as our win.
Today (Gemini 128, TrainingSectorSubsystem / TrainingSectorRules, L_Gen_Depot_1_X6.sectors.json): no contact 90 s -> PMC relocated to ~40 m,
at most 3 per round; after the 3rd the last enemy can hide forever (play log: "Round 4 pmc_relocated #3/3").
Rule (defaults in the sectors data):
- once at least one PMC is down (the fight happened), no contact (no sight / shot either side) for fled_after_s = 40 s -> the remaining PMC
  are "fled": round ends = our win (tactical: enemy fled), the remaining PMC are removed, message "Противник отступил - тактическая победа"
  (English message key + RU text as the HUD does), round event "pmc_fled" in the recording (count it as a win in TG fights table / A/B).
- if no PMC is down yet (never met): keep the 90 s relocation; after relocate_max_per_round with still no contact -> round ends as a draw
  "no contact" and the next round starts (no endless search either).
- Data keys in sectors.json; pure rules -> unit tests (timers reset on any contact; fled only after a loss; draw after max relocations).
- Smoke on X6: kill 2 of 3, hide the 3rd -> fled win at ~40 s; never-met case -> draw after the relocations.
- Branch codex/147-pmc-fled-defeat from origin/main; report docs/port/codex/outbox/147-pmc-fled-defeat.report.md; FINISH `147 FINISH | ...`.
  Queue: right after 139 (designer plays the sectors now).
