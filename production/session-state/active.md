# Session State — CodexTactics (UE 5.8 port)

<!-- STATUS -->
Epic: Godot → UE port (vertical slice per docs/port/REFERENCE_PLAYTHROUGH_01.md)
Feature: playtest fixes (tremor / facing / stances) + Gemini's TANDEM requests 1-3 (enemy upper-body hits, pause abuse, Marksman enemy)
Task: Unreal is the reference: level JSON at runtime, Wave Editor in Tools/WaveEditor, playtest bot + run telemetry (done); next = balance with the bot (user decision), more parity items from PORT_MATRIX
<!-- /STATUS -->

**The handoff index (state, traps, how to work; system map `docs/systems/`, next steps `docs/port/NEXT_STEPS.md`, change log `docs/port/CHANGELOG.md`) is `docs/port/HANDOFF.md`. Read it first.**

## Mode (user, 2026-09-28)
- Full port with identical behaviour; work autonomously through PORT_MATRIX / docs/port/NEXT_STEPS.md, commit each verified
  system, ask only on real design forks. Godot project is read-only.
- Gemini may take over at any time: keep HANDOFF.md, PORT_MATRIX.md and TANDEM.md current with every commit.
- The user may close / reopen the Unreal Editor for builds (graceful close only).

## Checkpoint 2026-10-25 — formation look sectors: DESIGN AGREED, no code yet (cloud session, branch claude/stoic-bell-dr29o5)

- Read `docs/design/FORMATION_SECTORS.md` first (decisions, code map, order of work); schemes + 3 s clips in
  `docs/design/formation-sectors.html`. Keep the page / clips in sync with every design change (user request).
- Decided: diamond (4) / wedge (3), patrol at ~80 % pace with the rear man walking backwards, triad rear gap stays,
  wall layout = everyone faces the open side (K away from the wall, 4th at the roomier end), auto selection by level
  geometry + hold-G radial menu (centre = Auto), echelon right / left kept, bounding overwatch + door stack dropped.
- Next: Wave Editor -> "Training Ground" (departments: waves / movement / level-design bots); movement simulator in TS
  (0 tokens), analysis via Jev / Claude API on the user's keys (not the agent's session tokens); formation lab maze
  from a layout JSON; then C++ rules with parity tests, runtime, radial menu, in-game smoke.
- The cloud session had no Unreal, no TYPESAFE_API_KEY / ANTHROPIC_API_KEY and api.typesafe.ai was blocked; the user
  moves the work to a local session with the model router.

## Checkpoint 2026-10-22 night — main b14c8ed pushed (morning report)

- Parallel sessions protocol in force (memory parallel-sessions-protocol): weather (CodexTactics-agents2 / env/phase1), ladders spec owner (reviews), my agents in CodexTactics-agents / CodexTactics-ladder. Only I merge into main.
- Today merged: tutorial play-test fixes (pause lesson, mine retry, DEFENSIVE, tripwire card, dead-zombie lessons close), squad stays put after fights, barricade vault both ways, horde intro framing, heat fear needs a clear line, TB arena only on flat open ground, face the enemy after a retreat, gate exit -> fade -> points screen -> 0.1 Frozen Factory (campaign.json, carry-over, NEW GAME = 0.0 Frozen Dam), no end-of-fight button, level-up "+N" badge, weather phases 2-3, ladder design + clips + climbing (nav link, rung snap, C++ hand / foot IK, fall on knockdown), test ladders + closed set-dressing gate on Frozen Factory, blizzard upper-body walk, headless navmesh bake (Scripts/bake_navmesh.ps1; factory baked 126 / 95 tiles).
- Weather phase 4 (falling snow + object snow overlay) running in env/phase1; then landscape snow (steps 1-3) + interactive snow (step 5) - waits for the user's "да" in the weather chat.
- Ladders still open: climbing enemies (FrozenPMC, Military Zombie, Marksman), camera / roof cutaway / minimap on ladders, TB exclusion, turn back mid-climb, DT_LadderTypes + course meshes.
- Review for the user: AGateActor::bSetDressingOpen default false (all set-dressing gates closed); blizzard pose hides the rifle (bHideWeaponInBlizzardWalk).
- Known: FrozenFactoryStartSmoke fade timing is real-time sensitive (3.3 s vs 3.1 tolerance once); test.ps1 -CheckMap lists unregistered dev commands BodyLineupShots / LadderClipShots / MarksmanFeetProbe / EnvironmentShots.

## Checkpoint 2026-10-21 evening — NEXT SESSION START HERE

- main 429b60f is LOCAL ONLY (ahead of origin by 2: the compound quest merge). NOT pushed on purpose: the quest's layout
  script removed the lake map's built navmesh (TRAPS rule for commandlet saves) -> TutorialAmbushLakeSmoke and
  TutorialAmbushLakeLureSpottedSmoke fail until the user opens L_Demo01_A_FrozenLake in the editor, lets the navmesh
  build and saves. Then: commit the map, run all lake smokes, push.
- Open question to the user: investigate why patrol zombies stand still on a RUNTIME-built navmesh (path Moving, speed 0;
  likely the same as the "operative stuck at Overlook_LowCover after a skirmish" report) - real risk for packaged builds.
- Quest designer notes: generator not reachable from the north (shed back wall at the compound wall); HQ line
  "Get the squad inside!" on gate open is wrong here (gate = exit); quest objective banner duplicates the tutorial's;
  A5 / A6 / A9-A10 lessons not written; quest objects are cubes (no art yet).
- Pending from the user: minimap full map north-up rectangle (asked "make the level a straight rectangle");
  prone-death clip for the Marksman (AS_Prone_Aim_Death sinks 12 cm, option: anim_death there too); PMC BP not switched.
- Paused: visual QA via the API (Scripts/Tools/visual_qa.py, calibration 7/13 at $0.10; HUD over the right body causes
  false alarms; key in the user's env ANTHROPIC_API_KEY, never printed).
- Today done + pushed: optimized single-slot bodies switched in BPs (all but PMC), Marksman rifle + retarget, A4 in two
  parts + skirmish mode + SPOTTED pause + wall lessons + Susanin off, cover switch (sprint, face under cursor, coplanar
  buildings), top-down minimap, disarm from cover + trap windows close.

## Checkpoint 2026-10-21 — character single-slot roll-out DONE (main 6225283 pushed)

- From 2026-10-20 the user dropped Gemini: shaders / materials / VFX / perf are ours.
- Pipeline `Scripts/Pipeline/` (docs/pipeline/CHARACTER_SINGLE_SLOT.md): clean -> reduce (Decimate) -> atlas (UVPackmaster 4) -> bake -> export -> review -> `run_unreal_import.ps1` (editor closed, content snapshot guard) -> `OptimizedCharacterSmoke` -> `cleanup_intermediates.ps1`.
- All 11 bodies imported under `/Game/Characters/{Operatives,Enemies}/Optimized/<Asset>/` with every colour set as its own MI: Post_Apo_Survivor_a (3 sets), Female_Soldier, FrozenPMC (default + 5), MilitaryZombie_1-3 (3 each), Brute, Cutter, Marksman, Hound (5), Frostbitten (4, M_CharacterMaster_Subsurface). Budgets: operatives 50k / 4K, enemies 20k / 2K, Hound 10k, Brute 25k.
- NOT done (user's call): switching any BP_* / roster to the optimized meshes.
- Today also: cover corner-to-corner + wall-following (819348f) and hovered = committed fix (38080cd); HasEnemiesWithin counts sight-hidden enemies (c2c1701).
- Incident 2026-10-20: my worktree removal emptied untracked Content/Characters/Mannequins + Content/VFX/SnowTracks through nested junctions; restored (template copy / regenerated). Memory updated (recursive junction scan).
- Open: M_CombatFeedback lacks bUsedWithSkeletalMesh (hit flash may not show in a packaged build) - asked the user whether to fix.

## Checkpoint 2026-10-19 (end of day) — REPORT FOR THE MORNING
Main pushed. Final verification on main: full unity build OK, unity clashes 0, 545/545 unit tests, 12 smokes green
(incl. the new SaveCoverage / SaveCoverageLake).
- Play-test fixes today: anim-first loot / disarm; medkit lesson; every load reopens the level; patrol speed + level
  enemies survive loads; prone-in-cover stands first; mesh pinned to the capsule; occluded vision cones; Aggressive
  really fires; posture lesson; A4 ambush part 2 (lure); symmetric squad sight; peek sight (limited cone from the
  leaned-out eye); role-specific A3 cover rings + the medic finds the mines with his peek cone (layout 5j on the user's
  map); quick fire = Ctrl + click on a point (object / enemy shots unchanged).
- Audit (model router): HANDOFF 681 KB -> 24 KB index + `docs/systems/*.md` + CHANGELOG / TRAPS / OPEN_QUESTIONS /
  NEXT_STEPS / KNOWN_GAPS (Sonnet); `EnemyCharacter.cpp` 2983 -> 291 lines by cluster + TurnBasedCombatSubsystem split
  plan in `docs/port/REFACTOR_ENEMY_2026-10.md` (Opus high); tutorial engine reference + consolidation + steps schema
  validator (Opus); save/load audit — format v3, explicit restore order, state inventory, 13 gaps closed (KIA operatives,
  frostbite flags, cover slot, defence line, random wave events, kill stats, hound scent episode…), corrupt / old /
  missing-map saves handled, saving blocked during dialogue, the self-noise search loop fixed (Fable high); unity-clash
  finder in build.ps1 (+ -FullUnity / -NoUnity).
- Open for the user: GEMINI.md got two link updates (HANDOFF split); CLAUDE.md's description of HANDOFF is outdated
  (not edited — user's file); the observation «after an automatic open shot the return to a high-cover corner resets
  crouch to standing» (gameplay decision); save follow-ups (APC repair in progress, auto-sidearm memory, tutorial
  TriggerHits, objects in flight at save) in `docs/systems/save-load.md`; try one old save slot in the editor.
- Next: Demo 01 patrol fight before the horde (encounter groups) + beats A5-A10; TurnBasedCombatSubsystem split (plan
  ready); wind scent + stealth kill (parked).

## Audit 2026-10-19 evening (user approved, distributed by the model router)
Order / parallelism (two worktrees): (1) L2 Sonnet — split `docs/port/HANDOFF.md` (668 KB) into a short index +
`docs/systems/*.md` + `docs/port/CHANGELOG.md` (agents2, agents/audit); (1') L3 Opus high — split `EnemyCharacter.cpp`
by cluster + a TurnBasedCombatSubsystem split PLAN (agents, agents/work; docs in `docs/port/REFACTOR_ENEMY_2026-10.md`,
not HANDOFF); then (2) L4 Fable — save/load audit end to end (after the enemy split, same files); (3) L3 — tutorial
engine reference doc + dedup. Agents append to the new per-system docs once the HANDOFF split is in main.

## Today 2026-10-19 (play-test fixes)
Agent A (`CodexTactics-agents`, agents/work) — interactions + save/load:
1. Crate: open clip FIRST, then the loot window; Take / Take All -> search clip, items move at the grab moment.
2. Mine / tripwire disarm: bend down first, the trap disappears at the grab moment.
3. Medkit tutorial lesson on the first injury (mine / tripwire / cold / lesson down).
4. Patrol speed set by the designer (65 cm/s) resets to 190 after a checkpoint load (and after a search?).
5. After a load: operatives flee instead of fighting (even AGGRESSIVE); Solo shows ON but the squad moves in formation.
6. After a load: a prone operative ordered to run to cover crawls backwards fast; the mesh stays ~1 m off the capsule
   (ring vs body, clicks / aim off) even during normal locomotion.
7. ROOT: a load must reproduce exactly the saved state — every load path (Retry, F9, Continue, LOAD) = full level
   reload, nothing transient from the pre-load world (repro: knocked down -> load -> he is mid get-up).
Agent B (`CodexTactics-agents2`, agents/cones) — vision cones clipped by obstacles (visibility polygon, low-cover
«standing only» tone, matches SightRules, throttled).
Pending in main: 8bc194c (smoke arenas) built? no — build + push with the next merge. User map edits:
L_Demo01_A_FrozenLake modified (keep, back up before merges).
Then: Demo 01 patrol fight (encounter groups) + A5-A10; wind scent + stealth kill (parked).

## Idea parked 2026-10-18 (user likes it): WIND for hound scent
Hounds smell only operatives UPWIND of them (the wind carries the scent toward the hound); approaching from downwind =
not smelled. A per-level wind direction (level JSON; the lake already has wind zones), shown to the player (a wind
arrow on the HUD / minimap, snow drift direction). Planned together with the stealth kill. Smell rules done first:
walls block scent, smell = sniff alert (not instant detection), radius 12 -> 6 m (agents/smell).

## Queued 2026-10-18 (user decisions), order: bug fix (running) -> stealth lure -> minimap -> Demo 01 patrol fight
- **Melee close-in ON — 2026-10-18 on agents/work** (user approval; `ai_tuning.json` `Codex.Enemy.MeleeCloseIn` 1; pack flanking = a real detour
  since 2026-10-18 (EnemyTacticsSmoke green)).
- **Stealth lure — DONE 2026-10-18 on agents/work** (keys 5 / 6, `docs/design/STEALTH_LURE.md`, `StealthLureSmoke`; to merge after the
  user's look). Original decision:
- **Stealth lure** (before the patrol fight; stealth kill maybe later): any operative can WHISTLE (noise at his own
  position, ~12 m) or THROW a stone / bolt / cartridge (aimed arc like a grenade, noise at the impact point, ~8 m).
  Infinite, per-operative cooldown (no ammo, nothing to pick up). Works ONLY in exploration and only while the squad is
  undetected; disabled once combat starts (useless in turn-based / real-time combat). Enemies that hear it investigate
  the point via the existing perception SEARCH state (~60 s, then back to the route). EVERY enemy inside the lure's
  hearing radius comes (user correction 2026-10-18: e.g. a hound + a marksman together both come; a horde in range =
  all of them come, so luring a horde is pointless). Hounds fast + smell, Brute slow. UI: action-bar
  buttons, hearing circle while aiming, «?» over enemies who heard.
- **Minimap — DONE 2026-10-18 on agents/work** (`docs/design/MINIMAP.md`, `MinimapSmoke`; F2 panel switch, Tab / F3 full map; to merge after the
  user's look; Gemini request #24 for the look / capture cost). Original decision:
- **Minimap** (user decisions 2026-10-18): a RECTANGLE in the top-right corner, in the same panel as the message feed —
  the player switches that panel between MINIMAP and MESSAGES. ISOMETRIC view at roughly the level's starting camera
  angle (very important; not a top-down map); it stays at that starting angle when the player rotates the camera (user
  2026-10-18). Shown PER FLOOR: on the ground the ground level; on the 2nd / 3rd floor
  the map of that floor (geometry above the floor cut away). Fog of war: yes (explored areas only). Full-screen map on
  Tab. Content: squad (selected highlighted), visible / last-known enemies, objectives / beacons, spotted traps,
  buildings, doors; wheel = zoom, click = move the camera, RMB = move order. Perf idea: orthographic iso captures per
  floor once at load + live icons projected with the same matrix.

## Checkpoint 2026-10-15/16 (end of day) — cleanup day DONE
- Cleanup: (1) OperativeCharacter split (10-14); (2) smokes on a fixed 60 fps step, flaky list emptied, arming-countdown
  fix; (3) ONE lake map `/Game/Maps/L_Demo01_A_FrozenLake` (review copy saved over it, copy removed; layout script =
  additive mode only); controller split `CodexTacticsPlayerController.cpp` 2978 -> 446 (`docs/port/REFACTOR_CONTROLLER_2026-10.md`);
  (4) audit batches: `UEnemyRegistrySubsystem` (world tick -12 % with 55 enemies), HUD text cache, playtest bot gated to
  dev builds / Game+PIE worlds, ALL dev/debug commands moved to the new DeveloperTool module `Source/CodexTacticsDev`
  (`Private/Debug/...`; smokes still run the same way).
- Demo 01 rev 5h-5i: spotted trap -> squad stops + Solo; corner peek reveals hidden traps (A3 lesson mines beyond the
  peek corner, spotted ONLY by the peek: `SpotRequiresStep`); roof cutaway (`URoofCutawaySubsystem`, Gemini request #22
  for the dithered fade); tutorial `confine` (hut entry, A3 crouch approach to `VaultRidge_A3`, fence one by one + solo
  cover); SKIP TUTORIAL button + pause menu. Interaction clips (Polygonmaker pack, untracked): crate open / search,
  pickup after a disarm by stance, cold idle variant; disarm walk-up ~70 cm.
- Open / to tell the user: `CLAUDE.md` + `GEMINI.md` still name `Source/CodexTactics/Private/Debug/` for the smoke
  commands — now `Source/CodexTacticsDev/Private/Debug/` (user's / Gemini's files, not edited). CornerHoldSmoke (~1 in 3:
  «flank rush: 2 of 5 down», the commander doesn't fire at enemies 1-2 m away for ~40 s — looks like a gameplay bug, also
  on the old code) and HordeIntroSmoke («planned move runs 0 of 500 cm», failed on the base too) — next to investigate.
  Remaining audit items: target-selection / enemy-decision tick rates (P2/P4), vault probe (P5), tick on demand (P9),
  OperativeCharacter.h split, shared smoke harness, Saved/ cleanup. `HandleWorldHit` helper split (REFACTOR_CONTROLLER §5).
- User questions still open: SAVE GAME in the pause menu, box select after the hut, vision-cone key persistence, patrol
  Panic/TB, APC numbers; EXIT arrow look; peek mine spot (4 mines 2-2.8 m apart); disarm distance 70 cm; loot window
  without the clip in tactical pause; Female body in the Polygonmaker clips.
- Next Demo 01 work: Phase 2 = encounter groups (patrol fight before the horde) + beats A5-A10.

## Checkpoint 2026-10-14 (end of day)
- Done today: Demo 01 rev 5d-5g (heat pack sharing with Shift+drag, barrel into the ruin, corner peek near edge + low
  cover, place dialogue `cancel_on`, traps avoided only in AUTO, tripwire disarm fix, sprint locked until the first heat
  pack, prone dives on the move with root motion, monotonic tutorial progress by location, NO autosaves (checkpoints +
  F5 only) + tutorial save round-trip fixes, orders survive a selection change, followers leave cover to follow, lake
  cold x0.4, instant arrival cards, interior EXIT arrows + M camera toggle); editable squad roster
  `BP_CodexTacticsGameMode` (user switched the Medic-Sapper back to the regular operative); status report
  `docs/port/PROJECT_STATUS_2026-10-14.md`.
- Cleanup day task 1 DONE: `OperativeCharacter.cpp` 4867 -> 306 lines, split into per-cluster files of the same class
  (byte-identical definitions; components = stage 2, plan in `docs/port/REFACTOR_OPERATIVE_2026-10.md` §4, controller
  split proposal §6). Verified on main: build, 449/449 unit tests, 15 smokes PASS.
- Controller split DONE on `agents/work` (2026-10-15, not merged yet): `CodexTacticsPlayerController.cpp` 2978 -> 446 lines,
  9 units by input area, byte-identical definitions; 470/470 unit tests, 46/46 smokes before and after. Map + the
  `HandleWorldHit` break-up plan: `docs/port/REFACTOR_CONTROLLER_2026-10.md`.
- Next (cleanup day continues, effort high for task 2): (2) flaky / failing smokes: PanicSmoke (fails on the base too —
  environment changed), SquadControlSmoke («Alt + V refused while moving», fails on base — likely the new prone-dive
  rule), VisionConeSmoke (1400 cm), CornerHold / TurnBasedAim / Knockdown pass only on rerun; (3) merge the two lake maps
  with the user; (4) audit batches. Open user questions: SAVE GAME in the pause menu, box select after the hut, cone key
  persistence, patrol Panic/TB, APC numbers; EXIT arrow look (graybox), hut doors found = 1.

## Planned: cleanup day (user decision 2026-10-14, after the rev 5g agent)
No new features. (1) Split `OperativeCharacter.cpp` (~4.9k lines) into components (cover / stance + prone dive / sniper),
behaviour unchanged, smokes as the safety net; look at `CodexTacticsPlayerController.cpp` (~2.9k) too. (2) Stabilise the
flaky smokes (CornerHold, Cover, LevelWave, GroupSelect, TurnBlastDeath, EnemyTactics, Knockdown) and fix the
VisionConeSmoke 1400 cm failure. (3) Reconcile the review copy `Maps/Dev/L_Demo01_A_FrozenLake_AutoLayout` with the
user's original into ONE working map (with the user). (4) Audit batches from `docs/port/audit_2026-10-11.md` (enemy
registry, HUD caches, debug module, bot gate). Proposed cadence: one cleanup day every 1-2 weeks.

## Checkpoint 2026-10-13 (end of day)
- Done: Demo 01 tutorial rev 5 / 5b / 5c (user flow 2026-10-13, `docs/demo/DEMO01_BEAT_SHEET.md` decisions block):
  HQ radio runs with the staged camera lesson (+ MMB drag), then click-walk / double-click-run cards; precredit (actions
  done earlier count); dialogue skip (Enter = next line, hold / SKIP = whole talk; TB: SKIP button only); sprint never
  locked, no move lock; progressive command unlocks (untaught commands locked, unlock-all on combat start / tutorial off);
  level `cold_rate_mult` (0.8 on the lake); shaky bridge (`ABridgeCrossingVolume`, no solo lesson) ; low fence
  (`ACrawlGapActor`, prone only); vision-cone toggle O + pause menu; tactical hut entry (select + Solo, stack left/right
  of the door, third enters, then loot); click-to-peek wall corners (`Cover.CornerPeek`, high cover); roof-overhang cover
  fix. Auto layout: `Scripts/Editor/build_demo01_lake_layout.py` (idempotent, tag `AutoLayout_Demo01`) + review copy
  `/Game/Maps/Dev/L_Demo01_A_FrozenLake_AutoLayout` (own level JSON), doc `docs/demo/DEMO01_AUTOLAYOUT.md`.
- User's original `L_Demo01_A_FrozenLake.umap` is modified locally and NOT committed (backups `Saved/Backups/user_wip_20261013_*`).
- Waiting on the user: review the copy (hut door jamb graybox narrows the 4 m door to 2 m; hut camera zone 33 cm past
  the north wall; crate holds 1 heat pack, beat wants 3; keep box select or not); then swap the copy into the original
  (or rerun the script on his map with `CODEX_LAYOUT_TARGET`). Open questions: vision-cone key O / persist; patrol
  Panic / TB.
- Next: Phase 2 = encounter groups (pre-horde patrol fight, beat sheet §1.3) + beats A4-A10 (ambush, trapped crate,
  defence prep, canister quest, horde lessons: pause, AUTO, generator repair + Engineer guard).
- Smoke names need the `Smoke` suffix (`MovementSmoke`), short names silently time out. CornerHoldSmoke can need a rerun under load.

## Checkpoint 2026-10-12 (end of day)
- Done 10-11/12: cover fixes (hug, shimmy, wall clearance), knockdown tuning, sudden horde intro (running-mass trigger,
  flyover, free pause w/o radius), repath throttle, audit (`docs/port/audit_2026-10-11.md`), Sprint 15 heat, vision cones,
  M16 25 m, guard posts, ambush on any map, blast door + beacon, F1 Controls, tutorial core + Demo 01 lake A0-A1 rev 4
  (`docs/demo/DEMO01_BEAT_SHEET.md`, `Content/Data/Tutorial/demo_01_steps.json`, per-map level JSON
  `Content/Data/LevelJson/L_Demo01_A_FrozenLake.json`), weather idle poses + squad gathers at fires, APC P0-P2+P4
  (`docs/vehicles/`; Y/Alt+Y board, drive, turret, run-over, Brute knockdown, hull damage/repair), placed-enemy archetype net.
- User maps committed: `L_Demo01_A_FrozenLake`, `L_VehicleTest`. Studio roles = Gemini Game Studios defs in
  `C:/Users/Zephyrus15Duo/Documents/Codex/.agents/agents/*.json`, run as Claude subagents.
- Next: user builds A1 hut/markers per rev 4 (hut near the crack), then A2 camp (barrel lesson), A3+ per beat sheet;
  APC P3 (turn-based parked turret); open APC P4 confirmations (docs/port/OPEN_QUESTIONS.md); audit batches (enemy registry, HUD caches,
  debug module, bot gate); Gemini requests #15-#21.
- Rules: one agents worktree (+ temp ones removed junctions-first via [IO.Directory]::Delete), back up user files + hash
  check before every merge, beep instead of phone files when the user is at the PC.

## Checkpoint 2026-10-09 (end of day, main 763ca9c pushed)
- Done today: F9 quick load + black load cover (4b31144); Sniper-pack death clips for operatives / Marksman (4c75003);
  Female Soldier = Medic-Sapper body + sniper rifle only for her, fires kneeling / prone only (c2478dd, ac61f69);
  TB facing snap-back, cover-entry pops, kneel reload lift (5816996); TB shots resolve after turn / kneel (763ca9c).
- User WIP NOT committed (do not touch / commit without asking): BP_Operative, BP_Enemy_Marksman, L_MovementTest,
  DefaultEditor.ini, DefaultScalability.ini, ABP_Operative_Rifle2 deletion. Backups: Saved/Backups/user_wip_20261009_*.
  L_PatrolTest is the committed (old) version; the user may restore Saved/Autosaves/.../L_PatrolTest_Auto1.umap themselves.
- Open (ask the user): enemy TB shots with the same turn -> shot order; sniper pose in cover (now M4 cover clips);
  real sniper rifle mesh (DA_Weapon_sniper_rifle.HandMesh); KIA in saves; save-slot playtime; Options screen.
- Rule: back up the user's uncommitted files and compare hashes before every merge into main (memory backup-before-merge-main).

## Last verified
2026-10-05: ABP_Operative_Rifle2 — build OK, `test.ps1 -Smart` 23/0, RifleLocomotion 2/0, setup script OK (6 states / 18 transitions, compile 0 errors, clips 8/16/16/8/6), rendered HudShot rifle2 OK, Jev 88 %.
2026-10-05: Sprint 10 defense line — build OK, `test.ps1 -Smart` 43/0, SquadAutonomy + SquadROE 9/0, smokes Defend / CommanderMode / RealtimeSelect / ClickRules / CameraZone / Tripwire / Sight PASS, Jev ROE 81 %.
2026-10-05: Sprint 09 tripwire — build OK, `test.ps1 -Smart` 4/0, `Interactables.Tripwire.*` 2/0, smokes Tripwire / Inventory / Deployable / Relocation / PrepDeploy / TurnBasedDeploy / EnemyAI / Vault / EnemyTactics / Grenade / Barrel PASS; CombatFlow fixed (4/4).
2026-10-05: Sprint 08 line of sight — build OK, `test.ps1 -Smart` 0 failed, `Combat.Sight.*` 2/0, smokes Sight / SquadFire / LevelWave / WaveCombat / EnemyAI / EnemyHunt / EnemyTactics / Facing / FlankBreach / Cutter / BarricadeContact / Vault / Marksman / MarksmanAdvance / Turret / TargetedShot / CommanderMode / Deployable / Stance / BotMarksman / TurnBased / TurnBasedBarricade / EnemyDeath PASS; CombatFlow fails also with Codex.Sight=0 (free pause 17.5 s left vs 18-20 expected — open, not Sprint 08).
2026-10-05: preparation set-up by the closest running operative — build OK, `test.ps1 -Smart` 4/0, smokes PrepDeploy / Deployable / TurnBasedDeploy / Relocation / Inventory PASS.
2026-10-05: real-time order lock + «АВТО» button + 1-4 camera — build OK, `test.ps1 -Smart` 40/0, smokes RealtimeSelect / ClickRules / ActionBar / GroupSelect / Guard / Inventory / TargetedShot / WeaponSelector / CameraZone / TurnBasedCamera / TurnSelect PASS.
2026-10-05: Sprint 07 Commander Mode — build OK, `test.ps1 -Smart` 40/0 (+ SquadAutonomy 6/0), smokes CommanderMode / Movement PASS.
2026-10-04: Sprint 06-E..H — build OK, `test.ps1 -Smart` 162/0, smokes Relocation / Deployable / TurnBasedDeploy / BarricadeTurnContact / Marksman / MarksmanAdvance / BotMarksman / LevelWave PASS.
2026-10-01: 155 automation tests, all smokes ALL GREEN (`verify_all.ps1`, parallel x3) after the Marksman / enemy hit layer / pause cooldown work.

## Open questions
- none (L_MovementTest re-laid by the user is kept; smokes are layout-relative)
