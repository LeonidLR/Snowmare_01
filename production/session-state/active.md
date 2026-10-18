# Session State — CodexTactics (UE 5.8 port)

<!-- STATUS -->
Epic: Godot → UE port (vertical slice per docs/port/REFERENCE_PLAYTHROUGH_01.md)
Feature: playtest fixes (tremor / facing / stances) + Gemini's TANDEM requests 1-3 (enemy upper-body hits, pause abuse, Marksman enemy)
Task: Unreal is the reference: level JSON at runtime, Wave Editor in Tools/WaveEditor, playtest bot + run telemetry (done); next = balance with the bot (user decision), more parity items from PORT_MATRIX
<!-- /STATUS -->

**The full handoff (state, system map, traps, next steps, change log) is `docs/port/HANDOFF.md`. Read it first.**

## Mode (user, 2026-09-28)
- Full port with identical behaviour; work autonomously through PORT_MATRIX / HANDOFF §8, commit each verified
  system, ask only on real design forks. Godot project is read-only.
- Gemini may take over at any time: keep HANDOFF.md, PORT_MATRIX.md and TANDEM.md current with every commit.
- The user may close / reopen the Unreal Editor for builds (graceful close only).

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
  APC P3 (turn-based parked turret); open APC P4 confirmations (HANDOFF §7); audit batches (enemy registry, HUD caches,
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
