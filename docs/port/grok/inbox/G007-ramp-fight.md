<!-- Reassigned 2026-11-17 evening from Codex (Codex leaving, not started). Grok rules: docs/port/grok/README.md; ignore Codex/Gemini-only lines; audit --agent grok. -->
<!-- Reassigned 2026-11-17 from Gemini 081 (Gemini handed over). Codex rules: docs/port/codex/README.md; worktree CodexTactics-agents; branch codex/043-ramp-fight; ignore Gemini-only lines (gemini_gate, FINISH format). -->
# 081 - Fight on a ramp: nobody fires, then shots do no damage for seconds (designer bug B17-6, 2026-11-17) - MEDIUM, C++
Base: main. Branch gemini/081-ramp-fight. Worktree CodexTactics-gemday (one branch at a time).
Designer (playtest of main 0cc08c4b, L_Gen_Depot_1 ramps / external stairs): "my operatives go DOWN a ramp, an enemy comes UP it -
they do not fire and he does not attack. Then they start shooting / attacking each other but no health is taken. Only after a few
seconds of this forced fight do they fire at them as usual."
Suspects (prove with logs first): the line of sight / fire trace on a slope (the eye or muzzle trace hits the ramp floor or its edge),
a "same floor" / level check by Z (a few dozen cm on a slope reads as another floor), the aim height (FireArc / ProfileHeight) at a target
above or below, the melee reach by Z of the enemy.
Do:
1. PLAN: build a smoke on L_Gen_Depot_1 (a ramp from the LevelGenRampSmoke sites, `Content/Data/LevelJson/L_Gen_Depot_1.json` read only):
   one operative going down, one Frostbitten going up, log per 0.1 s: sees / target / fires / hit actor / damage / blocking component +
   impact Z. Quote the first 5 s.
2. Fix the cause(s) - numbers in the weapon / sight / AI JSON config. Never loosen a smoke limit.
Allowed paths: Sight / FireArc / LineOfFire rules, the enemy melee reach, their configs, the new smoke + test_map.json registry entry, plan +
report. OperativeCharacterAim.cpp / FireGateRules.* / ThreatFacingRules.* are Claude's fresh code: ask in the chat before touching them.
Gate: the new RampFightSmoke 3/3 (the operative damages the enemy within 1.5 s of sight, the enemy's melee hits within its normal reach),
LevelGenRampSmoke, FireGateSmoke, CoverSmoke, EnemyArchetypeSmoke, unit tests, gemini_gate PASS.
