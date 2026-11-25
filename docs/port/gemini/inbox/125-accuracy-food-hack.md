# Gemini 125 (Hydrogen) - (A) measure operative vs PMC accuracy, (B) food must not cure critical wounds (designer 2026-11-25) - after 124

Branch gemini/125-accuracy-food, worktree C:/Work/Lanes/CodexTactics-125. BUILD ONLY ON THE WORKER.
A) MEASURE ONLY, no balance change: designer feels "PMC hit me much more often than I hit them". 1) Compare the data side by side:
   operative vs PMC weapon spread / aim time / accuracy modifiers (weapons_tuning.json, pmc_operators.json, GameBalanceConfig C++ defaults,
   stance / moving / cover modifiers, suppression effects) - one table. 2) Measure: hit % by side and distance bucket (0-10/10-25/25-50/50+ m)
   on X6 sector fights on the worker (3v3, AI vs AI with the same seeds, 20 rounds) - if the round JSON lacks per-shot data, add a counter
   (shots / hits per side per bucket) to the round stats. Report the numbers + which modifiers explain a gap. Lead + designer decide changes.
B) FOOD HACK: a heavily wounded operative cannot treat himself with a medkit (by design), but eating chocolate / canned food heals him -
   that bypasses the wound system. Fix: food never clears or reduces a critical / heavy wound state and never heals HP while critically
   wounded (food keeps its warmth / stamina / small HP effect only for light wounds). Find the food use path (consumables / personal items,
   CON_* data) and the wound state (critical wounds GDD / code). Unit tests for the rule; FirstAid / Survival smokes still PASS.
Checklist #1104; stage files by path. Report docs/port/gemini/outbox/125-accuracy-food.report.md + "125 FINISH | ...". Never push.
