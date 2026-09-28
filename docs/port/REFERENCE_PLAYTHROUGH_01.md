# Reference playthrough 01 (Godot build, user walkthrough)

Source: `Codex/Videos/Claude Demo 01.mp4` (8:16) + user narration (2026-09-28).
This is the acceptance reference for the UE port: the same flow must be playable in UE.
Times are approximate video timestamps.

## Flow

| # | Time | What happens | Requirement for UE | Port item |
|---|---|---|---|---|
| 1 | 0:00 | Main menu «COLD GRAD: ТАКТИЧЕСКИЙ РЕЖИМ» | Menu with 3 entries: «Начать игру» (exploration → combat), «Начать бой» (tactical preparation), «Начать исследование» (exploration only: quests + cold, no combat) | Main menu (UMG) |
| 2 | 0:25 | Intro dialogue with portrait, «Пропустить» / «Далее» | Dialogue window: speaker portrait, text, next/skip. A cutscene will precede it later | Dialogue system |
| 3 | 0:40 | Squad walks, camera zooms in/out | Camera: WASD pan, wheel zoom, Q/E rotate 45° | Camera |
| 4 | 0:40 | Cold vignette on screen | Cold accumulates in exploration; screen vignette + blue cold bars under squad slots | Cold system, HUD |
| 5 | 1:00 | Loot crates, trapped crates | Plain crates → loot; trapped crates must be defused (animations later) | Loot / interactables |
| 6 | 1:10 | Squad: Commander, Engineer, Medic-sapper | Medic-sapper is better at detecting/defusing mines | Mines, roles |
| 7 | 1:20 | Poses change as they freeze | Freezing changes idle/move pose; all actions get less effective with cold | Cold system, animation layer |
| 8 | 1:30 | Ctrl+X restart, «Пропустить» | Ctrl+X restarts the mission | Game flow |
| 9 | 1:40 | Double click — run into zone | (done) double click = sprint | Movement ✅ |
| 10 | 1:50 | Camera trigger zones | Entering a camera zone switches camera framing; **only the current leader triggers it** | Camera zones |
| 11 | 1:50 | Mine appears from under the snow | Hidden mine revealed on approach; defuse → goes to inventory, else it can explode (−60 HP seen) | Mines |
| 12 | 2:40–3:50 | Warming | (a) fuel barrels lit with matches → cold removed nearby; (b) generator: take empty canister → siphon diesel from overturned APC → fill generator → warm zone; squad recovers | Heat sources, quest chain |
| 13 | 3:00 | Unique level objects | Barricades: pick up, trap (mine), relocate. Turrets work only while the level has power (generator running) | Deployables, power |
| 14 | 4:30 | Barricade relocation | Click barricade → place within radius, mouse wheel / R rotates 45°, LMB confirm, RMB cancel | Deployables |
| 15 | 4:40 | Blue bars drop / recover | Cooling outside warm zones, warming inside generator zone | Cold system |
| 16 | 5:00 | Gate terminal | Terminal: «Открыть ворота» / «Заминировать (2)» / «Отмена»; opening plays a short cutscene | Interactables, cutscene |
| 17 | 5:20 | «ПОДГОТОВКА К БОЮ: N сек · [ПРОБЕЛ] — нижнее меню · Начать бой» | Preparation: each operative is placed individually (1/2/3 selects), can place picked-up barricades; trapping a barricade spends a grenade; a trapped barricade explodes when enemies hit it | Preparation, deployables |
| 18 | 5:45 | Personal inventory (Commander) | Turrets, barricades, mines, medkit, canned food, bread, chocolate, matches | Inventory |
| 19 | 6:10 | «ОБОРОНА: Отразить волну 1! Врагов: 12» | «Начать бой» spawns the wave; squad fires automatically in real time; operatives reaching a barricade take a tactical (cover) position | Waves, combat AI |
| 20 | 6:30 | «РЕЖИМ ПРИКАЗОВ \| Зарядов в волне: 2/3 \| Время планирования» | Space: time slows, click operative then point to give move orders; 3 pauses per wave then cooldown | Tactical pause ✅ logic, orders + UI |
| 21 | 7:10 | Hold Space → zone appears → tactical grid | Hold Space: hold-sphere zone, grid appears, Gorky 17 turn-based combat; weapon behaviour per `docs/GAME_DESIGN_SPEC.md` / weapon data | Turn-based combat |
| 22 | 7:30 | «ХОД ОТРЯДА», AP 8/8, Tab / C / R / F | Turn panel: end squad turn, next soldier [Tab], stance [C] (1 AP), rotate [R], barrel [F]; hit chance rolls logged (e.g. 75% / rolled 88%) | Turn-based combat, HUD |
| 23 | 7:40 | Leave turn-based any time | Hold Space again → back to tactical pause (slow time, orders) | Game flow ✅ logic |
| 24 | 8:10 | «МИССИЯ ПРОВАЛЕНА» | Any squad member dies → mission failed screen with cause + tip + «Начать заново» | Game flow, UI |

## HUD elements seen

- Objective banner top-left («ЦЕЛЬ: …»).
- Message feed top-right with speaker header (Командир, Инженер, Медик-сапёр, ОТРЯД, ШТАБ, GORKY 17).
- Bottom action bar: two colour portrait slots, weapon + ammo «Автомат МТКМ-16 [30/60] | [G] Граната», reload («ПЕР» / «ХОД» in turn-based), stance letter (С/П/Л), «ОБОР» (guard), squad buttons [1] КОМ, [2] ИНЖ, [3] МЕД with HP/cold bars, [4] РЕЗ (locked reserve slot).
- Preparation banner with countdown and «Начать бой».
- Tactical pause banner with charges and planning timer; green order-radius rings.
- Turn-based panel «ХОД ОТРЯДА» (AP, HP, buttons) and red grid cells.

## Consequences for the port order

The video defines a vertical slice. Port order follows it:
1. Camera controls (WASD, zoom, Q/E 45°) and camera trigger zones (leader only).
2. UI shell: main menu, dialogue window, objective banner, message feed, bottom action bar.
3. Cold system + heat sources (barrels with matches, generator warm zone) + vignette/bars.
4. Interactables + inventory + the checkpoint quest chain (canister → APC → generator → terminal → gate).
5. Mines (reveal, medic-sapper bonus, defuse/explode) and trapped crates.
6. Deployables: barricade pick-up/relocate/rotate/trap, turrets with power.
7. Preparation phase (per-operative placement), wave spawning, enemies, real-time auto-combat, cover.
8. Tactical pause orders + UI (logic already ported), mission failed + Ctrl+X restart.
9. Gorky 17 turn-based combat (grid, LOS, AP, weapons, turn panel).
10. Content: Stage 01 level geometry from the Godot/Blender source, character models + animations, cutscene.
