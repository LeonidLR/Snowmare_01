# Project status — CodexTactics «Operation: Cold Silence» (2026-10-14)

A non-technical snapshot of where the project stands, for planning the next steps. Technical detail lives in
`docs/port/HANDOFF.md`; the phase table in `docs/port/PORT_MATRIX.md`; the demo plan in `docs/demo/DEMO01_BEAT_SHEET.md`.

## 1. Snapshot

| | |
|---|---|
| Game | Tactical arctic survival RPG, Gorky 17 tribute. Squad of three (Commander, Engineer, Medic-Sapper) |
| Engine | Unreal Engine 5.8.2, C++ first, Blueprints for content and tuning |
| Reference version | Unreal (since 2026-10-02). The Godot 4.7 original is a frozen read-only archive |
| Repository | `main` pushed to GitHub (`LeonidLR/Snowmare_01`), 330+ commits since 2026-09-28, about 160 of them in the last week |
| Code size | ~720 source files, ~162 000 lines of C++ |
| Automated checks | ~440 unit tests, ~120 in-game smoke tests (headless), pre-commit ownership audit (Jev) |
| Team | Game designer (user). Claude: gameplay code, AI, UI, animation setup, data, tests, tools. Gemini: architecture specs, rendering, shaders, VFX, performance. Work is split between Claude subagents by a difficulty router (Haiku / Sonnet / Opus / Fable) |

## 2. Port from Godot: done

Phases 0–5 of the port are complete: project skeleton, pure game logic (flow, grid, line of sight, action points,
damage, cold, panic / rage), data import, framework (event bus, operatives, input, camera, squad formation), combat
(real time, tactical pause, turn-based, enemy AI, waves), world systems (mines, heat sources, loot, quests, dialogue,
save). Phase 6 (content: art, animation, VFX, the first stage, weather) is partly done: the operative models and
animations are in; the rest continues as Unreal-only work.

## 3. What the game can do today

**Tactics and squad**
- Three time modes in one fight: real time, tactical pause with planned orders, full turn-based with action points.
- Squad control: select / box select, formation following, Solo mode, Regroup, Guard, Commander Mode (AUTO, Ctrl+T) with rules of engagement.
- Tactical cover against walls and barricades: hug, shimmy, corner peek and corner shots, blind fire, low / high cover.
- Stances (stand / crouch / prone) including diving prone on the move, vaulting, knockdown and get-up.
- Weapons: rifles, a kneel / prone-only sniper rifle, grenades, signal flares, targeted shots, squad fire.

**Survival**
- Cold for every operative, wind zones, shelters. Warming: heat packs (hand them to a mate), fire barrels you can push and light, campfire, gas stove, fire puddles. Sprinting warms the body.
- Weather idle poses: shivering, hand warming, the squad gathering and sitting around a fire.

**Enemies and encounters**
- Eight enemy archetypes: Base, Frostbitten, Frost Hound, Spitter, Brute, Cutter, Cryo Drone, Marksman.
- Patrols with routes, sight / hearing / smell perception, guard posts, ambushes, searching after a trap goes off.
- Wave defence with a sudden horde start (slow-motion flyover, «HORDE INCOMING»), flank breaches, a generator and a blast door that opens after wave 3.

**World**
- PMC traps (hidden mines, tripwires, booby-trapped crates) with cold-dependent disarming; the squad can lay its own traps and mine barricades.
- Loot crates, drag-and-drop item hand-over, dropping items, quests, dialogue, turrets, movable barricades.
- APC (BTR-82): board, drive, turret, run over small enemies, hull damage and Engineer repair.

**Game shell**
- Main menu, pause menu, controls screen (F1), tutorial on / off.
- Saving: checkpoints after each tutorial stage, F5 quicksave, F9 quick load, a load list; no autosaves (decision 2026-10-14).
- The squad composition is editable in the editor (`BP_CodexTacticsGameMode` → Squad Roster).

**Tools**
- Wave Editor (web tool) for level / wave data and live balance advice.
- Playtest bot with run telemetry, compressed by Jev into balance patches.
- An editor script that lays out the tutorial level automatically (the designer then adjusts by hand).

## 4. Demo 01 «Frozen Lake» (the current focus)

A ~30-minute playable tutorial level that teaches the game from scratch, built as one map (`L_Demo01_A_FrozenLake`;
review copy with the automatic layout: `Maps/Dev/L_Demo01_A_FrozenLake_AutoLayout`).

| Stage | Content | State |
|---|---|---|
| A0 Drop | HQ radio while the camera lesson runs (pan, MMB drag, rotate, zoom), click to walk, beacon 1 | playable |
| A1 Crossing | shaky bridge one at a time, cold, heat pack lesson, sprint unlocked («running keeps you warm»), tactical hut entry (Solo, stack at the door, one goes in), loot, hand out heat packs | playable |
| A2 Camp | push a fire barrel into a ruined building and light it («a fire indoors doesn't give us away») | playable |
| A3 Approach | cautious approach: stances, vault, crawl under a low fence, cover, corner peek, spotting and disarming mines / tripwires | playable (graybox) |
| A4 Ambush | spot a patrol (frozen + hounds), vision-cone toggle, set tripwires, small fight | cards done; the separate patrol fight before the horde is designed, not built |
| A5–A10 | base supply room with a trapped crate, prepare the defence (barricades, mines, barrels, high ground), canister quest, horde fight lessons (pause, AUTO, generator repair + Engineer on guard) | designed only |

Tutorial engine features: staged lessons, actions done early count automatically, every untaught command is locked until
its lesson, dialogue skip (Enter), progress by location (skipped beacons don't block), checkpoints, retry.

Postponed by decision: the second location «Auto-Repair Garage» (stealth + 4-minute horde), the transfer between maps.

## 5. In progress / next

1. **Now:** fixes from today's play test — orders keep running when another operative is selected, operatives leave cover to follow the formation, half the cold on the lake, the barrel task appears immediately, EXIT arrows at doors + a camera key inside buildings.
2. **Cleanup day** (agreed 2026-10-14): split the largest code file, make the flaky tests reliable, merge the two lake maps into one, audit fixes.
3. **Demo 01 phase 2:** the patrol fight before the horde (encounter groups), then stages A5–A10.
4. **Text pass:** all tutorial texts are drafts (English); a narrative pass is pending (TANDEM request #17).
5. **Art pass:** graybox pieces (bridge, fence, ruin, compound) to be replaced with real art; placeholder VFX for heat sources (Gemini).
6. **APC:** phase P3 (turret in turn-based) and the open balance confirmations.
7. **Gemini requests #15–#21** (rendering / VFX / performance).

## 6. Risks and technical debt

- Two very large code files (the operative character ~4 900 lines, the player controller ~2 900) slow every change to the squad — first item of the cleanup day.
- About eight in-game tests are flaky under load, one has failed for days — makes «green» less trustworthy.
- Two versions of the lake map are edited in parallel — to be merged.
- The tutorial was revised seven times in two days; the engine is generic, but the rules keep growing — keep the beat sheet as the single source of truth.
- Postponed audit items (enemy registry, HUD caches, debug module, bot gate).

## 7. Decisions waiting for the designer

- Keep «SAVE GAME» in the pause menu (it is a manual save, not an autosave)?
- Box select after the hut regroup — keep or drop?
- Vision-cone key (O) — remember the choice between sessions?
- Patrols before the horde: panic and turn-based rules as usual?
- APC: armour numbers, repair only by the Engineer without parts, wheel damage and smoke at 25 % hull.
