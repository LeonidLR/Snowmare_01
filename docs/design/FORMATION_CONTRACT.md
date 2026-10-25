# Formation work - contracts between the parts (coordinator: «Маршрутизатор моделей», user decision 2026-10-25)

Design source: `docs/design/FORMATION_SECTORS.md` (+ `formation-sectors.html`). This file fixes the DATA the parts exchange, so
the TypeScript simulator (Training Ground), the C++ rules and the Unreal map generator can be built in parallel and meet.
Units: centimetres, degrees, seconds. Yaw: degrees, 0 = +X, counter-clockwise positive (UE yaw); "clock" sectors are relative
to the leader's heading (12 = heading, 3 = right = heading - 90).

## 1. Layout JSON - `Content/Data/FormationLab/<name>.json` (one file feeds the simulator AND the Unreal map generator)
```json
{
  "schema": "codex.formation_layout/1",
  "name": "formation_lab_01",
  "bounds": { "min": [-6000, -6000], "max": [6000, 6000] },
  "ground_z": 0,
  "spawn": { "pos": [0, 0], "yaw": 0 },
  "walls": [ { "id": "W1", "a": [x, y], "b": [x, y], "height": 300, "thickness": 40 } ],
  "zones": [ { "id": "Z1", "kind": "open|corridor|wall_run|gap_run|corner|wall_end", "poly": [[x, y], ...] } ],
  "routes": [ { "id": "R1", "pace": "walk|run", "points": [[x, y], ...],
               "expect": [ { "zone": "Z1", "formation": "diamond|wedge|column|line|echelon_left|echelon_right|wall|hedgehog|blizzard" } ] } ]
}
```
- Walls are straight segments; a gap = two segments. Corners = two segments sharing an end point.
- Zones are labels for the checks (what the terrain classifier SHOULD say), not geometry.
- Routes are the leader's click path; the bot walks the squad along each route.

## 2. Pure rules (same names in TS and C++; TS first, C++ ports them; golden vectors prove equality)
- `FormationShapeRules::Slots(formation, memberCount, leaderPos, leaderYaw, config) -> slots[]` (pos + role K/P/L/Z).
- `FormationSectorRules::LookYaw(role, formation, mode(walk|run|halt|wall), timeInPatrol, leaderYaw, config) -> yaw`
  (patrol scans: P 12->3, L 9->12 counter-phase, dwell; Z walks backwards looking at 6).
- `FormationTerrainRules::Classify(layout walls, leaderPos, leaderYaw, previousClass, timeInClass, config) -> class`
  (open / wall_left / wall_right / corridor / at_wall) with hysteresis (like ColumnHoldTime) -> formation choice.
- `FormationWallRules::Layout(wall segment, clickedSlot, memberCount, roomLeft, roomRight) -> slots[] + look yaws`
  (L 8, K 6 away from the wall, P 4; the 4th at the roomier end, his neighbour turns to the centre).
- Config defaults in one place: `Content/Data/FormationLab/formation_config.json` (spacing, pace 0.8, hysteresis s, scan
  periods); both sides read it.

## 3. Golden vectors - `Tools/TrainingGround/golden/formation_<rule>.json`
```json
{ "schema": "codex.formation_golden/1", "rule": "Slots", "tolerance": { "cm": 0.5, "deg": 0.5 },
  "cases": [ { "in": { ... }, "out": { ... } } ] }
```
Exported by the simulator (`npm run golden`); C++ parity tests `CodexTactics.Formation.Parity.<Rule>` read them from
`FPaths::ProjectDir()/Tools/TrainingGround/golden/` (skip with a warning while the file does not exist yet).

## 4. Movement bot report - `Saved/TrainingGround/movement/report.md` (<= 30 lines; the ONLY thing a coding agent reads)
Header: layout, routes, runs, seed. Then one line per check with pass / fail counts and the worst case (route id, time):
1. wall stare: nobody looks into a wall closer than 200 cm (facing within 45 deg of the wall normal, towards it);
2. rear cover: with 4 members the 5-7 o'clock sector is watched >= 90 % of patrol time; with 3 the gap is reported, not failed;
3. flicker: formation changes per 100 m of route (target <= 1 outside expected changes);
4. expected formation per zone (routes[].expect) matched;
5. slots: no slot inside a wall / off the walkable area; spacing within config;
6. pace: the rear man keeps <= 150 cm behind his slot at patrol pace.
Raw runs: `Saved/TrainingGround/movement/runs.jsonl` (never read by agents). Advice (optional): Jev (`TYPESAFE_API_KEY`) or the
Claude API (`ANTHROPIC_API_KEY`) turns runs.jsonl into <= 10 lines of tuning advice appended to the report; without a key the
report says "no model advice".

## 5. Who does what (coordinator assigns; "ready: <branch> <sha>" to the coordinator, who merges into main)
- Cloud session «Тактическая система формаций»: (1) Training Ground departments shell, schema validation (1.), the TS rules (2.),
  movement simulator + bot + report (4.), golden export (3.), a SMALL sample layout for its own tests.
- Local agent (Opus, worktree): C++ pure rules (2.) + unit tests from the design doc, then parity tests (3.).
- Local agent (Sonnet, worktree, after 1-2 land): the big lab layout JSON (1.) + Unreal map generator script + navmesh bake.
- Local agent (Sonnet): the real Claude API / Jev client for the tuner (6.) and the first budget-capped training runs.
- Later: runtime integration (slots, look yaw, pace, wall pose), hold-G radial menu, in-game smoke on the lab map.

## 6. Bot training on the user's API tokens (user request 2026-10-25: "use the API tokens as much as possible, train the bot through the cloud APIs")
The tuner loop - `Tools/TrainingGround` department "movement", `npm run train -- --budget-usd <N> --rounds <N>`:
1. simulate a series (deterministic TS, 0 tokens): every route x N seeds with the current `formation_config.json`;
2. distil the series to a compact JSON summary (check stats, worst cases, current parameters) - never raw runs;
3. ask the Claude API (`ANTHROPIC_API_KEY`, model `claude-opus-5-5` for proposals, `claude-sonnet-5-5` for summaries; Jev
   `TYPESAFE_API_KEY` for typed pass/fail judgments when available) for the next parameter set as strict JSON within allowed
   ranges (the ranges live next to the defaults in formation_config.json);
4. keep the best set (score = weighted checks of section 4), stop on budget, on no improvement for K rounds, or on all checks green;
5. write `Saved/TrainingGround/movement/train_report.md` (<= 30 lines: rounds, spend, best score, the winning parameters as a
   diff) and `train_best_config.json`. A coding agent reads only train_report.md; the winning config goes into main only after
   the coordinator's review.
Budget: a hard cap per run (`--budget-usd`, default 5), the spend counted from the API's token usage and logged per call; the
key is read from the environment / HKCU and never printed. The same loop pattern later serves the waves bot and the
level-design bot (Training Ground departments).
Where it runs: the user's PC (the key lives there). The cloud session builds the loop with a mock client (no key, no network)
and a `--dry-run`; the coordinator's local agent wires the real client and runs the first training.
