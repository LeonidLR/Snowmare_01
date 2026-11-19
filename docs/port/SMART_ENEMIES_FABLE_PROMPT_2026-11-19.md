# Smart enemies - GDD + re-plan session (run on FABLE) - start prompt, 2026-11-19

Model: open this session with **Claude Fable 5.2** (`claude-fable-5-2`, the app's model picker). You are the design architect of
CodexTactics (UE 5.8 C++ tactical arctic survival game, Gorky 17 tribute). Reply to the designer in Russian; documents in English.
The designer judged the 2026-11-19 night training results "catastrophic". He wants the whole topic "training bots / smart enemies"
re-thought from requirements, written as a game design document, and the work re-planned end to end. You design and plan; you do not
write gameplay C++ in this session.

## The designer's core requirement (verbatim intent, binding)
PMC fighters, resistance fighters and survivors must move and fight TACTICALLY, exactly like the player's own operatives:
- cross open ground tactically (bounding, cover to cover, not in the open longer than needed);
- take dominant heights when needed (roofs, upper floors, towers);
- hide behind corners and behind cover, peek, lean;
- enter buildings when needed, leave them, jump out of windows when needed;
- create MANY dangerous situations for the player: fire from windows, fire from upper floors, go up to the upper floors when the
  player enters the ground floor, ambush, flank, hold chokepoints, fall back through prepared layers.
Monsters (hounds, zombies, brutes etc.) are part of the same topic but with their own behaviour language (packs, scent, fear of fire /
suppression, horde pressure) - include them in the GDD at the right depth.
HARD RULE: Spitter and CryoDrone do not exist - never mention or plan them.

## Step 1 - read everything (the context budget is large; read fully, not by grep)
- Project rules: CLAUDE.md, docs/port/HANDOFF.md, docs/port/TASK_BOARD.md (done-lists since 11-01), docs/port/TASK_TEMPLATE.md.
- Design: design/gdd/*.md in the Godot archive (C:/Users/Zephyrus15Duo/Documents/Codex/godot-test-01/design/gdd/) for the original
  enemy specs; in this repo docs/design/: PMC_DEFENCE.md, PMC_TRAINING_ANALYSIS.md, TACTICAL_MAP.md, AI_DIRECTOR.md, MANTLING.md,
  LADDERS.md, CLICK_PREVIEW.md, the cover / formation / stealth / first-aid docs you find there, and docs/design/levelgen/ (LEVEL_GENERATOR,
  BUILDING_GENERATOR, BUILDINGS_TECH_SPEC, gdd/*.md - building archetypes, opening states, route model, stories, breach panels,
  S2_ROOMS_PLAN): the buildings the enemies must use.
- The designer's own words: docs/port/reports/designer-tactics-notes-2026-11-18.md (clip notes), memory files in
  C:/Users/Zephyrus15Duo/.claude/projects/C--Users-Zephyrus15Duo-Documents-Unreal-Projects-CodexTactics/memory/ - especially
  pmc-ai-architecture, enemy-ai-intent, solo-primary-play-mode, combat-lab-decisions, arena-clips-readability, narrative-vision,
  weapons-roster-designer-only, designer-decisions-2026-11-17, mantling-decisions, ladder-system-decisions, no-spitter-cryodrone.
- What exists in code (read the headers + the key .cpp): Source/CodexTactics/Public|Private/AI/ (PmcCommanderRules, PmcSquadSubsystem*,
  PmcTacticalRules, PmcArenaBehaviourRules, PmcArenaCommand, PmcEncounter*), SquadOrderCatalog*, Characters/ (OperativeCharacter* -
  the shared body: movement, cover, corner peek, mantle, ladder, drop links, door breach, stealth), EnemyCharacter* + EnemyAIRules
  (monsters), Combat/ (suppression, fire gate, squad fire rules), Tactics/; data Content/Data/AI/*.json (pmc_commander + bounds),
  Content/Data/LevelGen/tactical_runtime.json.
- Training pipeline and results: Scripts/Trainer/ (night_trainer.py, nighttrainer/trainer.py, trainer_config.json), Antigravity
  083 / 083c reports (docs/port/gemini/outbox/083*), docs/port/reports/worker-night-2026-11-18.md, worker-night-2026-11-19.md,
  worker-overwatch-kills.md, docs/port/grok/outbox/G00*-*.report.md (G004 corpse cost, G007 ramp fight, G008 / G008b defence roles and
  overwatch), the open branch grok/G008b-overwatch-hold.
- Ask the designer (once, short) which 1-3 clips looked catastrophic and why; use his answer as the first acceptance example.

## Step 2 - write the GDD
`docs/design/gdd/smart-enemies.md` (studio GDD standard - the `design-system` skill template if it helps): pillars and the player
fantasy of fighting them; enemy factions (PMC, resistance, survivors, monsters) and how they differ; the shared operative body and the
tactical vocabulary (move / cover / corner / height / building entry-exit / window jump / breach / fall back / ambush / flank /
suppress / hold); squad roles and command layer (utility commander, posts from the level generator, encounters, AI director); how
enemies read buildings (stairs, windows, floors, roof access, opening states, breach panels) and the tactical map; the catalogue of
DANGEROUS SITUATIONS the game must generate (each: setup, enemy behaviour, player counterplay, which systems / data it needs);
monsters' behaviour language; difficulty / fairness / readability rules (telegraphing, no aimbot, reaction times, tells); metrics and
acceptance tests (what a smoke / arena round must show for each behaviour); what training is FOR (tuning weights of designed behaviours
vs learning new ones) and when it is allowed to run.

## Step 3 - audit vs the GDD
`docs/design/gdd/smart-enemies-audit.md`: for every GDD behaviour - exists / partial / missing / broken in code, with file:line evidence
and the test that proves it (or that it is missing). Explain plainly why the night training produced bad behaviour (root causes:
missing behaviours, scoring, roles, metrics / fitness rewarding the wrong thing, broken perception / navigation, buildings the AI cannot
use).

## Step 4 - re-plan the whole workflow
`docs/design/gdd/smart-enemies-roadmap.md`: phases in dependency order (foundations first: navigation / building use / cover / firing
positions, then squad tactics, then dangerous-situation scenarios, then training only to tune), each phase with deliverables,
acceptance smokes / arena scenarios, owner type (Claude lead, Codex, Grok, Antigravity, building-generator session), rough size, and
what must NOT start before it. Include the building generator dependencies (enemies need walkable interiors, windows, stairs, roof
access). Then the first 2 weeks as concrete tasks written in docs/port/TASK_TEMPLATE.md form (design gate, evidence, task, never-list,
gate, allowed paths), ready to dispatch.

## Rules
- Budgets: the shared weekly "all models" limit was at 96 % on 11-19 morning (team stop 98 %, weekly reset 11-21 09:00 UTC); Fable has
  its own weekly allowance (32 % used) but the all-models cap can still stop the session - check get_usage, save your documents early and
  often (commit + push each document as it is ready: `$env:CODEX_AGENT="claude"`, Conventional Commits, docs only).
- Do not change code, smokes, maps or the trainer; do not start runs. Do not dispatch tasks yourself - the lead session dispatches
  from your roadmap after the designer approves it.
- Argue: give the designer real alternatives with costs where a decision is his (memory argue-dont-agree), mark them as open questions
  in the GDD, never invent decisions he has not made.
