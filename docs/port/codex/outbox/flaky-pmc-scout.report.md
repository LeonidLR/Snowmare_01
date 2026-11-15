# PmcStalemateSmoke: code-only flaky scout
Scope: main 418b3f1d; no logs, builds, editor or smoke execution. Suspects are hypotheses, not reproduced causes.
Paths below are repository-relative; Arena = Source/CodexTacticsDev/Private/Debug/PmcArenaCommand.cpp.

## Smoke thresholds
- Arena:2848-2851: objective round, 3 fighters per side, 180 s limit; Arena:107,2659-2660: harness advances in fixed 0.1 s steps.
- Arena:2192-2199: shots between sides required; longest quiet spell <=45 s; attackers reach <=1.5 * configured objective radius (converted to metres), or are eliminated; early order preempts = 0.
- Arena:2201-2207: orders/fighter/min <=60/max(1, WeightsA.OrderMinHoldSeconds); repeated identical orders within 2 s <=max(2*Squad, ceil(2*Squad*Duration/60)).

## Top 3 suspects, ranked
1. Shared global random stream in combat and cosmetics.
   Evidence: Source/CodexTactics/Private/Characters/OperativeCharacterCombat.cpp:1008,1111,1127,1154 uses FMath::FRand/FRandRange for misfire, pellet hits, visual offsets and critical hits; OperativeCharacterWeapons.cpp:284,396 uses global RNG for callouts.
   Why: a fixed initial seed does not isolate gameplay draws from differing cosmetic/event consumption; changed draw counts/order can alter casualties and quiet spells. No divergence reproduced here.
   Fix: give each fighter/round a seeded gameplay FRandomStream and separate cosmetic RNG; risk: med (changes combat random sequences).
2. Order-dependent equal-distance/score decisions without an explicit stable tie-break.
   Evidence: Source/CodexTactics/Private/AI/PmcSquadSubsystem.cpp:538 iterates Teams; :1132-1140 chooses the first nearest Known target on equal distance; :1854 StableSort compares score only.
   Why: differing registration/contact/candidate insertion order can propagate into targeting/order selection. TMap iteration alone does not prove variation between identical runs.
   Fix: sort relevant inputs by stable scenario fighter/target IDs and add deterministic distance/score tie-breaks; risk: med (changes tactical choices on ties).
3. Setup waits for elapsed harness time rather than verified navigation readiness (conditional).
   Evidence: Arena:2669-2682 waits only StageTime >=2 before setup; :647-657 accepts seeded samples through ProjectPointToNavigation; seeded streams at :692,773,900,1046.
   Why: if nav generation is still active, the same sampled coordinates can yield different accepted locations. No evidence established that this map rebuilds navigation during the smoke.
   Fix: gate setup on navigation/level readiness and record selected spawn coordinates/nav state for comparison; risk: low for instrumentation, med for a readiness gate that can time out.

## Counter-findings
- Arena:2391,2405 spawns A then B with explicit seeds; site/spawn samplers use FRandomStream. No global FMath random or real-time clock call found in the arena harness or AI/Pmc*.cpp search.
- PMC path queries inspected are synchronous (PmcSquadSubsystem.cpp:1429,1866,1901); world GetTimeSeconds is simulation time, not evidence of a wall-clock dependency. Recording ticker is separate from combat stepping.

## Open questions
- Which exact assertion fails, and what is the first divergent event across identical seed/map/options runs? Compare spawn positions, orders and combat RNG draw counts before changing thresholds.
- Is runtime nav generation active during setup, and are initial nav/actor-registration states identical? The fixed 2 s wait alone does not answer this.
- Are ForceHit/critical overrides enabled in the failing invocation? No such override was found in this arena path; no runtime evidence was collected.
