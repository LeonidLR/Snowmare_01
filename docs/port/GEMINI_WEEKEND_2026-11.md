# Gemini weekend plan (2026-11-06 .. 2026-11-09)

Written by Claude (the orchestrator) for Gemini working through Google Antigravity while Claude is offline.
The user approved this plan. **Do only the tasks below.** Anything else you notice goes into your report as a note, not into code.

## 0. You have been away for a long time - catch up first

Your last active period on this project ended around 2026-10-20 (the user decision then: Claude + the user only). Since then
the project changed a lot: squad formations and wall work, ladders, falls / drop links, a level generator with the generated
map `L_Gen_Depot_1`, vision from a height, the PMC enemy squads with a utility-AI commander, a self-play arena and a night
trainer. Roles changed too: **Claude now owns all code, shaders, materials, VFX and performance; Claude is the only one who
merges into `main`.** You work on a branch in your own worktree and hand it back.

Before touching anything read, in this order (skim, don't memorise):
1. `CLAUDE.md` (project rules, build / test commands).
2. `docs/port/HANDOFF.md` (state, system map, traps) and `docs/port/TRAPS.md`.
3. `docs/port/CHANGELOG.md` - the rows since 2026-10-20 (newest first).
4. Per task: the docs named in the task.
`docs/port/TANDEM.md` describes the OLD two-agent ownership; it is outdated for ownership (see above) - ignore its ownership table.

## 1. Rules (hard)

- **Own worktree + branch**, never work in the main checkout:
  `powershell -ExecutionPolicy Bypass -File Scripts/Tools/new_agent_worktree.ps1 -Name gemini -Branch gemini/<task-slug> -From origin/main`
  (one worktree for all your tasks; one branch per task: `git switch -c gemini/<slug> origin/main` inside it). Disk is nearly
  full - one worktree only. Content packs in a worktree are JUNCTIONS to main's folders: never `git add -A`, never delete or
  save into `Content/<pack>` folders; add files explicitly.
- **Never merge into `main`, never push `main`.** Push only your `gemini/*` branches.
- **Never touch:** `Content/Maps/L_MovementTest.umap`, `Content/Maps/Generated/L_Gen_Depot_1.umap` (the user has uncommitted
  hand edits), `L_Demo01_B_FrozenFactory.umap`, `Config/DefaultEditor.ini`, `Config/DefaultScalability.ini`,
  `Content/Data/AI/pmc_commander.json` (+ `.bounds.json`), anything under `Scripts/Trainer/` and `Saved/Trainer/` (the night
  trainer may be running from the main checkout - do not start it, do not run game processes from the main checkout).
- The Godot repo `C:/Users/Zephyrus15Duo/Documents/Codex/godot-test-01` is read-only.
- Code / docs / commit messages / in-game text in **English**. Epic coding standard (see CLAUDE.md). Conventional commits.
- Never skip git hooks. Never print API keys.
- Before handing a C++ / TS task back: `Scripts/build.ps1` green, `Scripts/test.ps1` 0 failed (or `-Filter` + `-Smart` while
  iterating), the task's smokes via
  `powershell -Command "& Scripts/smoke_parallel.ps1 -Commands <FullSmokeName>,... -Jobs 2 -ExtraArgs '-Multiprocess'"`
  (full names from `Scripts/test_map.json`, e.g. `SquadControlSmoke`; short names silently time out).
  In PowerShell quote `"-LevelJson=X.json"` (PowerShell 5.1 splits an unquoted argument at the dot).
- One report per task in `docs/port/gemini_reports/<slug>.md`: what changed (files), how you tested (commands + results),
  open questions. Keep it short. Then the next task.
- If a task turns out bigger or riskier than described, **stop it**, write the report with what you found, go to the next.

## 2. Tasks (in this order)

### T1. Training Ground - the Waves tab shows the real levels (+ the horde threshold field)
Area: `Tools/TrainingGround/` (React + TypeScript, `npm test`, `npm run typecheck`, `npm run golden -- --check`, dev server
`npm run dev` on :5173). No C++.
- Today the level dropdown in the Waves tab (`src/App.tsx`, ~lines 505-522) is HARDCODED to the old Godot 12-battle list
  (`stage_01..12`: "Бой у КПП", "Складской терминал", ...). The game does not use those for its current levels.
- Build the dropdown from the real data: `Content/Data/Campaign/campaign.json` (campaign order and player-facing titles) plus
  every `Content/Data/LevelJson/*.json` (group: Campaign / Labs & tests / Generated / Legacy stage_XX), showing
  `level_name`. Loading / saving must use the selected file through the existing dev-server save endpoint.
- Give the generated map a proper title: `Content/Data/LevelJson/L_Gen_Depot_1.json` `level_name` -> `"0.1 Depot"`
  (decision: the Depot replaces Frozen Factory as level 2 - do NOT change `campaign.json` itself, Claude does that later).
- In the horde section of each level add the field **"Horde after N firefights"** stored as the level-JSON key
  `horde_after_firefights` (integer, default 6, min 1). Add it to `Content/Data/Schemas/level_config.schema.json`.
  (The game-side rule that reads it is Claude's task later - only the data + UI now.)
- Tests for the list building and the new field. Done = the user opens the tab and sees the Lake, the Depot, the labs with
  their real names and can edit + save the Depot's horde threshold.

### T2. Design doc - blizzard and cold vs sight and accuracy (no code)
Write `docs/design/WEATHER_COLD_COMBAT.md`. The user's rule (2026-11-03): in a **blizzard the sight range is halved** (the
vision cone half as long - they walk shielding their faces, can't spot distant enemies); **accuracy drops in a blizzard and
drops further with strong cold** (stacked: a frostbitten man in a blizzard hits very rarely). Deliver:
- a table weather state (see `docs/design/` / `Content/Data/` weather data and `environment-weather` docs) x cold level
  (the cold component's levels / frostbite) -> sight multiplier, accuracy multiplier (and optionally reaction / spotting time);
- how it combines with the existing **height sight bonus** (x(1 + 0.25 x level), cap x2; `docs/systems/sight-and-vision-cones.md`)
  and with stance / suppression accuracy (find the weapon accuracy code path and name it);
- enemies: armed humans (PMC, marksmen) use the same model; monsters - propose;
- data layout (which JSON, keys, defaults), the smokes / unit tests that would prove it, and a balance sanity check with
  numbers (e.g. hit chance at 20 m / 40 m for: warm+clear, warm+blizzard, frostbitten+blizzard).
Mark every number "proposal - pending the user".
**Update 2026-11-04 (the user):** the sight RANGE part is being implemented by Claude now with these rules - a clear day =
the maximum (at least 2x the old 60 m; the cone covers practically everything visible), NIGHT cuts it drastically regardless
of the weather, snowfall / blizzard / fog reduce it, enemies use the same model. In your doc treat the range numbers as
"implemented by Claude, see `docs/systems/sight-and-vision-cones.md`" and focus on ACCURACY (weather x cold x stance /
suppression), spotting / reaction time and the balance tables.

### T3. Design doc - tactical doorway breach (no code)
Write `docs/design/DOORWAY_BREACH.md`. The user's request: hovering the cursor over a doorway shows ghost positions: one man at
the LEFT jamb, one at the RIGHT jamb (both against the wall, already peeking round the jamb and aiming inside), the third in
the MIDDLE in front of the door a bit back, aiming in. This is an extra option next to the existing wall stack at a door
(which stays). Cover: how the player chooses it (cursor over the opening vs the wall, or a second click like the cover cycle),
who goes where (nearest-slot assignment), solo vs formation vs box-selected group (keep these three modes clearly separate -
the user's direction), the follow-up "enter / clear" step (order of entry, sectors inside), edge cases (narrow doors, doors
on roofs / upper floors, enemies inside), the ghosts / UI, and which existing systems to reuse (read
`docs/systems/squad-and-orders.md`, `docs/systems/cover-and-peek.md`, `docs/design/FORMATION_SECTORS.md`).

### T4. Narrative bible - first draft (no code)
Write `docs/design/NARRATIVE_BIBLE.md` from the user's vision (2026-11-02):
- Four comrades: Andrey Sallivanov "Sally", Pavel Yatsina "Roger", Marat Babaev "Babai", Sergey Roninov "Ronin".
- Twenty years ago Ronin betrayed his team to rebels; they were wounded and captured. He massacred a village for diamonds and
  founded the PMC "Ronin". Twenty years later the three survivors, with PTSD, are hired by a state rapid-response unit (ГБР)
  to eliminate him.
- Prologue: P1 alone at a summer camp (tutorial); P2 the summer camp, all four; P3 the four in rain, Africa or South America -
  the operation where the betrayal happens (formations, ambushes, a fight, a defence, a tactical pause, driving an APC). Then
  the present: the Lake in winter with three (the current first level `L_Demo01_A_FrozenLake`), then the Depot.
- The game stays arctic; rain / sun only on narrative levels the user names (P3 is one).
- Open questions to list (don't decide): roles of each character (the squad roles today: Commander, Engineer, Medic-Sapper),
  the instructor at the camp, Africa vs South America, whether the vertical slice = P3.
Deliver: logline, characters (bio, voice, wounds, skills that map to game roles), timeline, level-by-level outline, themes,
tone (Gorky 17 tribute: dry, tense, cold), and the open questions. 3-6 pages.

### T5. PMC jacket material - tint and snow parameters
Area: `/Game/Characters/Enemies/PMC/Operator/` (Claude's assets, committed). The three instances `MI_PmcOperator_Grey`,
`MI_PmcOperator_Dark`, `MI_PmcOperator_Snowed` are no-ops because the parent material comes from the user's untracked pack
(`Content/Cyberpunk_Explorer_A`, a junction - **never edit or save anything in that pack**).
- Create our own master `M_PmcOperator_Body` in `/Game/Characters/Enemies/PMC/Operator/Materials/` that takes the pack's
  textures as parameters (copy references, don't copy files) and adds: `Tint` (vector, multiplies base colour on the cloth via a
  mask - the pack's material ID / colour mask if any, else a luminance-based approximation), `SnowAmount` (scalar 0..1: a
  world-up-facing snow layer on the jacket / shoulders / headgear, white, rougher), `Grime` (scalar).
- Re-parent the three MIs to it with visibly different settings (grey / dark / snowed). Do it headless with editor Python
  (`UnrealEditor-Cmd ... -run=pythonscript -script=...`), script saved under `Scripts/Editor/setup_pmc_body_material.py`.
- Check: `CodexTactics.PmcTeamSmoke` and `CodexTactics.PmcArenaSmoke` still pass; a `-RenderOffscreen` screenshot of the three
  variants side by side in `Saved/Screenshots/PmcMaterials/` (never capture the desktop).

### T6. Solo -> formation switch clears the box selection (small C++)
User bug 2026-11-03: in SOLO mode he box-selects all three, moves them, then presses **B** (formation). The multi-selection
stays, so every order is still a group order and the formation is effectively ignored ("sometimes it works, sometimes not").
Wanted: switching solo -> formation clears the box / group selection and selects the commander (the squad leader), so the
formation works. Formation -> solo: keep today's behaviour. Find the B toggle in `Source/CodexTactics/Private/Core/`
(player controller) / `USquadSubsystem`. Add a unit test if the rule is pure, and a smoke (or extend `SquadControlSmoke`)
asserting: box-select 3 in solo -> B -> selection == {leader}. Run SquadControlSmoke, SoloRejoinSmoke, CombatFormationSmoke,
GroupCornerPeekSmoke, WallLeadershipSmoke.

### T7. CANCELLED (2026-11-04)
The cone / sight-range work moved to Claude together with a new sight-range model (see T2). **Do not touch**
`VisionConeComponent`, `TacticalSightSubsystem`, `SightRules` or the sight data this weekend.

### T8 (optional, only after T1-T7 are handed back). Learning Agents - an isolated feasibility spike
The user wants the PMC fighters to become very smart opponents. Today they are trained by the **night trainer** (an evolutionary
search over the utility-commander weights in the self-play arena - `docs/systems/pmc-squad-ai.md`, `docs/tools/night-trainer.md`;
don't run or change it). The next step could be Epic's **Learning Agents** plugin (reinforcement / imitation learning).
- Read the plugin's source in the engine (`C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/**/LearningAgents*`) and write
  `docs/design/LEARNING_AGENTS_SPIKE.md`: the 5.8 API (manager, interactor observations / actions, policy, critic, PPO trainer,
  imitation trainer + recorder, the Python / PyTorch training process and how it is launched), what it needs on this machine,
  how it would sit on top of OUR seams (the shared order catalog `SquadOrderCatalog` = the action space, the commander's
  considerations = the observations, the arena `CodexTactics.PmcArena` = the training environment, the user's recorded play =
  imitation data), a narrow first skill to learn (e.g. "pick the next cover position" for one PMC fighter), the risks, and an
  estimate.
- Optional prototype ONLY inside a new self-contained plugin `Plugins/CodexLearningSpike/` (its own `.uplugin` with
  `"EnabledByDefault": false` and the LearningAgents dependency; no change to `CodexTactics.uproject`, to the game modules or to
  existing maps; a new test map inside the plugin's Content if needed). If it doesn't build cleanly within the plugin, stop and
  report - the document is the deliverable.

### T9 (any time, READ-ONLY). Analyse the night trainer's results
The night trainer runs by itself from Windows Task Scheduler every night 22:15-08:00 (run id `pmc-main`). Don't start, stop,
pause or configure it; don't edit anything under `Scripts/Trainer/`, `Saved/Trainer/` or `Content/Data/AI/`.
- Read `docs/tools/night-trainer.md` and `docs/systems/pmc-squad-ai.md`, then the run's outputs (read-only):
  `Saved/Trainer/pmc-main/report.html`, `leaderboard.json`, `champion_weights.json` vs `Content/Data/AI/pmc_commander.json`,
  `generation.json` files, the highlight clips / 2D track replays.
- Write `docs/design/PMC_TRAINING_ANALYSIS.md`: how fitness evolves, which weights drift and in which direction (what that
  means tactically), what the fights look like (draw rate, time-limit draws, stuck events, friendly fire, flanks), behaviours
  that look dumb in the clips / tracks, and concrete recommendations for Claude (fitness terms, bounds, arena scenarios,
  commander bugs). Recommendations only - no code, no config changes.

## 3. Out-of-plan work is discarded

Claude reviews every `gemini/*` branch file by file against this plan. Changes outside a task's area (refactors, "cleanups",
renames, reformatting, new features, edits to other systems, data or docs not asked for) are **rejected and not merged**, even
if they look useful - put such ideas in your report instead. The allowed areas: `Tools/TrainingGround/`, `docs/`,
`Content/Data/LevelJson/L_Gen_Depot_1.json`, `Content/Data/Schemas/level_config.schema.json`,
`Content/Characters/Enemies/PMC/Operator/`, `Scripts/Editor/setup_pmc_body_material.py`, `Scripts/test_map.json`,
`Source/CodexTactics/{Public,Private}/{Core,Characters,Combat}/` (T6 / T7 only, minimal), `Source/CodexTacticsDev/Private/Debug/`,
`Source/CodexTacticsTests/`, `Plugins/CodexLearningSpike/` (T8 only). The main checkout's state was snapshotted before your
session; any change there is detected.

## 4. Hand-back
When done (or at the end of the weekend): push your `gemini/*` branches, make sure every task has its report in
`docs/port/gemini_reports/`, and write a one-page summary `docs/port/gemini_reports/SUMMARY.md` (task -> branch -> status ->
how verified -> open questions). Claude reviews, runs the full bar and merges on Monday.
