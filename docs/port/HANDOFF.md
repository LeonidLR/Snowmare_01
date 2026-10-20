# HANDOFF — CodexTactics (UE 5.8 port of Operation: Cold Silence)

**Purpose.** The short entry point for any agent (Claude, Gemini, ...): how to work in this repo, the traps, the current state and a map to the
per-system docs. Detail lives in `docs/systems/*.md`, the history in `docs/port/CHANGELOG.md`. (Until 2026-10-19 this file held all of it, 668 KB;
it was split verbatim, nothing was dropped - old `HANDOFF §N` references resolve through the table in section 6 and in `docs/systems/README.md`.)

Last update: 2026-10-19 (split into an index + per-system docs + CHANGELOG; the per-system content is current to main `2a6561c`).

**Project status report (non-technical, for planning): `docs/port/PROJECT_STATUS_2026-10-14.md`.**

**State (2026-10-19).** The Unreal port is the reference version (the Godot repo is a frozen archive). Playable: the exploration -> preparation -> wave combat
(real time / tactical pause / turn-based) loop, squad cover and peek, stealth lure and hound scent, cold and tactical heat, loot / traps / turrets / APC,
CommonUI frontend with save / load, minimap, and the Demo 01 tutorial on `/Game/Maps/L_Demo01_A_FrozenLake` (one lake map since 2026-10-15; rev 5j).
Last recorded test run (CHANGELOG head, quick fire): 529 unit tests green; all smokes are registered in `Scripts/test_map.json`.
Latest work (2026-10-19): quick fire (Ctrl + click on a point), peek sight, symmetric sight while exploring, occluded vision cones, load = reopen the level,
unity-clash finder in `build.ps1`. 2026-10-20 (tutorial, `agents/work`): one current navigation target (edge arrow + minimap highlight, guide
beacon; places behind never call back - the lake bridge marker bug), the A4 ambush by the Medic-Sapper alone, the A3 fence without the
one-at-a-time queue, the pacing pass (breather 0.8 s, `Tutorial.Flow.Pacing`) - `docs/systems/tutorial-engine.md` sections 17-18, 554 unit tests.
2026-10-20 (`agents/pmcfighter`): the Frozen PMC Fighter (`/Game/Characters/Enemies/PMC/FrozenPMC_Fighter/BP_Enemy_FrozenPMC_Fighter`, a Frostbitten variant on the user's Cyberpunk_Explorer_A body with a retargeted copy of the Frostbitten ABP; the pack mesh has a stray FBX root bone - see `docs/systems/animation-setup.md` "Frozen PMC Fighter").
2026-10-20 (`agents/coverswitch`): the wall-edge cover switch - in cover at a wall edge, a double-click on another cover within 8 m dashes there and arrives through the M4 pack's `std_jog_fwd[_L|_R]_to_cvr_*` run-in clip (`docs/systems/cover-and-peek.md` "Wall-edge cover switch").
2026-10-20 (`agents/work`): corner-to-corner switch (the nearest corner of the other object, mirrored pose) and the wall-following round a cover object (click along the same object's outline -> round its corners in cover) - `docs/systems/cover-and-peek.md` "Corner to corner" / "Wall-following round a cover object".
What is waiting for the user: section 7 (open questions) and `docs/port/NEXT_STEPS.md`.

---

## 1. Read first (in this order)

1. This file.
2. `CLAUDE.md` — project rules (Godot is read-only, no GAS, commit per verified system, ask only on real design forks).
3. `docs/port/PORT_MATRIX.md` — Godot file → UE class table with status and tests; intentional deviations.
4. `docs/port/REFERENCE_PLAYTHROUGH_01.md` — the user's reference video, the order the port follows.
5. `docs/port/TANDEM.md` — coordination log between agents (claim files before editing, one builder at a time).
6. `docs/port/ARCHITECT_SUPERVISOR_DIRECTIVE.md` — Gemini's directive; §4 lists where Godot code wins over it.
7. `docs/systems/README.md` - the per-system docs (the old system map); start with the doc of the system you touch.

Godot source of truth: `C:/Users/Zephyrus15Duo/Documents/Codex/godot-test-01/` (READ ONLY — never edit).
The user's rule: **Godot code values win** (except explicit user decisions listed in PORT_MATRIX "deviations").

## 2. Environment facts and traps (read before touching anything)

One line per trap; the full rows (with consequences and exact commands) are in [TRAPS.md](TRAPS.md).

- **Paths / junction:** engine `C:/Program Files/Epic Games/UE_5.8`; `Documents/Codex/unreal` is a junction to this repo, so an agent "in Codex/unreal" edits the same files - claim work in TANDEM, build one at a time. [TRAPS.md](TRAPS.md)
- **Editor locks the DLLs:** close the Unreal Editor before `build.ps1` (graceful `CloseMainWindow`, wait 45 s; never force-kill a window with a save dialog). [TRAPS.md](TRAPS.md)
- **Build log is UTF-16-ish:** read `Saved/Logs/Build.log` with PowerShell `Get-Content`, not grep. [TRAPS.md](TRAPS.md)
- **UHT / MSVC names:** no members shadowing `AActor` (`Role`, `Instigator` -> `SquadRole`, `InstigatorName`); C4458 is an error. [TRAPS.md](TRAPS.md)
- **Commandlet output:** `unreal.log()` in `-run=pythonscript` does not reach stdout - write results to `Saved/Logs/*.txt`; editing an inherited component via Python CDO needs `set_editor_property` on the template. [TRAPS.md](TRAPS.md)
- **Git LFS** for `.uasset/.umap`, never mark them `lockable`. [TRAPS.md](TRAPS.md)
- **L_MovementTest was re-laid by the user** (rotated 180 deg, moved): smokes write design coordinates and map them with `SmokeUtils::LevelPoint` / `LayoutTransform`; never hard-code world coordinates. [TRAPS.md](TRAPS.md)
- **Empty navmesh at runtime:** a saved rotated / unaligned `RecastNavMesh-Default` stays empty; maps ship without one (RuntimeGeneration=Dynamic) and editor scripts destroy it before saving. "leader on navmesh=0" in a smoke -> check this first. [TRAPS.md](TRAPS.md)
- **Unity builds merge .cpp files** (adaptive unity builds changed files alone: a branch builds, its merge into main fails): every file-local name of a .cpp must be unique in its module, `#undef` your `#define`s, shared helpers go to an `…Internal.h`; check with `python Scripts/Tools/find_unity_clashes.py`, proof builds `build.ps1 -FullUnity` / `-NoUnity`. [TRAPS.md](TRAPS.md)
- **No emoji in default fonts:** the HUD / menus strip them (`StripUnsupportedGlyphs`); texts stay verbatim. [TRAPS.md](TRAPS.md)
- **CommonUI input config:** keep `bEnableDefaultInputConfig=False`; screens are pooled - reset per-show state in `NativeOnActivated`, not the constructor. [TRAPS.md](TRAPS.md), [dialogue-frontend-mission](../systems/dialogue-frontend-mission.md)
- **Long Python in bash heredocs breaks:** write it to the scratchpad with the file tool and run `python <file>`. [TRAPS.md](TRAPS.md)
- **Smokes run on a fixed 60 fps step** (`smoke_parallel.ps1`): wait on CONDITIONS with generous caps, never on fixed seconds; `CustomTimeDilation = 0` does not stop an enemy's `TickBehavior` (also `SetActorTickEnabled(false)`, `SmokeUtils::ParkEnemyFarAway`). [TRAPS.md](TRAPS.md), [build-test-smokes](../systems/build-test-smokes.md)
- **`AOperativeCharacter` = one class, 16 .cpp files:** put new code in its cluster file, shared helpers in `OperativeCharacterInternal.h`. [TRAPS.md](TRAPS.md), [stances-and-movement](../systems/stances-and-movement.md), `REFACTOR_OPERATIVE_2026-10.md`
- **`ACodexTacticsPlayerController` = one class, 10 .cpp units:** new key / click handler in the unit of its input area, shared helpers in `CodexTacticsPlayerControllerInternal.h`, each unit keeps and `#undef`s `LOCTEXT_NAMESPACE`. [TRAPS.md](TRAPS.md), [squad-and-orders](../systems/squad-and-orders.md), `REFACTOR_CONTROLLER_2026-10.md`
- **`UPlaytestBotSubsystem` only in development builds and Game / PIE worlds:** null-check `GetSubsystem<UPlaytestBotSubsystem>()`; in Test / Shipping `-CodexBot` and `Codex.Bot.*` do not exist. [TRAPS.md](TRAPS.md), [tools-bot-waveeditor](../systems/tools-bot-waveeditor.md)
- **Dev console commands (smokes, probes, shots) live in module `CodexTacticsDev`:** link against `CodexTactics`, so runtime symbols they call need `CODEXTACTICS_API` and runtime `Private/` headers are not includable; `test.ps1 -CheckMap` scans the folder. [TRAPS.md](TRAPS.md)
- **Mesh on the capsule:** the skeletal mesh may move relative to the capsule only by the tracked cover offsets; `EnforceMeshBaseOffset` puts anything else back every frame (smokes: `GetMeshBaseOffsetCm`, `ResetBodyOffsets`). [TRAPS.md](TRAPS.md), [cover-and-peek](../systems/cover-and-peek.md)
- **Holding a montage on its last frame:** clear `bEnableAutoBlendOut` on the running instance, not on the montage asset. [TRAPS.md](TRAPS.md), [animation-setup](../systems/animation-setup.md)
- **Blend spaces made by script** need `ValidateSampleData` + `ResampleData`, else the player outputs the reference pose (T-pose) with no error. [TRAPS.md](TRAPS.md), [animation-setup](../systems/animation-setup.md)
- **Two agents, one checkout:** set `$env:CODEX_AGENT` (claude / gemini); every build / test / smoke / verify_all takes `Saved/agent.lock` (`Scripts/agent_lock.ps1 -Status`). [TRAPS.md](TRAPS.md), `TANDEM.md`

## 3. How to work, build, test, verify

**Worktrees and the lock.** Parallel work happens in a `git worktree` on an `agents/<topic>` branch (TANDEM protocol 8: its own `Binaries` / `Intermediate` /
`Saved/agent.lock`; untracked asset packs and `DerivedDataCache` are directory junctions to the main folder, read-only use; tracked user-edited assets are the committed
versions there). Never edit another agent's worktree or the main checkout from yours; the lead merges into `main` once the user's editor is closed, after build + tests
pass. One builder at a time: `$env:CODEX_AGENT = "claude"` (or `"gemini"`), `Scripts/agent_lock.ps1 -Status` shows the holder; builds refuse while the user's editor is open.

```
powershell -ExecutionPolicy Bypass -File Scripts/build.ps1                    # [-FullUnity | -NoUnity]; runs the unity-clash finder first (yellow WARNING per hit)
python Scripts/Tools/find_unity_clashes.py                                   # the clash check alone (exit 1 on a clash)
powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 -Changed [-Quick]   # impact map Scripts/test_map.json decides which unit tests + smokes run (-Smart = -Changed)
powershell -ExecutionPolicy Bypass -File Scripts/test.ps1 [-Filter CodexTactics.Cold]
powershell -ExecutionPolicy Bypass -File Scripts/smoke.ps1 -Command CodexTactics.DeployableSmoke
powershell -ExecutionPolicy Bypass -File Scripts/verify_all.ps1              # build + all tests + all smokes (nightly / milestone, editor closed)
```

- **Verification tiers.** edit: `build.ps1` then `test.ps1 -Changed -Quick`; pre-commit: `test.ps1 -Changed` + `test.ps1` (all unit tests) +
  `python Scripts/Tools/typesafe_triage.py --audit-diff --agent claude`; nightly / big merges: `verify_all.ps1`. Add a rule to `test_map.json` whenever you add a smoke or a system
  (`test.ps1 -CheckMap` fails on an unmapped `Debug/*Smoke*Command.cpp`).
- **The `Smoke` suffix rule.** Smoke command names are the FULL names (`MovementSmoke`, `CodexTactics.DeployableSmoke`); `smoke_parallel -Commands` with a short name
  (`Movement`) silently times out.
- **Fixed-step note.** `smoke_parallel.ps1` runs every smoke with `-UseFixedTimeStep -FPS=60 -FixedSeed` (each frame = exactly 1/60 s whatever the load; per-smoke
  `smokeOptions` in `test_map.json`: `realTime`, `fixedStepFps`, `timeout`). `smoke.ps1` still runs on the wall clock: add `-Extra "-UseFixedTimeStep -FPS=60 -FixedSeed"` to
  reproduce a batch result. A PASS* in the batch summary = passed only on the re-run (suspect flakiness).
- **Unity-clash check.** See the trap above; `build.ps1` runs it before every build.
- **Ownership with Gemini (TANDEM).** Claude: gameplay C++, logic, AI, UI, animation setup, data, tests, Wave Editor. Gemini (Lead Architect / Technical Director): ADRs, shaders /
  materials, VFX, rendering / scalability, profiling; he formulates specs in `docs/port/TANDEM.md`. Cross-boundary changes go through TANDEM «Requests»; claim `.uasset`s in
  «In progress» (they cannot be merged). TypeSafe (Jev) tooling: `Scripts/Tools/typesafe_triage.py` - use it for the pre-commit audit and telemetry digests, not for C++ syntax.
- **Save rules (user files).** Before ANY merge in `main`, back up the user's uncommitted files (copy them and verify the hashes) - the user edits maps, packs and Blueprints
  in the main folder. Commit only your own files. Conventional Commits, body references the phase / Godot file, `Co-Authored-By` trailer; commit + push to `main` after
  build and tests pass (no PRs / branches unless asked).
- **Never touch:** `Content/Maps/L_MovementTest.umap` (the user's layout is the baseline: never reset / checkout / regenerate it), `Config/DefaultEditor.ini` and
  `Config/DefaultInput.ini` (local editor changes, never committed), `Config/DefaultScalability.ini` and `Content/VFX` (Gemini's), the imported packs (Combat_Dog, RifleAnims,
  Post_Apo_Survivor, monsters, Mannequins, `90+_Weather_Animations`, ...), and the Godot repo `Documents/Codex/godot-test-01` (read-only archive).
- **Docs with every commit.** The matching `docs/systems/*.md`, a row in `docs/port/CHANGELOG.md`, `PORT_MATRIX.md` where a row changes, and the TANDEM log. Keep this file short.

Full build / test reference, Unreal MCP notes and the table of every smoke command: [build-test-smokes](../systems/build-test-smokes.md).

## 4. Conventions

- C++ first, Blueprints for look / tuning only. **Characters are Blueprints the user owns** (mesh, AnimBP, collision):
  C++ exposes EditDefaultsOnly properties + BlueprintImplementableEvents, never hardcodes visuals a BP cannot override.
- Each class header names its Godot reference file / functions. Epic coding standard (A/U/F/E/I, TObjectPtr, IWYU).
- Player-facing texts: **verbatim from Godot** (Russian, emoji included) in `LOCTEXT`.
- Colours from Godot are sRGB: use `FLinearColor::FromSRGBColor(FColor(...))`.
- Units: Godot metres × 100 = cm; Godot `+Y up` → UE `+Z up`.
- Commits: Conventional Commits, body references the Godot file(s), `Co-Authored-By` trailer. Only verified work.
- After each system: update PORT_MATRIX row, TANDEM log, this file (§5, §8, §10).
- Since the 2026-10-19 split of this file: the system detail goes into the matching `docs/systems/*.md` (not here), the change-log row into `docs/port/CHANGELOG.md` (old §10), open questions into `docs/port/OPEN_QUESTIONS.md` (old §7); keep this index short.

## 5. System map (UE -> Godot): the per-system docs

One doc per system in [docs/systems/](../systems/README.md); each starts with a 3-5 line summary and the key files, followed by the old text verbatim.

| Doc | What is in it |
|---|---|
| [squad-and-orders](../systems/squad-and-orders.md) | GameMode squad spawn, input (PlayerController), orders / who owns an operative's movement, formation, fire postures, per-member Blueprints. |
| [stances-and-movement](../systems/stances-and-movement.md) | AOperativeCharacter overview: stances / capsule per stance, sprint, vault, carrying, inventory limits, source layout of the 16 cluster files. |
| [cover-and-peek](../systems/cover-and-peek.md) | Cover slots / enter / leave / shimmy, followers leaving cover, the group cover order (others beside / prone behind), corner peek that reveals traps (physically, one by one), return to cover after a knockdown. |
| [combat-and-fire](../systems/combat-and-fire.md) | Targeted shots, quick fire (Ctrl + click), fire postures / ROE, combat feedback, knockdown values, M16 range, turn-based combat manager and grid. |
| [sight-and-vision-cones](../systems/sight-and-vision-cones.md) | Squad sight (symmetric, also while exploring), peek sight from the leaned-out eye, occluded vision cones, how to switch cones on. |
| [enemy-ai-and-perception](../systems/enemy-ai-and-perception.md) | Enemy registry (no actor iterators in hot paths), pack flanking detour; parity notes for enemy AI live in KNOWN_GAPS. |
| [stealth-lure-and-scent](../systems/stealth-lure-and-scent.md) | Whistle / stone lures, enemy investigation and re-target rules, hearing round walls, hound scent (sniff, follow, detect), tutorial locks. |
| [survival-cold-heat](../systems/survival-cold-heat.md) | Cold accumulation / tiers, mobile and tactical heat (campfire, stove, flares, heat packs, fire puddle), weather idle poses and the squad gathering at the fire. |
| [interactables-traps-loot](../systems/interactables-traps-loot.md) | Interaction menus, barrels, relocation, deployables (barricade / mine / turret), loot crates, generator, quest chain and gate, spotted-trap Solo, PMC tripwire how-to. |
| [waves-horde-encounters](../systems/waves-horde-encounters.md) | Phases (Exploration -> Preparation -> WaveCombat -> ...), sudden horde start, horde intro flyover, level enemies / guard posts / ambush start, wave data. |
| [tutorial-engine](../systems/tutorial-engine.md) | **The steps JSON schema reference** (every key, default, precedence, save behaviour, examples), command choke point and locks, state machine / overlay, demo flags, how to place volumes / markers / steps; revision history below it. |
| [demo01-lake](../systems/demo01-lake.md) | Per-revision decision blocks of the Demo 01 tutorial level (rev 4 .. rev 5i, 2026-10-19), what the designer places in the lake map, lessons (posture, medkit, A4 ambush). |
| [save-load](../systems/save-load.md) | JSON save slots (format v3), save policy (outside combat / dialogue), load = reopen the level, the 11-step restore order, the state inventory (saved / derived / gaps), how to add saved state, robustness (corrupt files, missing map). |
| [camera-hud-minimap](../systems/camera-hud-minimap.md) | Tactical camera and camera zones, roof cutaway, building exits, minimap / full map, HUD, phase banners, action bar. |
| [dialogue-frontend-mission](../systems/dialogue-frontend-mission.md) | Dialogue sequences, CommonUI frontend (title, main menu, pause menu, save slots), mission start / mission rules, narrative pipeline. |
| [vehicles](../systems/vehicles.md) | Kinematic nav-driven APC: boarding / seats, driving, cover, turret and cannon, hull damage / repair, run-over decisions, how to place it and make L_VehicleTest. |
| [animation-setup](../systems/animation-setup.md) | AnimInstance, left-hand IK and aim offsets (what the user adds to the graph), grenade / knockdown / hit / sniper clips, interaction clips, art (operatives, enemies). |
| [tools-bot-waveeditor](../systems/tools-bot-waveeditor.md) | Editor python scripts, the playtest bot, the Wave Editor and Jev (TypeSafe) tooling - pointers into the code and the change log. |
| [build-test-smokes](../systems/build-test-smokes.md) | Unreal MCP, build / test / verify commands, verification tiers, fixed-step smokes, and the full table of every smoke command with what it checks. |

Also: [TRAPS.md](TRAPS.md) (full §2), [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md) (§7), [NEXT_STEPS.md](NEXT_STEPS.md) (§8), [KNOWN_GAPS.md](KNOWN_GAPS.md) (§9),
[CHANGELOG.md](CHANGELOG.md) (§10), [PORT_MATRIX.md](PORT_MATRIX.md) (Godot file -> UE class table), [TANDEM.md](TANDEM.md), [PROJECT_STATUS_2026-10-14.md](PROJECT_STATUS_2026-10-14.md).

## 6. Data / content status (old §6): where it went

The old §6 blocks (how the user places things, what the artist edits, ABP setup) are now in the doc of their system:

| Old HANDOFF.md | Now |
|---|---|
| §1 Read first | [HANDOFF.md](HANDOFF.md) §1 |
| §2 Environment facts and traps | [TRAPS.md](TRAPS.md) (full table), condensed list in [HANDOFF.md](HANDOFF.md) §2 |
| §3 Build, test, verify (+ the smoke table) | [build-test-smokes.md](../systems/build-test-smokes.md) |
| §4 Conventions | [HANDOFF.md](HANDOFF.md) §4 |
| §5 System map | [docs/systems](../systems/README.md), by system (section 5 above) |
| §6 «Tutorial: how the user places volumes ...» | [tutorial-engine.md](../systems/tutorial-engine.md) (how-to) and [demo01-lake.md](../systems/demo01-lake.md) (rev blocks) |
| §6 «APC: how the user places it ...» | [vehicles.md](../systems/vehicles.md) |
| §6 «How to place a PMC tripwire» | [interactables-traps-loot.md](../systems/interactables-traps-loot.md) |
| §6 «ABP: left-hand IK / aim offset / grenade / knockdown / hit reactions / Female Soldier + sniper», «Interaction clips» | [animation-setup.md](../systems/animation-setup.md) |
| §6 «Weather idle poses» | [survival-cold-heat.md](../systems/survival-cold-heat.md) |
| §6 «Frontend: what the artist edits», «Narrative pipeline» | [dialogue-frontend-mission.md](../systems/dialogue-frontend-mission.md) |
| §6 «Save system» | [save-load.md](../systems/save-load.md) |
| §6 data blocks (tactical heat, vision cones, M16 range, sudden horde, per-member BP, GameMode squad) | [survival-cold-heat](../systems/survival-cold-heat.md), [sight-and-vision-cones](../systems/sight-and-vision-cones.md), [combat-and-fire](../systems/combat-and-fire.md), [waves-horde-encounters](../systems/waves-horde-encounters.md), [squad-and-orders](../systems/squad-and-orders.md) |
| §7 Open user decisions / questions | [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md) (condensed in [HANDOFF.md](HANDOFF.md) §7) |
| §8 Next steps | [NEXT_STEPS.md](NEXT_STEPS.md) |
| §9 Known gaps / tech debt | [KNOWN_GAPS.md](KNOWN_GAPS.md) |
| §10 Change log | [CHANGELOG.md](CHANGELOG.md) |

## 7. Open user decisions / questions (condensed; the full text is in [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md))

- **Melee close-in** shipped (2026-10-18, `ai_tuning.json`); its follow-ups (pack flanking detour, return to cover after a knockdown) are done; the bite-count balance is the open part.
- **Demo 01 rev 5 defaults to confirm** (beat sheet §12): the shaky bridge refuses a second operative; the low fence needs manual prone; moves refused while the HQ intro plays; cones on `O` are session-only; pre-horde patrol = 2 frozen + 2 hounds; stance / vault / cover volumes at the approach (`VOL_A3_*`); precredit on by default.
- **Placed-enemy archetype fix** (approved): a designer who wants another type on a `BP_Enemy_X` sets the field on the instance.
- **APC P4 hull** (9 choices): hull 1200 HP, per-enemy multipliers, enemies target only a manned and running APC within 40 m, crew ejected into cover, repair = Engineer only (5 s), object shot = cannon only, no wheel damage.
- **APC P2** (10 choices): small enemies are run over at >= 1.5 m/s, a downed Brute is not, ram deals 40 and no hull damage, MG stagger removed, turret holds fire in Passive, no sounds yet.
- **APC P1** (7 choices): the pause refuses vehicle orders, turn-based entry is refused while aboard, Alt + Y semantics, `VehicleNav` opt-in per bounds volume, engine heater zone, no path ribbon, slope limit 44 deg.
- **Real time vs turn-based damage numbers** (`tb_parity_2026-10-11.md`): crouch defence 0.75 / 0.70, barrel 120 / 80, mine 120 (90) / 50, blast dodge only in TB - choose per row.
- **Blast gate tied to the generator:** 0.4 %/s, wave caps 30 / 60 / 90 %; «Start Battle» still opens it at once (squad placed behind it); the Wave Editor has no UI for `gate_*` keys yet.
- **Sprint 15 heat** (10 gaps decided by Claude): heat pack -35 points, items from crates, keys U / Shift + G / F, crafting = 1 match + 1 firewood, campfire noise, wind rule, fire fear, no flares in turn-based, burning-barrel puddle, no night system.
- **Sniper vs move orders** (confirmed): automatic fire never interrupts a move order; direct fire orders stop her.
- **Enemy AI direction / bot balance** (2026-10-04): VETERAN target win rate 40-75 %, ranged TB enemies, per-archetype morale; wave 3 and the cold drain are the user's to rebalance (NORMAL 2/10, VETERAN 4/10, CASUAL 1/5 in the bot batch).
- **Settled user decisions** (kept as reference): L_MovementTest stays re-laid; `DefaultEditor.ini` never committed; M16 clip 30; level waves spawn all at once like Godot; start menu has two modes; `CombatStart` tag; turn-based deviations; stage loadout; turn-based entry on flat ground only; `FText::ToLower` for Cyrillic.

## 8. Next steps (old §8: [NEXT_STEPS.md](NEXT_STEPS.md))

Waiting for the user: place / re-id the Demo 01 rev 5 volumes and actors (`docs/systems/demo01-lake.md`, beat sheet §10.1); run `Scripts/Editor/setup_apc_vehicle.py` (editor
closed) and build `L_VehicleTest` (`docs/systems/vehicles.md`); confirm Sprint 15 heat, the sudden-horde flyover and the sniper model; approve or reject the bot's stealth proposal
(`Saved/Coach/stealth_proposal.md`). Ahead: Demo 01 phases 2-3 (encounter groups, A4-A10 steps), APC P3 (turn-based) and P5 (tutorial beat V). Items 1-7 of the old list (loot crates, turrets, generator, targeted shots, UI shell,
the turn-based manager, the Phase 2 data importers) are done; item 8 (content: level, VFX, cutscene; the "twisted" character look) stays open.

## 9. Known gaps / tech debt (old §9: [KNOWN_GAPS.md](KNOWN_GAPS.md))

Parity notes and deliberate deviations per system (enemy AI parity pass and re-path throttle, floating texts, silhouette, camera choreography, camera shake, wave victory, progression,
panic, space hold, barricade contact damage, vault, hidden-mine scan, weapon-switch meshes, grenades, turn-based visuals, click rules, save / load differences, tracers), plus the smoke-reliability
note (known flaky list is EMPTY since 2026-10-15) and the Phase 2 data importer (values still hand-typed). Read the part for the system you touch.

## 10. Change log (old §10: [CHANGELOG.md](CHANGELOG.md))

Newest first, one table row per commit (`| Commit | What |`). Append your row at the top of the table in `docs/port/CHANGELOG.md` with every commit, and keep the matching
`docs/systems/*.md` current. Search it for history: `grep -n "keyword" docs/port/CHANGELOG.md`.
