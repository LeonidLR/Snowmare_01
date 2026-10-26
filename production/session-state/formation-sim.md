# Formation simulator / rules / bot — handoff (cloud session → PC, 2026-10-25)

Branch `claude/stoic-bell-dr29o5`, feature commits **1f8bc25** (v2.2), then v2.3 (red-team fixes, contract section 13) commits on top. Coordinator: session «Дирижер»
(merges into main, assigns tasks; the C++ agent ports the TS rules from the golden files). Read first:
`docs/design/FORMATION_SECTORS.md` (every user decision with its date) and `docs/design/FORMATION_CONTRACT.md`
(the contracts, sections 1–12). The user-facing page: `docs/design/formation-sectors.html` = claude.ai artifact
https://claude.ai/artifact/ErDQfTKvYcV1Q3YzaA19HT (v14 = v2.2). The user's standing rule: **every formation change is
reflected in the page's schemes / clips the same day** ("все, что мы исправляли с точки зрения формации, обновляй в анимациях").

## Where things are (all under `Tools/TrainingGround/`)
| What | File |
|---|---|
| Pure rules (TS mirror of C++ `Formation*Rules`) | `src/movement/rules.ts` — Shape (Slots, HedgehogSlots, TrailSlots), Sector (LookYaw, ClearLook), Terrain (Classify, FormationFor, PassageWidth, SideWidth), Wall (Layout, BreakRoomTie, **Slide**), Guard (ChainPairs, BlockedRoles, Resolve, Effective, TurnAhead), Bypass (ChooseSide, wallSideOf), Corner (Watch), Assign (AssignPlaces), **Cover (Allocate, AllocateStatic)** |
| Menu rules / icons / English labels | `src/movement/menu.ts`, `menuIcons.ts`, `labels.ts` (in-game text is English; Russian only in the TG UI) |
| Simulator | `src/movement/sim.ts` (route legs + bypass planning, classify → guard → targets → places → corner watch → coverage allocator → ClearLook → follower control → separation; wall slide phase) |
| Bot checks + report | `src/movement/checks.ts` (checks 1–11, info 12), `bot.ts` (series), `cli/bot.ts` (`--manual auto|<item>|all`) |
| Tuner (user's API key) | `src/movement/train.ts`, `ai.ts` (prompts, FIXED_PARAMS), `clients.ts` (real Claude / Jev clients, coordinator's) |
| Golden export | `src/movement/golden.ts` → `golden/formation_<Rule>.json` (C++ `CodexTactics.Formation.Parity.*`) |
| Lab generator | `src/movement/cli/genLab.ts` → `Content/Data/FormationLab/formation_lab_01.json` (edit the generator, never the JSON) |
| Config (the rules read it; the tuner tunes it) | `Content/Data/FormationLab/formation_config.json` (schema codex.formation_config/1, ~70 keys with ranges) |
| Unreal map builder | `Scripts/Editor/build_formation_lab.py` (`plan` mode runs with system python) |
| TG UI | `src/departments/MovementDepartment.tsx`, `SquadView.tsx` |
| Tests | `src/movement/*.test.ts` (121 pass at v2.3 part 1): rules, rulesV21, rulesV22, rulesV23, golden, bot, train, menu, levelAudit, clients, redteam |
| Red team | `src/movement/redteam.ts`, `Content/Data/FormationLab/redteam/*.json` (56 kept layouts, `expected_fail` with `since` / `triage`); `npm run bot:movement -- --layouts redteam [--refresh vX]` (~13 min) |

## State at 1f8bc25
- Rules v2.2: triad = K, P, Z; rear guard + coverage allocator on the move (walk / halt, 3+ members) with fixed looks;
  AllocateStatic for a standing group; wall slide; v2.1.1 turn fold + ClearLook; v2.1 guard / trail column / bypass /
  corner; minimal-travel places. LookYaw remains for the pair, run, "all forward", wall mode, standing hedgehog.
- Bot matrix at 1f8bc25 (formation_lab_01, 13 routes, members 2,3,4 × seeds 3; cells = failing runs; (n) = not gating
  for a manual formation; manual gate = checks 1/2/5/7/10/11):

| choice | runs | #1 | #2 | #3 | #4 | #5 | #6 | #7 | #8 | #9 | #10 | #11 | gate |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| auto | 117 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | PASS |
| line | 117 | 0 | 0 | (0) | (144) | 0 | (69) | 0 | (0) | (0) | 0 | - | PASS |
| diamond | 117 | 0 | 0 | (0) | (108) | 0 | (0) | 0 | (0) | (0) | 0 | - | PASS |
| echelon_right | 117 | 0 | 0 | (0) | (81) | 0 | (0) | 0 | (0) | (0) | 0 | - | PASS |
| blizzard | 117 | 0 | 0 | (0) | (144) | 0 | (44) | 0 | (0) | (0) | 0 | - | PASS |
| column | 117 | 0 | 0 | (0) | (126) | 0 | (0) | 0 | (0) | (0) | 0 | - | PASS |
| hedgehog | 117 | 0 | 0 | (0) | (144) | 0 | (48) | 0 | (0) | (0) | 0 | - | PASS |
| echelon_left | 117 | 0 | 0 | (0) | (135) | 0 | (0) | 0 | (0) | (0) | 0 | - | PASS |
| all_forward | 117 | 0 | 0 | (0) | (0) | 0 | - | 0 | (0) | - | 0 | 0 | PASS |

  sample_small: ALL GREEN. Run: `npm run bot:movement -- --layout formation_lab_01 --seeds 3 --manual all` (~5 min).

## v2.3 progress (red team, coordinator task 2026-10-25)
| step | red team failing (still + new) | #1 | #5 | #7 |
|---|---|---|---|---|
| v2.2 (1f8bc25) | 210 | 38 | 70 | 78 |
| part 1 ClearLook min arc + hysteresis | 186 (169 + 17) | 20 | 65 | 77 |
| part 2 trail-column stagger clamp | 113 (95 + 18) | 33 | 34 | 22 |
- v2.2-introduced regressions (`since: 'v2.2'`): 58 -> 26 open after part 1 -> 23 after part 2.
- Next: allocator fixed looks for the narrow column 1A (coordinator); then #5, the rest of #1 (idea tested: a narrow arc reachable
  WITHOUT a sweep beats a wide one behind a sweep - `NARROW` 180 penalty instead of the hard min-arc cut; subset 17 -> 13 new).

## Checklist per rules change (do all of it, same commit)
1. Rule in `rules.ts` (pure; doc comment states the user decision + date); config keys with ranges in formation_config.json.
2. Sim integration in `sim.ts`; a bot check if the decision is checkable (`checks.ts`; manual gate list in checks.ts +
   cli/bot.ts if it applies to manual formations).
3. Tests (`*.test.ts`), `npm test`, `npm run typecheck`.
4. `npm run golden` (and a golden section for a NEW rule in `golden.ts`; compute outputs from the ROUNDED inputs written —
   `place()` rounds scenes; anything else you write rounded must be passed rounded); `npm run golden -- --check`.
5. Bots: auto on formation_lab_01 and sample_small all green; the `--manual all` matrix for the report.
6. Docs: FORMATION_SECTORS.md (the decision with the date, the user's words; strike replaced decisions), FORMATION_CONTRACT.md
   (names / formulas the C++ port needs), the page `formation-sectors.html` (bullets + schemes + clips; render the clips
   with Playwright — `window.__clipDraw(i, t)` with requestAnimationFrame stubbed — and look at them), republish the artifact.
7. `python3 -B Scripts/Tools/typesafe_triage.py --audit-diff --agent claude` (Boundary Breach must PASS); commit (Conventional
   Commits), push, report "ready: <branch> <sha>" + the matrix to the coordinator.

## Open items / ideas (not started)
- Tuner: the new keys (rear.*, coverage.*, slide.*, look.*, turn.*) are tunable; no paid run since v2.2. `bypass.max_detour` is in
  FIXED_PARAMS (user value).
- The wall slide in the sim only starts after a route's wall ARRIVAL; slides starting from a cover order elsewhere and
  AllocateStatic in the standing wall layout (the leader's corner aim) are runtime features — the sim's wall layout keeps the
  user's L8 / K6 / P4 looks (FormationWallRules.Layout).
- Manual-formation pace (#6 in parentheses: line / blizzard / hedgehog) — not gating by design; the line's lag is the 4-wide
  front pivoting.
- Integration choice to confirm with the user if questioned: at a stop after a slide the 4th / the pair partner keeps his end
  when it has room (re-choosing the roomier end would send him through the line) — FORMATION_CONTRACT 12.5.
- Coverage check 10 measures cones only by direction (no range); a check with ranges / occlusion is an idea.

## Gotchas
- Never derive file paths from `URL.pathname` (spaces → `%20`, a leading slash before the drive on Windows): use
  `fileURLToPath` + `path.join` (coordinator rule after the "Unreal Projects" ENOENT).
- CRLF: golden + FormationLab JSON are `eol=lf` in `.gitattributes`; `sameGoldenText` normalises CRLF anyway.
- node 22 type stripping: `node src/movement/cli/*.ts` runs TS directly; `npm test` globs `src/**/*.test.ts`.
- `pkill -f "<pattern>"` in a shell whose own command line contains the pattern kills that shell too.
- The sim is deterministic (seeded); "flake" is never the explanation — debug with a frame dump (positions, slots, yaws).
- The simulator has a light separation standing in for the engine's crowd avoidance and reassigns places on the way when
  1 m cheaper (contract 12.6) — the C++ runtime must not copy the separation (the engine's avoidance does it).
- CLAUDE.md is a protected user file: do not edit it (the Jev audit flags it).
